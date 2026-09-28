// ---------------------------------------------------------------------------
//  AfterburnerBridge -- CPU temperature and CPU package power, borrowed from
//  MSI Afterburner's shared memory.
//
//  Why this exists rather than reading the hardware directly: on AMD Ryzen,
//  package temperature and power come from the SMU via MSR reads, which require
//  ring 0. Any user-mode app that shows them -- Afterburner, HWiNFO,
//  LibreHardwareMonitor -- does it through its own signed kernel driver. Adding a
//  second driver to the machine for two numbers is a bad trade, so if Afterburner
//  is already running we read what its driver already collected.
//
//  Consequences, stated plainly: with Afterburner closed, CPU temperature and CPU
//  power report n/a and every other metric is unaffected. Nothing here is
//  fabricated to fill the gap.
//
//  The mapping is read-only and never written. Layout is validated against the
//  header's own dwEntrySize before any entry is touched, so a future Afterburner
//  that changes the struct makes this source go unavailable instead of
//  misinterpreting bytes.
// ---------------------------------------------------------------------------
#include <windows.h>

#include <cstring>
#include <string>

#include "Sensors.h"

namespace {

#define MAHM_MAX_PATH 260

struct MahmHeader {
    DWORD dwSignature;  // 'MAHM', byte order differs between builds
    DWORD dwVersion;
    DWORD dwHeaderSize;
    DWORD dwNumEntries;
    DWORD dwEntrySize;
    DWORD time;
    DWORD dwNumGpuEntries;
    DWORD dwGpuEntrySize;
};

struct MahmEntry {
    char szSrcName[MAHM_MAX_PATH];
    char szSrcUnits[MAHM_MAX_PATH];
    char szLocalizedSrcName[MAHM_MAX_PATH];
    char szLocalizedSrcUnits[MAHM_MAX_PATH];
    char szRecommendedFormat[MAHM_MAX_PATH];
    float data;
    float minLimit;
    float maxLimit;
    DWORD dwFlags;
    DWORD dwGpu;
    DWORD dwSrcId;
};

bool containsNoCase(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    const size_t nlen = std::strlen(needle);
    if (nlen == 0) return true;
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               (std::tolower(static_cast<unsigned char>(p[i])) ==
                std::tolower(static_cast<unsigned char>(needle[i])))) {
            ++i;
        }
        if (i == nlen) return true;
    }
    return false;
}

// Afterburner reports an unpopulated sensor as a large negative sentinel.
bool plausible(float v) { return v > -270.0f && v < 100000.0f; }

class AfterburnerBridge final : public SensorSource {
public:
    ~AfterburnerBridge() override { close(); }

    bool init() override {
        // Not fatal if absent -- Afterburner may be started later, so sample()
        // retries on a timer.
        openMapping();
        return m_available;
    }

    void sample(SensorSnapshot &out) override {
        if (!m_available) {
            // Cheap periodic retry so starting Afterburner mid-session works
            // without restarting this app.
            if (++m_retryTick < 10) return;
            m_retryTick = 0;
            openMapping();
            if (!m_available) return;
        }

        auto *header = static_cast<const MahmHeader *>(m_view);
        if (!header || header->dwNumEntries == 0) return;

        // Afterburner rewrites the block in place; a torn read shows up as a
        // wild value, which `plausible` rejects.
        const auto *base = reinterpret_cast<const BYTE *>(m_view) + header->dwHeaderSize;
        const DWORD stride = header->dwEntrySize;
        if (stride != sizeof(MahmEntry)) {
            // Layout changed under us: stop trusting it.
            close();
            return;
        }

        for (DWORD i = 0; i < header->dwNumEntries; i++) {
            const auto *e = reinterpret_cast<const MahmEntry *>(base + i * stride);
            if (!plausible(e->data)) continue;

            const char *n = e->szSrcName;
            const bool isGpu = containsNoCase(n, "GPU");
            const bool isCpu = containsNoCase(n, "CPU") && !isGpu;

            if (isCpu && containsNoCase(n, "temp")) {
                // Prefer the plain package sensor over "CPU1 temperature" style
                // per-core entries, which appear first on some boards.
                const int t = static_cast<int>(e->data + 0.5f);
                if (out.cpuTempC == SYSMON_NA || t > out.cpuTempC) out.cpuTempC = t;
            } else if (isCpu && containsNoCase(n, "power")) {
                out.powerCpuDw = static_cast<int>(e->data * 10.0f + 0.5f);
            } else if (isGpu && containsNoCase(n, "power") && out.powerGpuDw == SYSMON_NA) {
                // Only if NVML did not already supply it; NVML is the better
                // source because it does not depend on a third-party app.
                out.powerGpuDw = static_cast<int>(e->data * 10.0f + 0.5f);
            }
        }
    }

    const char *name() const override { return "Afterburner/SharedMem"; }
    bool available() const override { return m_available; }

private:
    void openMapping() {
        close();
        m_map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"MAHMSharedMemory");
        if (!m_map) return;

        m_view = MapViewOfFile(m_map, FILE_MAP_READ, 0, 0, 0);
        if (!m_view) {
            close();
            return;
        }

        auto *header = static_cast<const MahmHeader *>(m_view);
        // 'MAHM' lands in a DWORD differently depending on how Afterburner was
        // built, so accept either order rather than guessing.
        const bool sigOk = header->dwSignature == 0x4D48414D ||
                           header->dwSignature == 0x4D41484D;
        const bool sane = sigOk && header->dwHeaderSize >= sizeof(MahmHeader) &&
                          header->dwEntrySize == sizeof(MahmEntry) &&
                          header->dwNumEntries > 0 && header->dwNumEntries < 4096;
        if (!sane) {
            close();
            return;
        }
        m_available = true;
    }

    void close() {
        if (m_view) UnmapViewOfFile(m_view);
        if (m_map) CloseHandle(m_map);
        m_view = nullptr;
        m_map = nullptr;
        m_available = false;
    }

    HANDLE m_map = nullptr;
    const void *m_view = nullptr;
    bool m_available = false;
    int m_retryTick = 0;
};

}  // namespace

std::unique_ptr<SensorSource> makeAfterburnerSource() {
    return std::make_unique<AfterburnerBridge>();
}
