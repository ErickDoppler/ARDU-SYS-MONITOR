// ---------------------------------------------------------------------------
//  Autostart -- start with Windows, already elevated.
//
//  Deliberately NOT the HKCU\...\Run key. This app exits unless it is running as
//  administrator, and a Run entry launches unelevated: the app would start on
//  every logon purely to show its "must run as admin" error. There is no way to
//  raise privileges from a Run entry without a UAC prompt at every logon either.
//
//  A scheduled task with RunLevel=HIGHEST and a logon trigger is the supported
//  way to do this. It starts elevated, silently, once per logon, with no prompt.
// ---------------------------------------------------------------------------
#pragma once

#include <string>

namespace autostart {

// True if our task exists and is enabled.
bool isEnabled();

// Creates or replaces the task. Returns false with a reason on failure -- the
// caller shows it, because a silently ignored autostart toggle is worse than an
// error.
bool enable(std::wstring &errorOut);

// Removes the task. Missing is treated as success.
bool disable(std::wstring &errorOut);

}  // namespace autostart
