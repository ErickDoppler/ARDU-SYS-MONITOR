// ---------------------------------------------------------------------------
//  Settings -- the three things the user can change, persisted per user.
// ---------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>

// The intervals offered in the dialog, in seconds.
inline const std::vector<int> &pollIntervalChoices() {
    static const std::vector<int> v{2, 5, 10, 15, 30};
    return v;
}

// Sentinel for "pick a port yourself". Stored as-is so the choice survives the
// board moving to a different COM number.
inline constexpr const wchar_t *kPortAuto = L"AUTO";

struct Settings {
    bool autostart = false;
    std::wstring port = kPortAuto;
    int pollIntervalSec = 5;

    // Reads from HKCU, filling in defaults for anything absent or invalid.
    static Settings load();

    // Writes to HKCU. Returns false if the registry write failed.
    bool save() const;

    bool portIsAuto() const { return port == kPortAuto; }
};
