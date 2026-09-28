// ---------------------------------------------------------------------------
//  MemorySensor -- physical and commit figures via GetPerformanceInfo.
//
//  GetPerformanceInfo gives everything needed in one call and in pages, so there
//  is no PDH query to keep alive and nothing to prime. GlobalMemoryStatusEx
//  would cover physical memory but not the system cache.
//
//  "Cached" here is SystemCache, which is what Task Manager labels Cached. It
//  overlaps standby memory and is not additive with "used" -- so the display
//  shows it as its own figure rather than as a slice of a stacked bar.
// ---------------------------------------------------------------------------
#include <windows.h>
#include <psapi.h>

#include "Sensors.h"

namespace {

class MemorySensor final : public SensorSource {
public:
    bool init() override {
        PERFORMANCE_INFORMATION pi{};
        pi.cb = sizeof(pi);
        m_available = GetPerformanceInfo(&pi, sizeof(pi)) != FALSE;
        return m_available;
    }

    void sample(SensorSnapshot &out) override {
        if (!m_available) return;

        PERFORMANCE_INFORMATION pi{};
        pi.cb = sizeof(pi);
        if (!GetPerformanceInfo(&pi, sizeof(pi))) return;

        const SIZE_T pageSize = pi.PageSize ? pi.PageSize : 4096;
        const auto toMib = [pageSize](SIZE_T pages) -> int {
            return static_cast<int>((static_cast<uint64_t>(pages) * pageSize) >> 20);
        };

        const SIZE_T usedPages =
            (pi.PhysicalTotal > pi.PhysicalAvailable) ? pi.PhysicalTotal - pi.PhysicalAvailable
                                                      : 0;

        out.memTotalMib = toMib(pi.PhysicalTotal);
        out.memUsedMib = toMib(usedPages);
        out.memCachedMib = toMib(pi.SystemCache);
        out.commitUsedMib = toMib(pi.CommitTotal);
        out.commitLimitMib = toMib(pi.CommitLimit);
    }

    const char *name() const override { return "Win32/Memory"; }
    bool available() const override { return m_available; }

private:
    bool m_available = false;
};

}  // namespace

std::unique_ptr<SensorSource> makeMemorySource() {
    return std::make_unique<MemorySensor>();
}
