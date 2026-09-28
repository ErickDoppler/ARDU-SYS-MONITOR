#include "Settings.h"

#include <windows.h>

#include <algorithm>

namespace {

constexpr const wchar_t *kKey = L"Software\\ArduSysMonitor";

bool readDword(HKEY key, const wchar_t *name, DWORD &out) {
    DWORD type = 0, size = sizeof(out);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&out),
                            &size) == ERROR_SUCCESS &&
           type == REG_DWORD;
}

bool readString(HKEY key, const wchar_t *name, std::wstring &out) {
    wchar_t buf[128]{};
    DWORD type = 0, size = sizeof(buf);
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf),
                         &size) != ERROR_SUCCESS ||
        type != REG_SZ) {
        return false;
    }
    out = buf;
    return true;
}

}  // namespace

Settings Settings::load() {
    Settings s;

    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return s;  // first run: defaults
    }

    DWORD dw = 0;
    if (readDword(key, L"Autostart", dw)) s.autostart = dw != 0;

    std::wstring port;
    if (readString(key, L"Port", port) && !port.empty()) s.port = port;

    if (readDword(key, L"PollIntervalSec", dw)) {
        // Only accept a value the dialog could have produced; a hand-edited 0
        // would spin the monitor thread flat out.
        const auto &choices = pollIntervalChoices();
        if (std::find(choices.begin(), choices.end(), static_cast<int>(dw)) !=
            choices.end()) {
            s.pollIntervalSec = static_cast<int>(dw);
        }
    }

    RegCloseKey(key);
    return s;
}

bool Settings::save() const {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_WRITE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD autostartDw = autostart ? 1u : 0u;
    const DWORD intervalDw = static_cast<DWORD>(pollIntervalSec);

    bool ok = true;
    ok &= RegSetValueExW(key, L"Autostart", 0, REG_DWORD,
                         reinterpret_cast<const BYTE *>(&autostartDw),
                         sizeof(autostartDw)) == ERROR_SUCCESS;
    ok &= RegSetValueExW(key, L"PollIntervalSec", 0, REG_DWORD,
                         reinterpret_cast<const BYTE *>(&intervalDw),
                         sizeof(intervalDw)) == ERROR_SUCCESS;
    ok &= RegSetValueExW(
              key, L"Port", 0, REG_SZ,
              reinterpret_cast<const BYTE *>(port.c_str()),
              static_cast<DWORD>((port.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;

    RegCloseKey(key);
    return ok;
}
