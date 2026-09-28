// ---------------------------------------------------------------------------
//  selftest -- exercises the sensors and the wire encoder from a console, with
//  no tray icon and no serial port involved.
//
//  Worth having as its own binary: when a value on the panel looks wrong, this
//  answers "is the sensor wrong or is the firmware drawing it wrong?" in one
//  step. It prints exactly the bytes the display would receive.
//
//  Also round-trips every encoded line back through the request parser's
//  checksum check, so a framing mistake shows up here rather than as a silently
//  ignored line on the Arduino.
//
//  Build:  tools\build-selftest.ps1
// ---------------------------------------------------------------------------
#include <windows.h>
#include <stdio.h>

#include <string>
#include <vector>

#include "../src/Protocol.h"
#include "../src/SerialPort.h"
#include "../src/sensors/Sensors.h"

namespace {

const char *screenName(int id) {
    switch (id) {
        case SCR_CPU: return "D1 cpu";
        case SCR_CORES: return "D2 cores";
        case SCR_MEM: return "D3 memory";
        case SCR_GPU: return "D4 gpu";
        case SCR_POWER: return "D5 power";
        case SCR_NET: return "D6 network";
        case SCR_PROC: return "D7 processes";
        default: return "?";
    }
}

void printValue(const char *label, int v, const char *unit) {
    if (v == SYSMON_NA) {
        printf("  %-16s n/a\n", label);
    } else {
        printf("  %-16s %d %s\n", label, v, unit);
    }
}

// Verifies a produced line survives the same checksum path the firmware uses.
bool checksumRoundTrips(const std::string &line) {
    const size_t open = line.find('{');
    const size_t close = line.find('}', open + 1);
    if (open == std::string::npos || close == std::string::npos) return false;

    const std::string body = line.substr(open + 1, close - open - 1);
    const size_t star = body.rfind('*');
    if (star == std::string::npos || star + 3 != body.size()) return false;

    const int hi = sysmonHexValue(body[star + 1]);
    const int lo = sysmonHexValue(body[star + 2]);
    if (hi < 0 || lo < 0) return false;

    const std::string payload = body.substr(0, star);
    return sysmonChecksum(payload.data(), (unsigned)payload.size()) ==
           (unsigned char)((hi << 4) | lo);
}

}  // namespace

int main() {
    printf("=== ports ===\n");
    for (const PortInfo &p : enumerateSerialPorts()) {
        printf("  %-8ls %ls\n", p.device.c_str(), p.description.c_str());
    }

    printf("\n=== sensors: priming ===\n");
    SensorHub hub;
    hub.init();

    // Rate counters need two collections separated by real time; sampling twice
    // is what the monitor thread does before its first send.
    hub.sample();
    Sleep(1200);
    SensorSnapshot s = hub.sample();

    const SourceStatus st = hub.status();
    printf("  PDH/Win32   %s\n", st.pdh ? "yes" : "NO");
    printf("  NVML        %s\n", st.nvml ? "yes" : "no");
    printf("  Afterburner %s\n", st.afterburner ? "yes" : "no");
    printf("  RTSS        %s\n", st.rtss ? "yes" : "no");
    printf("  note: %s\n", st.note.c_str());

    printf("\n=== readings ===\n");
    printValue("cpu usage", s.cpuUsage, "%");
    printValue("cpu freq", s.cpuFreqMhz, "MHz");
    printValue("cpu temp", s.cpuTempC, "C");
    printValue("processes", s.processCount, "");
    printf("  %-16s %d\n", "cores", (int)s.cores.size());
    if (!s.cores.empty()) {
        printf("    core0 %d%% @ %d MHz    core%d %d%% @ %d MHz\n", s.cores[0].usage,
               s.cores[0].freqMhz, (int)s.cores.size() - 1, s.cores.back().usage,
               s.cores.back().freqMhz);
    }
    printValue("mem used", s.memUsedMib, "MiB");
    printValue("mem total", s.memTotalMib, "MiB");
    printValue("gpu usage", s.gpuUsage, "%");
    printValue("gpu clock", s.gpuClockMhz, "MHz");
    printValue("gpu temp", s.gpuTempC, "C");
    printValue("gpu power", s.gpuPowerDw, "dW");
    printValue("vram used", s.vramUsedMib, "MiB");
    printValue("vram total", s.vramTotalMib, "MiB");
    printValue("fps", s.fps, "");
    printValue("power total", s.powerTotalDw, "dW");
    printValue("net rx", s.netRxKibs, "KiB/s");
    printValue("net tx", s.netTxKibs, "KiB/s");
    printValue("link", s.netLinkMbps, "Mbit/s");

    printf("\n  top processes:\n");
    for (const auto &p : s.processes) {
        printf("    %-16s %3d%%  %6d MiB\n", p.name.c_str(), p.cpu, p.memMib);
    }

    printf("\n=== wire output ===\n");
    bool allOk = true;
    printf("%s", encodeHello((int)s.cores.size()).c_str());
    for (int id = SCR_FIRST; id <= SCR_LAST; id++) {
        const std::string line = encodeScreen(id, s);
        const bool ok = checksumRoundTrips(line);
        allOk = allOk && ok;
        printf("%s", line.c_str());
        if (!ok) printf("   ^^^ CHECKSUM ROUND TRIP FAILED (%s)\n", screenName(id));
        if (line.size() > SYSMON_MAX_LINE) {
            allOk = false;
            printf("   ^^^ OVER SYSMON_MAX_LINE: %d bytes for %s\n", (int)line.size(),
                   screenName(id));
        }
    }

    printf("\n=== request parser ===\n");
    const char *cases[] = {
        "{Q;HELLO*13}",      // valid, checksum filled in below
        "{Q;D4*0F}",
        "{Q;D9*00}",         // out of range
        "{Q;D4*FF}",         // bad checksum
        "garbage",
    };
    // Build two correct ones programmatically so the test does not depend on
    // hand-computed sums.
    const std::string goodHello = frame("Q;HELLO");
    const std::string goodD4 = frame("Q;D4");
    printf("  %-14s -> %s\n", "frame(Q;HELLO)",
           parseRequest(goodHello) ? "accepted" : "REJECTED");
    printf("  %-14s -> %s\n", "frame(Q;D4)",
           parseRequest(goodD4) ? "accepted" : "REJECTED");
    for (const char *c : cases) {
        const auto r = parseRequest(c);
        printf("  %-14s -> %s\n", c, r ? "accepted" : "rejected");
    }
    if (!parseRequest(goodHello) || !parseRequest(goodD4)) allOk = false;

    printf("\n%s\n", allOk ? "SELFTEST OK" : "SELFTEST FOUND PROBLEMS");
    return allOk ? 0 : 1;
}
