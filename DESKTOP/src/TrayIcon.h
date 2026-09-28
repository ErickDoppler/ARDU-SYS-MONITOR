// ---------------------------------------------------------------------------
//  TrayIcon -- the notification-area icon and its menu.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <string>

class TrayIcon {
public:
    ~TrayIcon();

    bool create(HWND owner, UINT callbackMessage);
    void destroy();

    // Swaps the icon and tooltip for the link state. Cheap enough to call on
    // every change; it does nothing if the state has not actually moved.
    void setConnected(bool connected, const std::wstring &detail);

    // Shows the context menu at the cursor. Returns the chosen command, or 0.
    UINT showMenu(HWND owner);

    // Re-adds the icon after an Explorer restart.
    void restore();

private:
    void refreshIcon();

    HWND m_owner = nullptr;
    UINT m_callback = 0;
    HICON m_icon = nullptr;
    bool m_connected = false;
    bool m_added = false;
    bool m_stateKnown = false;
    std::wstring m_detail;
};
