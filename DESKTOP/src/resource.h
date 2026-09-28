// ---------------------------------------------------------------------------
//  resource.h -- IDs shared between app.rc and the code.
// ---------------------------------------------------------------------------
#pragma once

#define IDI_APPICON        101
#define IDD_SETTINGS       201

#define IDC_AUTOSTART      1001
#define IDC_PORT_COMBO     1002
#define IDC_INTERVAL_COMBO 1003
#define IDC_STATUS_TEXT    1004
#define IDC_SOURCES_TEXT   1005

// Tray menu commands. Kept clear of IDOK/IDCANCEL.
#define IDM_SETTINGS       2001
#define IDM_EXIT           2002

// Private window messages.
#define WM_TRAY_CALLBACK   (WM_APP + 1)
#define WM_LINK_CHANGED    (WM_APP + 2)
