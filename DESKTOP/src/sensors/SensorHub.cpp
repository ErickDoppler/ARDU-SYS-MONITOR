// ---------------------------------------------------------------------------
//  SensorHub -- owns every source, samples them in order, derives the totals.
//
//  Order matters. NVML runs before the Afterburner bridge so that GPU power comes
//  from the driver rather than from a third-party app when both can supply it,
//  and the power totals are computed last, once every contributor has reported.
// ---------------------------------------------------------------------------
#include "Sensors.h"

#include <string>

SensorHub::SensorHub() = default;
SensorHub::~SensorHub() = default;

void SensorHub::init() {
    // Registration order is sampling order.
    m_sources.push_back(makeCpuSource());
    m_sources.push_back(makeMemorySource());
    m_sources.push_back(makeNetworkSource());
    m_sources.push_back(makeProcessSource());
    m_sources.push_back(makeNvmlSource());
    m_sources.push_back(makeAfterburnerSource());
    m_sources.push_back(makeRtssSource());

    for (auto &s : m_sources) {
        if (s) s->init();
    }

    // A source that failed init can still come good later -- Afterburner and RTSS
    // retry internally -- so this is a snapshot of startup, refreshed on sample().
    refreshStatus();
}

void SensorHub::refreshStatus() {
    m_status = SourceStatus{};
    for (auto &s : m_sources) {
        if (!s) continue;
        const std::string n = s->name();
        const bool ok = s->available();
        if (n.rfind("PDH", 0) == 0 || n.rfind("Win32", 0) == 0) {
            // Any of the always-available sources working means PDH-class data is
            // fine; they are only marked false if all of them failed.
            m_status.pdh = m_status.pdh || ok;
        } else if (n.rfind("NVML", 0) == 0) {
            m_status.nvml = ok;
        } else if (n.rfind("Afterburner", 0) == 0) {
            m_status.afterburner = ok;
        } else if (n.rfind("RTSS", 0) == 0) {
            m_status.rtss = ok;
        }
    }

    std::string note;
    if (!m_status.nvml) note += "No NVIDIA GPU telemetry (nvml.dll). ";
    if (!m_status.afterburner) {
        note += "MSI Afterburner not running: CPU temperature and CPU power "
                "are unavailable (they need ring-0 access on this CPU). ";
    }
    if (!m_status.rtss) note += "RivaTuner not running: no framerate. ";
    if (note.empty()) note = "All sensor sources available.";
    m_status.note = note;
}

SensorSnapshot SensorHub::sample() {
    SensorSnapshot snap;

    for (auto &s : m_sources) {
        if (s) s->sample(snap);
    }

    // --- derived power ----------------------------------------------------
    // Sum only the rails that actually reported. A missing CPU figure must not
    // silently read as 0 W and make the total look like the GPU is the whole
    // machine.
    int total = 0;
    bool any = false;
    if (snap.powerCpuDw != SYSMON_NA) { total += snap.powerCpuDw; any = true; }
    if (snap.powerGpuDw != SYSMON_NA) { total += snap.powerGpuDw; any = true; }
    if (snap.powerOtherDw != SYSMON_NA) { total += snap.powerOtherDw; any = true; }
    snap.powerTotalDw = any ? total : SYSMON_NA;

    refreshStatus();
    return snap;
}
