// ---------------------------------------------------------------------------
//  SerialPort -- a COM port, plus enumeration of what is present.
//
//  Non-blocking by construction: read timeouts are set so ReadFile always returns
//  at once with whatever is buffered. The monitor thread therefore never blocks on
//  a silent port, and unplugging the board cannot wedge it.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

struct PortInfo {
    std::wstring device;       // "COM20"
    std::wstring description;  // friendly name from SetupAPI, may be empty
};

// Ports currently present, in numeric order.
std::vector<PortInfo> enumerateSerialPorts();

class SerialPort {
public:
    ~SerialPort();

    bool open(const std::wstring &device, int baud);
    void close();
    bool isOpen() const { return m_handle != nullptr; }

    // Appends whatever is buffered. Returns false only on a real port error, not
    // on "nothing to read".
    bool read(std::string &appendTo);

    bool write(const std::string &data);

    const std::wstring &device() const { return m_device; }

private:
    void *m_handle = nullptr;  // HANDLE, kept opaque so windows.h stays out
    std::wstring m_device;
};

// Splits accumulated bytes into complete lines, keeping any partial tail in the
// buffer for next time. Guards against unbounded growth if the far end sends a
// stream with no newline in it.
std::vector<std::string> extractLines(std::string &buffer);
