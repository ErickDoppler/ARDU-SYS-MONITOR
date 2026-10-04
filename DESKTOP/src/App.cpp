#include "App.h"

#include <commctrl.h>
#include <dbt.h>
#include <shellapi.h>

#include "IconFactory.h"
#include "SettingsDialog.h"
#include "resource.h"

namespace {

constexpr const wchar_t *kWindowClass = L"ArduSysMonitorWnd";
constexpr const wchar_t *kMutexName = L"Local\\ArduSysMonitorSingleInstance";

}  // namespace

bool isRunningElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                                        sizeof(elevation), &size) != FALSE;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

App::App() = default;

App::~App() {
    if (m_monitor) m_monitor->stop();
    m_tray.destroy();
    if (m_singleInstance) CloseHandle(m_singleInstance);
}

bool App::init(HINSTANCE instance) {
    m_instance = instance;

    // One tray icon per machine. Local\ scope, because the elevated and
    // unelevated sessions of one user should still collide.
    m_singleInstance = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!m_singleInstance || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
                    L"System Monitor is already running.\n\n"
                    L"Look for the microchip icon in the notification area.",
                    L"System Monitor", MB_ICONINFORMATION | MB_OK);
        return false;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProcThunk;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = icons::createAppIcon(32);
    wc.hIconSm = icons::createAppIcon(16);
    if (!RegisterClassExW(&wc)) return false;

    // A normal top-level window that is simply never shown -- NOT a message-only
    // (HWND_MESSAGE) window, which is the obvious choice and the wrong one:
    //
    //   * SetForegroundWindow cannot succeed on a message-only window, and
    //     TrackPopupMenuEx relies on it to dismiss and to report the clicked
    //     command reliably. The tray menu misbehaves, Exit included.
    //   * WM_POWERBROADCAST and WM_DEVICECHANGE are broadcast to top-level
    //     windows only. A message-only window never hears that the machine
    //     resumed or that USB re-enumerated, so a link lost across sleep is
    //     never noticed.
    //
    // WS_EX_TOOLWINDOW keeps it out of the taskbar and Alt-Tab, so "never shown"
    // really means invisible.
    m_window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"System Monitor",
                               WS_OVERLAPPED, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
                               nullptr, nullptr, instance, this);
    if (!m_window) return false;

    // Explorer broadcasts this after a restart; without handling it the icon
    // disappears for good when Explorer crashes.
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    m_settings = Settings::load();

    if (!m_tray.create(m_window, WM_TRAY_CALLBACK)) return false;

    m_monitor = std::make_unique<Monitor>();

    // Start the board from a known state. This is the autostart case: the
    // machine has just booted, the board has been running on its own for hours
    // without a host, and whatever state it ended up in is not something the
    // agent can inspect or reason about. One reset puts it somewhere definite,
    // with its panel freshly initialised.
    m_monitor->requestBoardReset();
    m_monitor->start(m_settings, [this](bool connected) {
        // Called on the worker thread: hop to the UI thread before touching the
        // tray, since Shell_NotifyIcon belongs to the window's thread.
        PostMessageW(m_window, WM_LINK_CHANGED, connected ? 1 : 0, 0);
    });

    updateTray();
    return true;
}

int App::run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK App::wndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto *self = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->wndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == m_taskbarCreatedMsg && m_taskbarCreatedMsg != 0) {
        m_tray.restore();
        updateTray();
        return 0;
    }

    switch (msg) {
        case WM_TRAY_CALLBACK: {
            // With NOTIFYICON_VERSION_4 the event is in the low word of lParam.
            const UINT event = LOWORD(lp);
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                const UINT cmd = m_tray.showMenu(hwnd);
                if (cmd == IDM_SETTINGS) onSettings();
                else if (cmd == IDM_EXIT) DestroyWindow(hwnd);
            } else if (event == WM_LBUTTONDBLCLK) {
                onSettings();
            }
            return 0;
        }

        case WM_LINK_CHANGED:
            onLinkChanged(wp != 0);
            return 0;

        case WM_POWERBROADCAST:
            // Waking from sleep is the case that used to need the board power
            // cycled. The USB stack re-enumerates while we are suspended, so the
            // handle we hold refers to a device that no longer exists -- and a
            // dead serial handle does not necessarily fail, it can simply return
            // zero bytes forever. Drop it and reopen rather than wait for a
            // timeout that may never fire.
            if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND ||
                wp == PBT_APMRESUMECRITICAL) {
                // Reset the board, not merely reopen the port. Reopening cannot
                // help if the MCU itself stopped while we were suspended: a hung
                // AVR answers nothing, so there is no reply to wait for and no
                // timeout that leads anywhere.
                if (m_monitor) m_monitor->requestBoardReset();
            }
            return TRUE;

        case WM_DEVICECHANGE:
            // DBT_DEVNODES_CHANGED needs no registration and covers the board
            // being replugged, but it fires for ANY device-tree change -- a USB
            // stick, a phone, another program opening a COM port. Acting on it
            // unconditionally would drop a perfectly good link every time.
            //
            // So it only prompts a retry while we are already disconnected,
            // where it turns a slow rediscovery into an immediate one. A link
            // that is up but secretly dead is the silence watchdog's job, not
            // this one's.
            if (wp == DBT_DEVNODES_CHANGED || wp == DBT_DEVICEARRIVAL ||
                wp == DBT_DEVICEREMOVECOMPLETE) {
                if (m_monitor && !m_monitor->connected()) m_monitor->forceReconnect();
            }
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wp) == IDM_SETTINGS) onSettings();
            else if (LOWORD(wp) == IDM_EXIT) DestroyWindow(hwnd);
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (m_monitor) m_monitor->stop();
            m_tray.destroy();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void App::onLinkChanged(bool connected) {
    m_connected = connected;
    updateTray();
}

void App::updateTray() {
    std::wstring detail;
    if (m_monitor) {
        const std::wstring port = m_monitor->activePort();
        if (m_connected && !port.empty()) {
            detail = port;
        } else if (!m_connected) {
            detail = m_settings.portIsAuto() ? L"searching for the board"
                                             : L"waiting for " + m_settings.port;
        }
    }
    m_tray.setConnected(m_connected, detail);
}

void App::onSettings() {
    DialogStatus status;
    status.connected = m_connected;
    if (m_monitor) {
        status.activePort = m_monitor->activePort();
        const SourceStatus src = m_monitor->sourceStatus();
        status.sourceNote = std::wstring(src.note.begin(), src.note.end());
    }

    Settings edited = m_settings;
    if (!showSettingsDialog(m_window, edited, status)) return;

    const bool portChanged = edited.port != m_settings.port;
    const bool intervalChanged = edited.pollIntervalSec != m_settings.pollIntervalSec;

    m_settings = edited;
    m_settings.save();

    if (m_monitor && (portChanged || intervalChanged)) {
        m_monitor->applySettings(m_settings);
        if (portChanged) {
            // The worker drops the link while it reopens; reflect that at once
            // rather than leaving a stale "connected" icon.
            m_connected = false;
            updateTray();
        }
    }
}
