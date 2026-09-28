// ---------------------------------------------------------------------------
//  IconFactory -- the microchip icon, drawn in code.
//
//  Procedural rather than a checked-in bitmap for two reasons: the tray icon has
//  to change with connectivity, and generating it means the taskbar icon, the
//  tray icon and the .ico embedded in the executable are all the same artwork
//  from the same source, at whatever size Windows asks for.
//
//  The 3D effect is a lit bevel: a fixed light direction from the top-left gives
//  the body a raised edge, the pins a rounded highlight, and the die a recessed
//  inner shadow. All computed per pixel into a 32bpp ARGB DIB, which is also why
//  alpha is correct at every size -- GDI shape drawing does not write alpha, so
//  anything drawn with it would come out opaque-square at small sizes.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

namespace icons {

// A plain microchip. Used for the window class, Alt-Tab and the embedded .ico.
HICON createAppIcon(int size);

// The microchip plus link state: blue "ON" when the board is talking to us,
// dark red "OFF" when it is not.
HICON createTrayIcon(int size, bool connected);

// Writes a multi-resolution .ico. Used by "sysmon.exe --export-icon <path>" at
// build time so the resource and the runtime icon can never drift apart.
bool writeIcoFile(const wchar_t *path);

}  // namespace icons
