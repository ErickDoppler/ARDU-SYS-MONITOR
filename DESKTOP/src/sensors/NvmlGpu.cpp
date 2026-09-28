// ---------------------------------------------------------------------------
//  NvmlGpu -- GPU telemetry from NVIDIA's NVML.
//
//  nvml.dll ships with the display driver and lives in System32, so it is loaded
//  by name at runtime and the handful of entry points needed are declared here.
//  That avoids a CUDA SDK dependency for six functions, and means a machine with
//  no NVIDIA card simply reports the source unavailable instead of failing to
//  link or refusing to start.
//
//  The ABI below is NVML's stable C interface. The _v2 suffixes are the current
//  symbol names; the unsuffixed ones still exist as compatibility aliases, so
//  each is tried in turn.
//
//  Device 0 only. A machine can have several adapters -- this one reports three,
//  two of them virtual monitors -- but NVML enumerates real NVIDIA GPUs only, so
//  index 0 is the physical card.
// ---------------------------------------------------------------------------
#include <windows.h>

#include "Sensors.h"

namespace {

// --- NVML ABI, declared locally -------------------------------------------
using nvmlDevice_t = void *;

struct nvmlUtilization_t {
    unsigned int gpu;     // % of the sample period with any kernel running
    unsigned int memory;  // % of the period the memory bus was busy
};

struct nvmlMemory_t {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

enum { NVML_TEMPERATURE_GPU = 0 };
enum { NVML_CLOCK_GRAPHICS = 0 };
enum { NVML_SUCCESS = 0 };

using PFN_nvmlInit = int (*)();
using PFN_nvmlShutdown = int (*)();
using PFN_nvmlDeviceGetHandleByIndex = int (*)(unsigned int, nvmlDevice_t *);
using PFN_nvmlDeviceGetUtilizationRates = int (*)(nvmlDevice_t, nvmlUtilization_t *);
using PFN_nvmlDeviceGetTemperature = int (*)(nvmlDevice_t, int, unsigned int *);
using PFN_nvmlDeviceGetPowerUsage = int (*)(nvmlDevice_t, unsigned int *);
using PFN_nvmlDeviceGetClockInfo = int (*)(nvmlDevice_t, int, unsigned int *);
using PFN_nvmlDeviceGetMemoryInfo = int (*)(nvmlDevice_t, nvmlMemory_t *);

class NvmlGpu final : public SensorSource {
public:
    ~NvmlGpu() override {
        if (m_shutdown) m_shutdown();
        if (m_dll) FreeLibrary(m_dll);
    }

    bool init() override {
        m_dll = LoadLibraryW(L"nvml.dll");
        if (!m_dll) {
            // Older drivers only installed it under Program Files.
            m_dll = LoadLibraryW(
                L"C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
        }
        if (!m_dll) return false;

        m_init = resolve<PFN_nvmlInit>("nvmlInit_v2", "nvmlInit");
        m_shutdown = resolve<PFN_nvmlShutdown>("nvmlShutdown", nullptr);
        m_getHandle = resolve<PFN_nvmlDeviceGetHandleByIndex>(
            "nvmlDeviceGetHandleByIndex_v2", "nvmlDeviceGetHandleByIndex");
        m_getUtil = resolve<PFN_nvmlDeviceGetUtilizationRates>(
            "nvmlDeviceGetUtilizationRates", nullptr);
        m_getTemp = resolve<PFN_nvmlDeviceGetTemperature>("nvmlDeviceGetTemperature", nullptr);
        m_getPower = resolve<PFN_nvmlDeviceGetPowerUsage>("nvmlDeviceGetPowerUsage", nullptr);
        m_getClock = resolve<PFN_nvmlDeviceGetClockInfo>("nvmlDeviceGetClockInfo", nullptr);
        // v1 deliberately, with no _v2 attempt. nvmlDeviceGetMemoryInfo_v2 takes a
        // different struct -- nvmlMemory_v2_t, which is larger and carries a
        // version field the caller must fill in -- so calling it with the v1
        // layout below fails the version check and VRAM silently reads n/a.
        m_getMem = resolve<PFN_nvmlDeviceGetMemoryInfo>("nvmlDeviceGetMemoryInfo", nullptr);

        if (!m_init || !m_getHandle) return false;
        if (m_init() != NVML_SUCCESS) return false;
        if (m_getHandle(0, &m_device) != NVML_SUCCESS || !m_device) return false;

        m_available = true;
        return true;
    }

    void sample(SensorSnapshot &out) override {
        if (!m_available) return;

        if (m_getUtil) {
            nvmlUtilization_t util{};
            if (m_getUtil(m_device, &util) == NVML_SUCCESS) {
                out.gpuUsage = static_cast<int>(util.gpu > 100 ? 100 : util.gpu);
            }
        }
        if (m_getTemp) {
            unsigned int t = 0;
            if (m_getTemp(m_device, NVML_TEMPERATURE_GPU, &t) == NVML_SUCCESS) {
                out.gpuTempC = static_cast<int>(t);
            }
        }
        if (m_getPower) {
            unsigned int mw = 0;
            if (m_getPower(m_device, &mw) == NVML_SUCCESS) {
                // milliwatts -> tenths of a watt
                out.gpuPowerDw = static_cast<int>(mw / 100);
                out.powerGpuDw = out.gpuPowerDw;
            }
        }
        if (m_getClock) {
            unsigned int mhz = 0;
            if (m_getClock(m_device, NVML_CLOCK_GRAPHICS, &mhz) == NVML_SUCCESS) {
                out.gpuClockMhz = static_cast<int>(mhz);
            }
        }
        if (m_getMem) {
            nvmlMemory_t mem{};
            if (m_getMem(m_device, &mem) == NVML_SUCCESS && mem.total > 0) {
                out.vramUsedMib = static_cast<int>(mem.used >> 20);
                out.vramTotalMib = static_cast<int>(mem.total >> 20);
            }
        }
    }

    const char *name() const override { return "NVML/GPU"; }
    bool available() const override { return m_available; }

private:
    template <typename T>
    T resolve(const char *primary, const char *fallback) {
        // Via void*: casting FARPROC straight to a typed function pointer is a
        // -Wcast-function-type warning, and the two-step is the documented way
        // to express "I know what this symbol really is".
        auto raw = reinterpret_cast<void *>(GetProcAddress(m_dll, primary));
        if (!raw && fallback) raw = reinterpret_cast<void *>(GetProcAddress(m_dll, fallback));
        auto fn = reinterpret_cast<T>(raw);
        return fn;
    }

    HMODULE m_dll = nullptr;
    nvmlDevice_t m_device = nullptr;
    bool m_available = false;

    PFN_nvmlInit m_init = nullptr;
    PFN_nvmlShutdown m_shutdown = nullptr;
    PFN_nvmlDeviceGetHandleByIndex m_getHandle = nullptr;
    PFN_nvmlDeviceGetUtilizationRates m_getUtil = nullptr;
    PFN_nvmlDeviceGetTemperature m_getTemp = nullptr;
    PFN_nvmlDeviceGetPowerUsage m_getPower = nullptr;
    PFN_nvmlDeviceGetClockInfo m_getClock = nullptr;
    PFN_nvmlDeviceGetMemoryInfo m_getMem = nullptr;
};

}  // namespace

std::unique_ptr<SensorSource> makeNvmlSource() {
    return std::make_unique<NvmlGpu>();
}
