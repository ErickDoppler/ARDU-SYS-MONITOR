#include "Protocol.h"

#include <cstdio>

namespace {

// Appends an integer. SYSMON_NA passes through as "-1" like any other value,
// which is what keeps "no sensor" distinguishable from zero all the way to the
// panel.
void addInt(std::string &s, int v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", v);
    s += buf;
}

void addField(std::string &s, int v) {
    s += SYSMON_SEP;
    addInt(s, v);
}

}  // namespace

std::string frame(const std::string &payload) {
    const unsigned char sum =
        sysmonChecksum(payload.data(), static_cast<unsigned int>(payload.size()));

    std::string out;
    out.reserve(payload.size() + 6);
    out += SYSMON_OPEN;
    out += payload;
    out += SYSMON_CHECK;
    out += sysmonHexDigit(static_cast<unsigned char>(sum >> 4));
    out += sysmonHexDigit(static_cast<unsigned char>(sum & 0x0F));
    out += SYSMON_CLOSE;
    out += '\n';
    return out;
}

std::optional<Request> parseRequest(const std::string &line) {
    const size_t open = line.find(SYSMON_OPEN);
    if (open == std::string::npos) return std::nullopt;
    const size_t close = line.find(SYSMON_CLOSE, open + 1);
    if (close == std::string::npos) return std::nullopt;

    std::string body = line.substr(open + 1, close - open - 1);

    // Split off "*CC" and verify before interpreting anything.
    const size_t star = body.rfind(SYSMON_CHECK);
    if (star == std::string::npos || star + 3 != body.size()) return std::nullopt;

    const int hi = sysmonHexValue(body[star + 1]);
    const int lo = sysmonHexValue(body[star + 2]);
    if (hi < 0 || lo < 0) return std::nullopt;

    const std::string payload = body.substr(0, star);
    const unsigned char want = static_cast<unsigned char>((hi << 4) | lo);
    if (sysmonChecksum(payload.data(), static_cast<unsigned int>(payload.size())) != want) {
        return std::nullopt;
    }

    // Expect "Q;<what>".
    const size_t sep = payload.find(SYSMON_SEP);
    if (sep == std::string::npos) return std::nullopt;
    if (payload.compare(0, sep, "Q") != 0) return std::nullopt;

    const std::string what = payload.substr(sep + 1);
    if (what == "HELLO") return Request{Request::Kind::Hello, SCR_CPU};

    if (what.size() >= 2 && (what[0] == 'D' || what[0] == 'd')) {
        const int id = std::atoi(what.c_str() + 1);
        if (id >= SCR_FIRST && id <= SCR_LAST) return Request{Request::Kind::Screen, id};
    }
    return std::nullopt;
}

std::string encodeHello(int coreCount) {
    std::string p = "HELLO;";
    p += SYSMON_APP_NAME;
    addField(p, SYSMON_PROTOCOL_VERSION);
    addField(p, coreCount);
    return frame(p);
}

std::string encodeScreen(int screen, const SensorSnapshot &s) {
    std::string p;
    p.reserve(256);
    p += 'D';
    addInt(p, screen);

    switch (screen) {
        case SCR_CPU:
            addField(p, s.cpuUsage);
            addField(p, s.cpuFreqMhz);
            addField(p, s.cpuTempC);
            addField(p, s.processCount);
            addField(p, static_cast<int>(s.uptimeSec));
            break;

        case SCR_CORES: {
            int n = static_cast<int>(s.cores.size());
            if (n > SYSMON_MAX_CORES) n = SYSMON_MAX_CORES;
            addField(p, n);
            for (int i = 0; i < n; i++) {
                const CoreSample &c = s.cores[static_cast<size_t>(i)];
                p += SYSMON_SEP;
                addInt(p, c.usage);
                p += SYSMON_SUBSEP;
                addInt(p, c.freqMhz);
                p += SYSMON_SUBSEP;
                addInt(p, c.tempC);
            }
            break;
        }

        case SCR_MEM:
            addField(p, s.memUsedMib);
            addField(p, s.memTotalMib);
            addField(p, s.memCachedMib);
            addField(p, s.commitUsedMib);
            addField(p, s.commitLimitMib);
            break;

        case SCR_GPU:
            addField(p, s.gpuUsage);
            addField(p, s.gpuPowerDw);
            addField(p, s.gpuClockMhz);
            addField(p, s.vramUsedMib);
            addField(p, s.vramTotalMib);
            addField(p, s.fps);
            addField(p, s.gpuTempC);
            break;

        case SCR_POWER:
            addField(p, s.powerTotalDw);
            addField(p, s.powerCpuDw);
            addField(p, s.powerGpuDw);
            addField(p, s.powerOtherDw);
            break;

        case SCR_NET:
            addField(p, s.netRxKibs);
            addField(p, s.netTxKibs);
            addField(p, s.netRxTotalMib);
            addField(p, s.netTxTotalMib);
            addField(p, s.netLinkMbps);
            break;

        case SCR_PROC: {
            addField(p, s.cpuUsage);
            int n = static_cast<int>(s.processes.size());
            if (n > SYSMON_PROC_ROWS) n = SYSMON_PROC_ROWS;
            addField(p, n);
            for (int i = 0; i < n; i++) {
                const ProcessSample &e = s.processes[static_cast<size_t>(i)];
                p += SYSMON_SEP;
                p += e.name;  // already sanitised in ProcessSensor
                p += SYSMON_SUBSEP;
                addInt(p, e.cpu);
                p += SYSMON_SUBSEP;
                addInt(p, e.memMib);
            }
            break;
        }

        default:
            break;
    }

    return frame(p);
}
