// ---------------------------------------------------------------------------
//  CpuSensor -- total and per-core usage, plus real (not nominal) frequency.
//
//  Two PDH counter sets over the "Processor Information" object:
//
//    % Processor Time         usage, per logical processor and _Total
//    % Processor Performance  current speed as a percentage of base clock
//
//  Frequency is base_MHz * performance / 100. That is how Task Manager gets the
//  number, and it tracks boost and downclocking -- Win32_Processor.MaxClockSpeed
//  is a nameplate value that never moves. On this 5950X, base reads 3401 MHz
//  while a boosting core reports well over 100% performance.
//
//  Everything uses PdhAddEnglishCounter. The localised variants resolve counter
//  names against the system language, so on a non-English Windows -- like this
//  machine -- "\Processor Information(*)\% Processor Time" simply does not exist
//  and every add fails. This is the single most common way PDH code breaks on
//  someone else's PC.
// ---------------------------------------------------------------------------
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <vector>
#include <string>
#include <cmath>

#include "Sensors.h"

namespace {

// Reads the nominal base clock once. Registry, because it is cheap and does not
// need WMI/COM just for one integer.
int readBaseMhz() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &key) != ERROR_SUCCESS) {
        return SYSMON_NA;
    }
    DWORD value = 0, size = sizeof(value), type = 0;
    LONG rc = RegQueryValueExW(key, L"~MHz", nullptr, &type,
                               reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_DWORD || value == 0) return SYSMON_NA;
    return static_cast<int>(value);
}

// True for the "_Total" rollup instance. Instance names on a machine with more
// than one processor group look like "0,5", so a plain compare is not enough.
bool isTotalInstance(const wchar_t *instance) {
    if (!instance) return false;
    const wchar_t *tail = wcsrchr(instance, L',');
    const wchar_t *leaf = tail ? tail + 1 : instance;
    return _wcsicmp(leaf, L"_Total") == 0;
}

// Sort key for an instance: the logical processor index, or -1 for _Total. PDH
// returns instances in whatever order it likes, and "10" must not sort before
// "2" or the per-core bars end up scrambled.
int instanceIndex(const wchar_t *instance) {
    if (isTotalInstance(instance)) return -1;
    const wchar_t *tail = wcsrchr(instance, L',');
    const wchar_t *leaf = tail ? tail + 1 : instance;
    return _wtoi(leaf);
}

class PdhCounterArray {
public:
    bool add(PDH_HQUERY query, const wchar_t *path) {
        return PdhAddEnglishCounterW(query, path, 0, &m_counter) == ERROR_SUCCESS;
    }

    // Returns false until PDH has the two samples a rate counter needs.
    bool read(std::vector<std::pair<int, double>> &out) {
        out.clear();
        if (!m_counter) return false;

        DWORD size = 0, count = 0;
        PDH_STATUS rc = PdhGetFormattedCounterArrayW(m_counter, PDH_FMT_DOUBLE,
                                                     &size, &count, nullptr);
        if (rc != static_cast<PDH_STATUS>(PDH_MORE_DATA)) return false;

        m_buffer.resize(size);
        auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(m_buffer.data());
        if (PdhGetFormattedCounterArrayW(m_counter, PDH_FMT_DOUBLE, &size, &count,
                                         items) != ERROR_SUCCESS) {
            return false;
        }

        out.reserve(count);
        for (DWORD i = 0; i < count; i++) {
            if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) {
                continue;
            }
            out.emplace_back(instanceIndex(items[i].szName),
                             items[i].FmtValue.doubleValue);
        }
        return !out.empty();
    }

private:
    PDH_HCOUNTER m_counter = nullptr;
    std::vector<BYTE> m_buffer;
};

int clampPercent(double v) {
    if (v < 0.0) return 0;
    if (v > 100.0) return 100;
    return static_cast<int>(std::lround(v));
}

class CpuSensor final : public SensorSource {
public:
    ~CpuSensor() override {
        if (m_query) PdhCloseQuery(m_query);
    }

    bool init() override {
        if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS) return false;

        bool ok = m_usage.add(m_query, L"\\Processor Information(*)\\% Processor Time");
        // Performance is nice-to-have: without it frequency reads n/a, but usage
        // still works, so a failure here must not disable the whole source.
        m_havePerf = m_perf.add(m_query,
                                L"\\Processor Information(*)\\% Processor Performance");
        m_haveProcCount =
            PdhAddEnglishCounterW(m_query, L"\\System\\Processes", 0, &m_procCount) ==
            ERROR_SUCCESS;

        if (!ok) {
            PdhCloseQuery(m_query);
            m_query = nullptr;
            return false;
        }

        m_baseMhz = readBaseMhz();

        // Prime the counters. Rate counters have no value until the second
        // collect, so the first sample() would otherwise report zero usage.
        PdhCollectQueryData(m_query);
        m_available = true;
        return true;
    }

    void sample(SensorSnapshot &out) override {
        if (!m_available) return;
        if (PdhCollectQueryData(m_query) != ERROR_SUCCESS) return;

        std::vector<std::pair<int, double>> usage, perf;
        const bool haveUsage = m_usage.read(usage);
        const bool havePerf = m_havePerf && m_perf.read(perf);
        if (!haveUsage) return;

        // Widen to hold the highest core index actually reported, rather than
        // trusting the count: PDH can omit an instance for one interval.
        int maxIndex = -1;
        for (auto &[idx, _] : usage) maxIndex = (idx > maxIndex) ? idx : maxIndex;
        if (maxIndex < 0) return;
        if (maxIndex >= SYSMON_MAX_CORES) maxIndex = SYSMON_MAX_CORES - 1;

        out.cores.assign(static_cast<size_t>(maxIndex) + 1, CoreSample{});

        for (auto &[idx, value] : usage) {
            if (idx == -1) {
                out.cpuUsage = clampPercent(value);
            } else if (idx <= maxIndex) {
                out.cores[static_cast<size_t>(idx)].usage = clampPercent(value);
            }
        }

        if (havePerf && m_baseMhz != SYSMON_NA) {
            for (auto &[idx, value] : perf) {
                const int mhz = static_cast<int>(std::lround(m_baseMhz * value / 100.0));
                if (idx == -1) {
                    out.cpuFreqMhz = mhz;
                } else if (idx <= maxIndex) {
                    out.cores[static_cast<size_t>(idx)].freqMhz = mhz;
                }
            }
        }

        // Per-core temperature is deliberately left at NA. Desktop CPUs expose
        // package or per-CCD sensors, not one per core; inventing a per-core
        // number by copying the package value would look authoritative and be
        // fiction.

        if (m_haveProcCount) {
            PDH_FMT_COUNTERVALUE v{};
            if (PdhGetFormattedCounterValue(m_procCount, PDH_FMT_LONG, nullptr, &v) ==
                    ERROR_SUCCESS &&
                v.CStatus == PDH_CSTATUS_VALID_DATA) {
                out.processCount = static_cast<int>(v.longValue);
            }
        }

        out.uptimeSec = static_cast<int64_t>(GetTickCount64() / 1000ULL);
    }

    const char *name() const override { return "PDH/CPU"; }
    bool available() const override { return m_available; }

private:
    PDH_HQUERY m_query = nullptr;
    PdhCounterArray m_usage;
    PdhCounterArray m_perf;
    PDH_HCOUNTER m_procCount = nullptr;
    bool m_havePerf = false;
    bool m_haveProcCount = false;
    bool m_available = false;
    int m_baseMhz = SYSMON_NA;
};

}  // namespace

std::unique_ptr<SensorSource> makeCpuSource() {
    return std::make_unique<CpuSensor>();
}
