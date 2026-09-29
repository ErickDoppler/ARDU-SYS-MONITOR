#include "Monitor.h"

#include <windows.h>

#include <chrono>

#include "Protocol.h"
#include "SerialPort.h"

namespace {

using clk = std::chrono::steady_clock;

// How long a candidate port gets to produce a valid request before AUTO moves on.
// Sized against the firmware's 2 s re-request timer with room for jitter: since
// opening the port no longer resets the board (see SerialPort.cpp), detection
// waits for that retry rather than provoking one.
constexpr auto kAutoProbeWindow = std::chrono::milliseconds(3500);
// No request for this long means the board is gone, even if the port is open.
constexpr auto kLinkTimeout = std::chrono::seconds(20);
// No BYTES at all for this long means the handle itself is dead. Distinct from
// the above: a port whose device vanished can stay open and simply return zero
// bytes for ever, which no amount of waiting for a valid request will detect --
// and if a link was never established, the request timeout never applies at all.
constexpr auto kSilenceTimeout = std::chrono::seconds(30);

}  // namespace

Monitor::~Monitor() { stop(); }

void Monitor::start(const Settings &settings, LinkCallback onLinkChange) {
    stop();
    {
        std::lock_guard lock(m_mutex);
        m_settings = settings;
        m_settingsDirty = false;
    }
    m_onLink = std::move(onLinkChange);
    m_stop = false;
    m_thread = std::thread(&Monitor::run, this);
}

void Monitor::stop() {
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
}

void Monitor::applySettings(const Settings &settings) {
    std::lock_guard lock(m_mutex);
    m_settings = settings;
    m_settingsDirty = true;
}

std::wstring Monitor::activePort() const {
    std::lock_guard lock(m_mutex);
    return m_activePort;
}

SourceStatus Monitor::sourceStatus() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

void Monitor::run() {
    SensorHub hub;
    hub.init();
    {
        std::lock_guard lock(m_mutex);
        m_status = hub.status();
    }

    SerialPort port;
    std::string rxBuffer;

    // Which screen the display last asked for. Kept across reconnects so a board
    // reset resumes on the same screen rather than snapping back to CPU.
    int currentScreen = SCR_CPU;
    bool haveRequest = false;

    SensorSnapshot snapshot;
    auto lastSample = clk::now() - std::chrono::hours(1);  // force an immediate sample
    auto lastRequestAt = clk::now();
    auto lastByteAt = clk::now();
    auto lastSendAt = clk::now() - std::chrono::hours(1);

    // AUTO mode state.
    std::vector<PortInfo> candidates;
    size_t candidateIndex = 0;
    auto probeStarted = clk::now();

    bool lastReported = false;
    const auto report = [&](bool up) {
        if (up == lastReported) return;
        lastReported = up;
        m_connected = up;
        if (m_onLink) m_onLink(up);
    };

    while (!m_stop.load()) {
        Settings settings;
        bool dirty = false;
        {
            std::lock_guard lock(m_mutex);
            settings = m_settings;
            dirty = m_settingsDirty;
            m_settingsDirty = false;
        }
        // A settings change and a forced reconnect want the same thing: drop
        // everything and start looking again from scratch. The candidate list is
        // cleared too, so AUTO re-enumerates rather than walking a stale list of
        // ports that may no longer exist.
        if (dirty || m_reconnect.exchange(false)) {
            port.close();
            rxBuffer.clear();
            candidates.clear();
            haveRequest = false;
            lastByteAt = clk::now();
            report(false);
        }

        // --- make sure a port is open ------------------------------------
        if (!port.isOpen()) {
            if (settings.portIsAuto()) {
                if (candidates.empty()) {
                    candidates = enumerateSerialPorts();
                    candidateIndex = 0;
                }
                if (candidates.empty()) {
                    Sleep(1000);
                    continue;
                }
                if (candidateIndex >= candidates.size()) {
                    candidates.clear();  // rescan: the board may have just appeared
                    Sleep(500);
                    continue;
                }
                const std::wstring dev = candidates[candidateIndex++].device;
                if (port.open(dev, SYSMON_BAUD)) {
                    std::lock_guard lock(m_mutex);
                    m_activePort = dev;
                    probeStarted = clk::now();
                    haveRequest = false;
                } else {
                    Sleep(50);
                    continue;
                }
            } else {
                if (port.open(settings.port, SYSMON_BAUD)) {
                    std::lock_guard lock(m_mutex);
                    m_activePort = settings.port;
                    probeStarted = clk::now();
                    haveRequest = false;
                } else {
                    // Named port not there: wait rather than spin on CreateFile.
                    Sleep(1000);
                    continue;
                }
            }
            // Opening asserts DTR, which resets an FTDI/16U2 board. It comes back
            // with {Q;HELLO}, which is what both auto-detect and the link check
            // are waiting for.
            lastRequestAt = clk::now();
        }

        // --- drain the port ----------------------------------------------
        const size_t beforeRead = rxBuffer.size();
        if (!port.read(rxBuffer)) {
            port.close();
            report(false);
            continue;
        }
        if (rxBuffer.size() != beforeRead) lastByteAt = clk::now();

        for (const std::string &line : extractLines(rxBuffer)) {
            const auto req = parseRequest(line);
            if (!req) continue;  // noise, or a checksum failure

            lastRequestAt = clk::now();
            haveRequest = true;

            if (req->kind == Request::Kind::Hello) {
                const int cores = static_cast<int>(snapshot.cores.size());
                if (!port.write(encodeHello(cores))) {
                    port.close();
                    report(false);
                    break;
                }
                report(true);
                continue;
            }

            currentScreen = req->screen;
            report(true);

            // Answer at once, so a tap feels instant no matter how long the
            // polling interval is, then restart the interval from here.
            if (!port.write(encodeScreen(currentScreen, snapshot))) {
                port.close();
                report(false);
                break;
            }
            lastSendAt = clk::now();
        }
        if (!port.isOpen()) continue;

        const auto now = clk::now();

        // --- AUTO: give up on a silent port and try the next one ---------
        if (settings.portIsAuto() && !haveRequest &&
            now - probeStarted > kAutoProbeWindow) {
            port.close();
            continue;
        }

        // --- the handle itself went dead ----------------------------------
        // Catches the post-resume case where the port is still open, ReadFile
        // still succeeds, and nothing ever arrives.
        if (now - lastByteAt > kSilenceTimeout) {
            port.close();
            report(false);
            haveRequest = false;
            candidates.clear();  // the device may now be on a different port
            lastByteAt = now;
            continue;
        }

        // --- the board stopped talking ------------------------------------
        if (haveRequest && now - lastRequestAt > kLinkTimeout) {
            port.close();
            report(false);
            haveRequest = false;
            continue;
        }

        // --- sample on the interval ---------------------------------------
        const auto interval = std::chrono::seconds(settings.pollIntervalSec);
        if (now - lastSample >= interval) {
            snapshot = hub.sample();
            lastSample = now;
            {
                std::lock_guard lock(m_mutex);
                m_status = hub.status();
            }
        }

        // --- resend the current screen on the interval --------------------
        if (haveRequest && now - lastSendAt >= interval) {
            if (!port.write(encodeScreen(currentScreen, snapshot))) {
                port.close();
                report(false);
                continue;
            }
            lastSendAt = now;
        }

        // Polling the port this often keeps a tap responsive without busy-waiting;
        // the sampling cadence is governed by the interval above, not by this.
        Sleep(20);
    }

    port.close();
    report(false);
}
