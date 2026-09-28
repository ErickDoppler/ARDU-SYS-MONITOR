// ---------------------------------------------------------------------------
//  Monitor -- the worker that owns the serial link and the sensors.
//
//  Runs on its own thread so that sampling, which touches PDH and walks every
//  process, never stalls the UI thread and its tray message pump.
//
//  The display drives the exchange (see PROTOCOL.md): this side answers requests
//  and then repeats the current screen at the polling interval. It never decides
//  which screen is showing.
// ---------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "Settings.h"
#include "sensors/Sensors.h"

class Monitor {
public:
    // Called whenever the link comes up or goes down, from the worker thread.
    using LinkCallback = std::function<void(bool connected)>;

    ~Monitor();

    void start(const Settings &settings, LinkCallback onLinkChange);
    void stop();

    // Applied at the top of the next loop iteration; reopens the port if the
    // device changed.
    void applySettings(const Settings &settings);

    bool connected() const { return m_connected.load(); }
    std::wstring activePort() const;
    SourceStatus sourceStatus() const;

private:
    void run();
    bool ensurePort();
    void serviceLine(const std::string &line);

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_connected{false};

    mutable std::mutex m_mutex;
    Settings m_settings;
    std::wstring m_activePort;
    SourceStatus m_status;
    bool m_settingsDirty = false;

    LinkCallback m_onLink;
};
