// ---------------------------------------------------------------------------
//  RtssBridge -- framerate, from RivaTuner Statistics Server's shared memory.
//
//  Framerate is not a system property: it belongs to whichever process is
//  presenting frames. Measuring it from the outside means tracing Present calls
//  through ETW, which is what PresentMon does in several thousand lines. RTSS
//  already hooks every 3D application and publishes per-process frame timing, and
//  it is running on this machine alongside Afterburner, so this reads that.
//
//  With nothing rendering, fps reports n/a -- which is the honest answer, and why
//  the protocol distinguishes n/a from 0. A desktop at idle has no framerate, it
//  is not running at 0 fps.
//
//  Picks the most recently updated entry, so the frontmost game wins when RTSS is
//  tracking several processes.
// ---------------------------------------------------------------------------
#include <windows.h>

#include "Sensors.h"

namespace {

#define RTSS_MAX_PATH 260

struct RtssHeader {
    DWORD dwSignature;  // 'RTSS'
    DWORD dwVersion;
    DWORD dwAppEntrySize;
    DWORD dwAppArrOffset;
    DWORD dwAppArrSize;
    DWORD dwOSDEntrySize;
    DWORD dwOSDArrOffset;
    DWORD dwOSDArrSize;
    DWORD dwOSDFrame;
};

// Only the prefix that matters here. Entries are larger and grow between RTSS
// versions, which is why dwAppEntrySize from the header is used as the stride
// instead of sizeof() -- reading a fixed-size array would misalign on an update.
struct RtssAppEntry {
    DWORD dwProcessID;
    char szName[RTSS_MAX_PATH];
    DWORD dwFlags;
    DWORD dwTime0;
    DWORD dwTime1;
    DWORD dwFrames;
    DWORD dwFrameTime;  // microseconds
};

class RtssBridge final : public SensorSource {
public:
    ~RtssBridge() override { close(); }

    bool init() override {
        openMapping();
        return m_available;
    }

    void sample(SensorSnapshot &out) override {
        if (!m_available) {
            if (++m_retryTick < 10) return;
            m_retryTick = 0;
            openMapping();
            if (!m_available) return;
        }

        auto *header = static_cast<const RtssHeader *>(m_view);
        if (!header) return;
        if (header->dwAppArrSize == 0 || header->dwAppEntrySize < sizeof(RtssAppEntry)) return;

        const auto *base = reinterpret_cast<const BYTE *>(m_view) + header->dwAppArrOffset;

        const RtssAppEntry *best = nullptr;
        for (DWORD i = 0; i < header->dwAppArrSize; i++) {
            const auto *e =
                reinterpret_cast<const RtssAppEntry *>(base + i * header->dwAppEntrySize);
            if (e->dwProcessID == 0) continue;     // free slot
            if (e->dwFrameTime == 0) continue;     // hooked but not presenting
            if (!best || e->dwTime1 > best->dwTime1) best = e;
        }

        if (!best) {
            out.fps = SYSMON_NA;
            return;
        }

        // dwFrameTime is the average frame time in microseconds over RTSS's own
        // window, so this is already smoothed.
        const double fps = 1000000.0 / static_cast<double>(best->dwFrameTime);
        out.fps = (fps > 0.0 && fps < 10000.0) ? static_cast<int>(fps + 0.5) : SYSMON_NA;
    }

    const char *name() const override { return "RTSS/SharedMem"; }
    bool available() const override { return m_available; }

private:
    void openMapping() {
        close();
        m_map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"RTSSSharedMemoryV2");
        if (!m_map) return;

        m_view = MapViewOfFile(m_map, FILE_MAP_READ, 0, 0, 0);
        if (!m_view) {
            close();
            return;
        }

        auto *header = static_cast<const RtssHeader *>(m_view);
        const bool sigOk = header->dwSignature == 0x53535452 ||  // 'RTSS'
                           header->dwSignature == 0x52545353;
        if (!sigOk || header->dwAppEntrySize == 0 || header->dwAppArrSize > 4096) {
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

std::unique_ptr<SensorSource> makeRtssSource() {
    return std::make_unique<RtssBridge>();
}
