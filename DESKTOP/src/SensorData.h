// ---------------------------------------------------------------------------
//  SensorData.h -- one snapshot of everything the display can ask for.
//
//  Every field uses the wire encoding from PROTOCOL.md, so the encoder is a
//  straight serialisation with no unit conversion and nowhere for a factor-of-10
//  mistake to hide. SYSMON_NA (-1) means "no sensor for this on this machine".
// ---------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sysmon_wire.h"

struct CoreSample {
    int usage = SYSMON_NA;    // %
    int freqMhz = SYSMON_NA;  // MHz
    int tempC = SYSMON_NA;    // deg C, usually NA: silicon reports per-CCD
};

struct ProcessSample {
    std::string name;         // executable only, already sanitised for the wire
    int cpu = 0;              // %
    int memMib = 0;           // MiB working set
};

struct SensorSnapshot {
    // --- CPU ---
    int cpuUsage = SYSMON_NA;
    int cpuFreqMhz = SYSMON_NA;
    int cpuTempC = SYSMON_NA;
    int processCount = SYSMON_NA;
    int64_t uptimeSec = 0;
    std::vector<CoreSample> cores;

    // --- memory, MiB ---
    int memUsedMib = SYSMON_NA;
    int memTotalMib = SYSMON_NA;
    int memCachedMib = SYSMON_NA;
    int commitUsedMib = SYSMON_NA;
    int commitLimitMib = SYSMON_NA;

    // --- GPU ---
    int gpuUsage = SYSMON_NA;
    int gpuPowerDw = SYSMON_NA;   // tenths of a watt
    int gpuClockMhz = SYSMON_NA;
    int vramUsedMib = SYSMON_NA;
    int vramTotalMib = SYSMON_NA;
    int fps = SYSMON_NA;
    int gpuTempC = SYSMON_NA;

    // --- power, tenths of a watt ---
    int powerTotalDw = SYSMON_NA;
    int powerCpuDw = SYSMON_NA;
    int powerGpuDw = SYSMON_NA;
    int powerOtherDw = SYSMON_NA;

    // --- network ---
    int netRxKibs = SYSMON_NA;
    int netTxKibs = SYSMON_NA;
    int netRxTotalMib = 0;
    int netTxTotalMib = 0;
    int netLinkMbps = SYSMON_NA;

    // --- processes, already sorted by CPU descending ---
    std::vector<ProcessSample> processes;
};

// What a sensor source managed to provide, for the Settings window and the log.
// Users need to know *why* a value reads n/a, not just that it does.
struct SourceStatus {
    bool pdh = false;          // always expected to work
    bool nvml = false;         // NVIDIA GPU telemetry
    bool afterburner = false;  // MSI Afterburner shared memory: CPU temp/power
    bool rtss = false;         // RivaTuner shared memory: framerate
    std::string note;          // human-readable summary of what is missing
};
