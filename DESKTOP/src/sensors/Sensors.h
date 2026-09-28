// ---------------------------------------------------------------------------
//  Sensors.h -- the collectors behind a SensorSnapshot.
//
//  Split by data source rather than by screen, because availability is a
//  property of the source. On this machine, for instance:
//
//    PDH          always present    CPU total + per core, memory, network,
//                                   per-process CPU and working set
//    NVML         nvml.dll present  GPU usage, clock, power, VRAM, temperature
//    Afterburner  if running        CPU temperature and CPU package power
//    RTSS         if running        framerate
//
//  The Afterburner dependency is not laziness. On AMD Ryzen, package temperature
//  and power live behind SMU/MSR reads that need ring 0, which means shipping a
//  signed kernel driver -- which is what LibreHardwareMonitor and Afterburner
//  actually do. Reading Afterburner's shared memory borrows its driver instead
//  of adding another one to the machine. With Afterburner closed, those two
//  values report n/a and everything else keeps working.
// ---------------------------------------------------------------------------
#pragma once

#include <memory>
#include <string>

#include "../SensorData.h"

// Base for every source. Collectors are stateful: rate-based counters need a
// previous reading, so sample() must be called on a steady interval.
class SensorSource {
public:
    virtual ~SensorSource() = default;
    virtual bool init() = 0;
    virtual void sample(SensorSnapshot &out) = 0;
    virtual const char *name() const = 0;
    virtual bool available() const = 0;
};

std::unique_ptr<SensorSource> makeCpuSource();          // PDH + registry
std::unique_ptr<SensorSource> makeMemorySource();       // GlobalMemoryStatusEx
std::unique_ptr<SensorSource> makeNetworkSource();      // GetIfTable2
std::unique_ptr<SensorSource> makeProcessSource();      // toolhelp + times
std::unique_ptr<SensorSource> makeNvmlSource();         // nvml.dll
std::unique_ptr<SensorSource> makeAfterburnerSource();  // MAHMSharedMemory
std::unique_ptr<SensorSource> makeRtssSource();         // RTSSSharedMemoryV2

// Owns every source, samples them in dependency order and fills in the derived
// power totals once the others have reported.
class SensorHub {
public:
    SensorHub();
    ~SensorHub();

    void init();
    SensorSnapshot sample();
    SourceStatus status() const { return m_status; }

private:
    void refreshStatus();

    std::vector<std::unique_ptr<SensorSource>> m_sources;
    SourceStatus m_status;
};
