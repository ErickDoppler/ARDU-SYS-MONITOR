// ---------------------------------------------------------------------------
//  App -- the hidden owner window, the tray icon and the worker's lifetime.
// ---------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <memory>

#include "Monitor.h"
#include "Settings.h"
#include "TrayIcon.h"

class App {
public:
    App();
    ~App();

    // False if another instance already has the tray icon, or the window could
    // not be created.
    bool init(HINSTANCE instance);
    int run();

private:
    static LRESULT CALLBACK wndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT wndProc(HWND, UINT, WPARAM, LPARAM);

    void onSettings();
    void onLinkChanged(bool connected);
    void updateTray();

    HINSTANCE m_instance = nullptr;
    HWND m_window = nullptr;
    HANDLE m_singleInstance = nullptr;
    UINT m_taskbarCreatedMsg = 0;

    TrayIcon m_tray;
    Settings m_settings;
    std::unique_ptr<Monitor> m_monitor;
    bool m_connected = false;
};

// True when the process holds an elevated token.
bool isRunningElevated();
