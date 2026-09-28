// ---------------------------------------------------------------------------
//  ProcessSensor -- top processes by CPU, with working set.
//
//  Per-process CPU is a rate, so it needs GetProcessTimes twice and the elapsed
//  wall time between them. The result is divided by the logical processor count,
//  which makes 100% mean "the whole machine" -- the same convention Task
//  Manager's default view uses. Without that division a single busy thread on
//  this 32-thread part would read 3%, and a fully loaded process 3200%.
//
//  This is where running elevated actually earns its keep: without it, OpenProcess
//  fails for services and anything running as another user, so the heaviest
//  process on the machine can be invisible. Failures are skipped rather than
//  reported as zero, so the list stays honest about what it could see.
//
//  Names are sanitised here, at the boundary. A process can be named almost
//  anything, and an executable called "a;b.exe" would otherwise inject a field
//  separator straight into the wire format.
// ---------------------------------------------------------------------------
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <unordered_map>

#include "Sensors.h"

namespace {

uint64_t toU64(const FILETIME &ft) {
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

// Keeps a name safe for the wire and short enough for a 320 px panel. Anything
// that could be mistaken for framing, and any control character, becomes '_'.
std::string sanitiseName(const wchar_t *wide) {
    std::string out;
    out.reserve(SYSMON_PROC_NAME_MAX);
    for (const wchar_t *p = wide; *p && out.size() < SYSMON_PROC_NAME_MAX; ++p) {
        wchar_t c = *p;
        if (c < 32 || c > 126) {
            out.push_back('_');  // the panel font is ASCII 0x20..0x7E only
            continue;
        }
        if (c == SYSMON_SEP || c == SYSMON_SUBSEP || c == SYSMON_CHECK ||
            c == SYSMON_OPEN || c == SYSMON_CLOSE) {
            out.push_back('_');
            continue;
        }
        out.push_back(static_cast<char>(c));
    }
    if (out.empty()) out = "?";
    return out;
}

struct Prev {
    uint64_t cpuTime = 0;
    bool seen = false;
};

class ProcessSensor final : public SensorSource {
public:
    bool init() override {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        m_cpuCount = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1;
        m_prevTick = GetTickCount64();
        m_available = true;
        collect(nullptr);  // establish a baseline so the first list is not all zeros
        return true;
    }

    void sample(SensorSnapshot &out) override {
        if (!m_available) return;
        collect(&out);
    }

    const char *name() const override { return "Win32/Processes"; }
    bool available() const override { return m_available; }

private:
    void collect(SensorSnapshot *out) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return;

        const uint64_t now = GetTickCount64();
        const uint64_t elapsedMs = (now > m_prevTick) ? now - m_prevTick : 0;
        // Process CPU time is in 100 ns units; the divisor converts a delta into
        // a percentage of one core, then of the whole machine.
        const double denom =
            (elapsedMs > 0) ? (static_cast<double>(elapsedMs) * 10000.0 * m_cpuCount) : 0.0;

        std::unordered_map<DWORD, Prev> current;
        current.reserve(512);
        std::vector<ProcessSample> rows;
        rows.reserve(256);

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snap, &entry)) {
            do {
                if (entry.th32ProcessID == 0) continue;  // the idle process

                HANDLE proc = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (!proc) continue;  // not ours to see, even elevated

                FILETIME create{}, exit{}, kernel{}, user{};
                uint64_t cpuTime = 0;
                if (GetProcessTimes(proc, &create, &exit, &kernel, &user)) {
                    cpuTime = toU64(kernel) + toU64(user);
                }

                int memMib = 0;
                PROCESS_MEMORY_COUNTERS pmc{};
                pmc.cb = sizeof(pmc);
                if (GetProcessMemoryInfo(proc, &pmc, sizeof(pmc))) {
                    memMib = static_cast<int>(pmc.WorkingSetSize >> 20);
                }
                CloseHandle(proc);

                Prev &slot = current[entry.th32ProcessID];
                slot.cpuTime = cpuTime;
                slot.seen = true;

                if (!out || denom <= 0.0) continue;

                auto it = m_prev.find(entry.th32ProcessID);
                if (it == m_prev.end()) continue;  // first sighting: no rate yet
                if (cpuTime < it->second.cpuTime) continue;  // PID reused

                const double pct = static_cast<double>(cpuTime - it->second.cpuTime) / denom * 100.0;

                ProcessSample row;
                row.name = sanitiseName(entry.szExeFile);
                row.cpu = static_cast<int>(pct < 0.0 ? 0.0 : (pct > 100.0 ? 100.0 : pct + 0.5));
                row.memMib = memMib;
                rows.push_back(std::move(row));
            } while (Process32NextW(snap, &entry));
        }
        CloseHandle(snap);

        m_prev.swap(current);
        m_prevTick = now;

        if (!out) return;

        // Heaviest CPU first; memory breaks ties so the order does not flicker
        // between equal-CPU idle processes every refresh.
        std::partial_sort(
            rows.begin(),
            rows.begin() + std::min<size_t>(rows.size(), SYSMON_PROC_ROWS),
            rows.end(),
            [](const ProcessSample &a, const ProcessSample &b) {
                if (a.cpu != b.cpu) return a.cpu > b.cpu;
                return a.memMib > b.memMib;
            });

        rows.resize(std::min<size_t>(rows.size(), SYSMON_PROC_ROWS));
        out->processes = std::move(rows);
    }

    bool m_available = false;
    DWORD m_cpuCount = 1;
    uint64_t m_prevTick = 0;
    std::unordered_map<DWORD, Prev> m_prev;
};

}  // namespace

std::unique_ptr<SensorSource> makeProcessSource() {
    return std::make_unique<ProcessSensor>();
}
