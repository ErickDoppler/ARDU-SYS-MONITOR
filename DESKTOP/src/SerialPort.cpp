#include "SerialPort.h"

#include <windows.h>
#include <setupapi.h>
#include <devguid.h>

#include <algorithm>
#include <cstdlib>

#include "sysmon_wire.h"

SerialPort::~SerialPort() { close(); }

bool SerialPort::open(const std::wstring &device, int baud) {
    close();

    // The \\.\ prefix is required for COM10 and above; harmless below it, so it
    // is applied unconditionally rather than parsing the number.
    std::wstring path = L"\\\\.\\" + device;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        CloseHandle(h);
        return false;
    }
    dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    // No flow control, and critically DTR and RTS are left DEASSERTED.
    //
    // On a board with an FTDI or 16U2 bridge, DTR is wired to RESET through a
    // capacitor: asserting it reboots the MCU. Opening the port with
    // DTR_CONTROL_ENABLE therefore resets the display every single time the
    // agent connects -- and the agent reconnects on a settings change, on a link
    // timeout and on any write error. The visible symptom is the firmware's
    // splash screen reappearing and all graph history being wiped, because
    // setup() really did run again.
    //
    // Nothing needs that reset. The firmware re-sends its request every 2 s
    // while it has no link, so auto-detection still works; it just waits for the
    // next retry instead of forcing one.
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(h, &dcb)) {
        CloseHandle(h);
        return false;
    }

    // Fully non-blocking reads: return immediately with whatever is buffered.
    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout = MAXDWORD;
    to.ReadTotalTimeoutMultiplier = 0;
    to.ReadTotalTimeoutConstant = 0;
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(h, &to);

    SetupComm(h, 4096, 4096);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    m_handle = h;
    m_device = device;
    return true;
}

void SerialPort::close() {
    if (m_handle) {
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
    m_device.clear();
}

bool SerialPort::read(std::string &appendTo) {
    if (!m_handle) return false;

    char buf[1024];
    DWORD got = 0;
    if (!ReadFile(static_cast<HANDLE>(m_handle), buf, sizeof(buf), &got, nullptr)) {
        return false;  // the port went away
    }
    if (got > 0) appendTo.append(buf, got);

    // ReadFile succeeding is not proof the device is still there. When a USB
    // serial adapter disappears -- unplugged, or re-enumerated while the machine
    // was asleep -- the handle can stay "valid" and simply return zero bytes for
    // ever, so a reader that only checks ReadFile waits for data that will never
    // come. ClearCommError talks to the driver and fails once the device is gone,
    // which is what actually detects it.
    //
    // It also clears any latched framing or overrun flags. Those are not treated
    // as fatal: a single glitch on a hot-plugged cable is normal, the affected
    // line fails its checksum and is dropped, and dropping the whole link for one
    // bad byte would be worse than the glitch.
    DWORD commErrors = 0;
    COMSTAT status{};
    if (!ClearCommError(static_cast<HANDLE>(m_handle), &commErrors, &status)) {
        return false;
    }
    return true;
}

bool SerialPort::pulseDtrReset() {
    if (!m_handle) return false;

    // On a board with an FTDI or 16U2 bridge, DTR is wired to RESET through a
    // capacitor, so asserting it reboots the MCU. The port is normally opened
    // with DTR_CONTROL_DISABLE precisely to avoid that -- resetting on every
    // reconnect wiped the display's graph history and made a dropped link look
    // like a crash.
    //
    // Deliberately, at one moment, it is the only thing that can revive a board
    // whose MCU has stopped: a hung AVR answers nothing, so no amount of
    // reopening the port or waiting reaches it. The reset makes setup() run,
    // which reconfigures the panel and clears a blank white screen.
    //
    // EscapeCommFunction rather than the DCB, because the DCB deliberately holds
    // DTR deasserted and this has to override that for one pulse.
    auto *h = static_cast<HANDLE>(m_handle);

    if (!EscapeCommFunction(h, SETDTR)) return false;
    Sleep(120);  // comfortably past the RC time constant on the reset line
    if (!EscapeCommFunction(h, CLRDTR)) return false;

    // The bootloader waits about a second before handing over, and setup() then
    // initialises the panel. Returning earlier would mean talking to a board
    // that is still in its bootloader, whose replies are not our protocol.
    Sleep(1800);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

bool SerialPort::write(const std::string &data) {
    if (!m_handle) return false;

    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(m_handle), data.data(),
                   static_cast<DWORD>(data.size()), &written, nullptr)) {
        return false;
    }
    return written == data.size();
}

std::vector<std::string> extractLines(std::string &buffer) {
    std::vector<std::string> lines;

    size_t start = 0;
    for (;;) {
        const size_t nl = buffer.find('\n', start);
        if (nl == std::string::npos) break;

        std::string line = buffer.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(std::move(line));
        start = nl + 1;
    }

    buffer.erase(0, start);

    // A peer stuck sending bytes with no newline must not grow this without
    // bound. Two max-length messages is plenty of slack for a partial tail.
    if (buffer.size() > SYSMON_MAX_LINE * 2) buffer.clear();

    return lines;
}

std::vector<PortInfo> enumerateSerialPorts() {
    std::vector<PortInfo> ports;

    HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr,
                                        DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return ports;

    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); i++) {
        HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV,
                                        KEY_READ);
        if (key == INVALID_HANDLE_VALUE) continue;

        wchar_t name[64]{};
        DWORD size = sizeof(name);
        DWORD type = 0;
        const LONG rc =
            RegQueryValueExW(key, L"PortName", nullptr, &type,
                             reinterpret_cast<LPBYTE>(name), &size);
        RegCloseKey(key);

        if (rc != ERROR_SUCCESS || type != REG_SZ) continue;
        // LPT ports live in the same device class.
        if (wcsncmp(name, L"COM", 3) != 0) continue;

        PortInfo pi;
        pi.device = name;

        wchar_t desc[256]{};
        if (SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_FRIENDLYNAME, nullptr,
                                             reinterpret_cast<PBYTE>(desc), sizeof(desc),
                                             nullptr)) {
            pi.description = desc;
        }
        ports.push_back(std::move(pi));
    }

    SetupDiDestroyDeviceInfoList(set);

    std::sort(ports.begin(), ports.end(), [](const PortInfo &a, const PortInfo &b) {
        return _wtoi(a.device.c_str() + 3) < _wtoi(b.device.c_str() + 3);
    });
    return ports;
}
