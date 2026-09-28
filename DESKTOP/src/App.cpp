#include "App.h"

#include <commctrl.h>
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

    // A message-only window: it never shows, it just owns the tray icon and
    // receives its callbacks on the UI thread.
    m_window = CreateWindowExW(0, kWindowClass, L"System Monitor", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, instance, this);
    if (!m_window) return false;

    // Explorer broadcasts this after a restart; without handling it the icon
    // disappears for good when Explorer crashes.
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    m_settings = Settings::load();

    if (!m_tray.create(m_window, WM_TRAY_CALLBACK)) return false;

    m_monitor = std::make_unique<Monitor>();
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
