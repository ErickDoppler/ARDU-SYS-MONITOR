// ---------------------------------------------------------------------------
//  SettingsDialog -- the modal settings window.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <string>

#include "Settings.h"

// Live information shown in the Status group, so a user can see why a value reads
// n/a without digging through a log.
struct DialogStatus {
    bool connected = false;
    std::wstring activePort;
    std::wstring sourceNote;
};

// Returns true if the user pressed Apply, with the result in `settings`.
// Autostart is applied here rather than by the caller: the checkbox has to reflect
// whether the scheduled task was really created.
bool showSettingsDialog(HWND owner, Settings &settings, const DialogStatus &status);
