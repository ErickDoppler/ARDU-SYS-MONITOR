#include "TrayIcon.h"

#include <shellapi.h>

#include "IconFactory.h"
#include "resource.h"

namespace {

constexpr UINT kIconId = 1;

NOTIFYICONDATAW makeData(HWND owner) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner;
    nid.uID = kIconId;
    return nid;
}

}  // namespace

TrayIcon::~TrayIcon() { destroy(); }

bool TrayIcon::create(HWND owner, UINT callbackMessage) {
    m_owner = owner;
    m_callback = callbackMessage;

    refreshIcon();

    NOTIFYICONDATAW nid = makeData(owner);
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = m_callback;
    nid.hIcon = m_icon;
    wcscpy_s(nid.szTip, L"System Monitor - starting");

    m_added = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (m_added) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
    return m_added;
}

void TrayIcon::destroy() {
    if (m_added && m_owner) {
        NOTIFYICONDATAW nid = makeData(m_owner);
        Shell_NotifyIconW(NIM_DELETE, &nid);
        m_added = false;
    }
    if (m_icon) {
        DestroyIcon(m_icon);
        m_icon = nullptr;
    }
}

void TrayIcon::refreshIcon() {
    // Ask for the size the shell actually wants, so the icon stays crisp at high
    // DPI instead of being a scaled 16 px bitmap.
    const int cx = GetSystemMetrics(SM_CXSMICON);
    HICON fresh = icons::createTrayIcon(cx > 0 ? cx : 16, m_connected);
    if (!fresh) return;

    HICON old = m_icon;
    m_icon = fresh;
    if (old) DestroyIcon(old);
}

void TrayIcon::setConnected(bool connected, const std::wstring &detail) {
    if (m_stateKnown && connected == m_connected && detail == m_detail) return;
    m_connected = connected;
    m_detail = detail;
    m_stateKnown = true;

    refreshIcon();
    if (!m_added) return;

    NOTIFYICONDATAW nid = makeData(m_owner);
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = m_icon;

    std::wstring tip = connected ? L"System Monitor - connected" : L"System Monitor - no link";
    if (!detail.empty()) {
        tip += L"\n";
        tip += detail;
    }
    // szTip holds 128 wchars including the terminator; overflowing it silently
    // drops the whole tooltip.
    if (tip.size() > 127) tip.resize(127);
    wcscpy_s(nid.szTip, tip.c_str());

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::restore() {
    m_added = false;
    create(m_owner, m_callback);
    m_stateKnown = false;
    setConnected(m_connected, m_detail);
}

UINT TrayIcon::showMenu(HWND owner) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return 0;

    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);

    // Both of these are required, and both are easy to leave out because the
    // menu still appears without them. SetForegroundWindow makes the menu
    // dismiss when the user clicks elsewhere and makes TrackPopupMenuEx report
    // the clicked command reliably; it only works because the owner is a real
    // top-level window. The WM_NULL afterwards is the documented workaround for
    // the menu otherwise sticking around after a selection.
    SetForegroundWindow(owner);

    const UINT cmd = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, owner,
        nullptr));

    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return cmd;
}
