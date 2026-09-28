#include "screens.h"

#include "link.h"

#include <stdio.h>
#include <string.h>

namespace screens {
namespace {

// --- history --------------------------------------------------------------
Graph gCpu, gMem, gGpu, gGpuClock, gPower, gNetRx, gNetTx;

// Diagnostics timing, collected continuously so the screen is useful the moment
// it opens rather than starting blank.
Graph gDiag;
uint32_t maxDiagLen = 1;
uint32_t paintedMaxDiag = 0xFFFFFFFFul;

// Frequency and throughput have no natural 0..100 range, so each graph carrying
// one keeps the largest value it has seen and scales against that. A fixed
// ceiling would either clip a boosting CPU or squash an idle one flat.
// 32-bit for the same reason the fields are: a 1 Gbit link peaks around 122070
// KiB/s, well past what a uint16_t running maximum could hold.
uint32_t maxGpuClock = 1;
uint32_t maxNetRx = 1;
uint32_t maxNetTx = 1;
uint32_t maxPower = 1;

// Bumped whenever a graph gains a sample, so drawLive knows to repaint the plot
// without comparing 300 bytes of ring buffer.
uint16_t graphEpoch = 0;

// What is currently on the panel. Comparing against this is what keeps a refresh
// down to a few hundred pixels.
SysData painted;
uint16_t paintedEpoch = 0xFFFF;
bool paintedLink = false;
bool paintedStale = false;
bool linkKnown = false;

// Last peaks the auto-scaled axes were drawn with. Those axes only change when a
// new maximum appears, so they are change-detected like any other field rather
// than repainted on every tick.
uint32_t paintedMaxPower = 0xFFFFFFFFul;
uint32_t paintedMaxRx = 0xFFFFFFFFul;
uint32_t paintedMaxTx = 0xFFFFFFFFul;

const int16_t kTop = 16;

uint8_t scaleTo100(int32_t value, uint32_t &runningMax) {
    if (value < 0) return GRAPH_NONE;
    if ((uint32_t)value > runningMax) runningMax = (uint32_t)value;
    if (runningMax == 0) return 0;
    int32_t pct = value * 100L / (int32_t)runningMax;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

uint8_t pctOrNone(int v) {
    if (v == SYSMON_NA) return GRAPH_NONE;
    if (v < 0) return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}

int memPercent(const SysData &d) {
    if (d.memUsed == SYSMON_NA || d.memTotal <= 0) return SYSMON_NA;
    return (int)((long)d.memUsed * 100L / d.memTotal);
}

int vramPercent(const SysData &d) {
    if (d.vramUsed == SYSMON_NA || d.vramTotal <= 0) return SYSMON_NA;
    return (int)((long)d.vramUsed * 100L / d.vramTotal);
}

// --- shared pieces --------------------------------------------------------

void tempText(char *buf, uint8_t len, int tempC) {
    if (tempC == SYSMON_NA) {
        strncpy(buf, "n/a", len);
        buf[len - 1] = '\0';
    } else {
        snprintf(buf, len, "%dC", tempC);
    }
}

void uptimeText(char *buf, uint8_t len, long seconds) {
    if (seconds <= 0) {
        strncpy(buf, "n/a", len);
        buf[len - 1] = '\0';
        return;
    }
    snprintf(buf, len, "%ldh %02ldm", seconds / 3600, (seconds % 3600) / 60);
}

void pairText(char *buf, uint8_t len, int32_t a, int32_t b) {
    if (a == SYSMON_NA || b == SYSMON_NA) {
        strncpy(buf, "n/a", len);
        buf[len - 1] = '\0';
        return;
    }
    char x[10], y[10];
    ui::formatMib(x, sizeof(x), a);
    ui::formatMib(y, sizeof(y), b);
    snprintf(buf, len, "%s/%s", x, y);
}

// ===========================================================================
//  D1  CPU
// ===========================================================================
void staticCpu() {
    ui::box(4, kTop + 4, 312, 78, F("cpu"));
    ui::label(170, kTop + 18, F("freq"));
    ui::label(170, kTop + 30, F("temp"));
    ui::label(170, kTop + 42, F("procs"));
    ui::label(170, kTop + 54, F("uptime"));
    ui::box(4, kTop + 90, 312, 130, F("history"));
    // Fixed 0-100% scale, so the axis is genuinely static and belongs here.
    ui::graphAxes(8, kTop + 96, 302, 118, 100, "%");
}

void liveCpu(const SysData &d, bool force) {
    char buf[16];

    if (force || painted.cpuUsage != d.cpuUsage) {
        ui::formatValue(buf, sizeof(buf), d.cpuUsage, "");
        tft.fillRect(14, kTop + 16, 116, 40, C_BG);
        ui::bigNumber(14, kTop + 16, buf, heatColor(d.cpuUsage), "%", 5);
        ui::meter(14, kTop + 62, 140, 12, d.cpuUsage);
    }
    if (force || painted.cpuFreq != d.cpuFreq) {
        ui::formatValue(buf, sizeof(buf), d.cpuFreq, " MHz");
        ui::valueField(306, kTop + 18, 96, buf, C_TEXT);
    }
    if (force || painted.cpuTemp != d.cpuTemp) {
        tempText(buf, sizeof(buf), d.cpuTemp);
        ui::valueField(306, kTop + 30, 96, buf, tempColor(d.cpuTemp));
    }
    if (force || painted.procCount != d.procCount) {
        ui::formatValue(buf, sizeof(buf), d.procCount, "");
        ui::valueField(306, kTop + 42, 96, buf, C_TEXT);
    }
    if (force || painted.uptime / 60 != d.uptime / 60) {
        uptimeText(buf, sizeof(buf), d.uptime);
        ui::valueField(306, kTop + 54, 96, buf, C_DIM);
    }
    if (force || paintedEpoch != graphEpoch) {
        ui::graphPlot(8, kTop + 96, 302, 118, gCpu, 0, 20);
    }
}

// ===========================================================================
//  D2  per core
// ===========================================================================
uint8_t coreRows(uint8_t n) { return (uint8_t)((n + 1) / 2); }

void staticCores() { ui::box(4, kTop + 4, 312, 220, F("cores")); }

void liveCores(const SysData &d, bool force) {
    uint8_t n = d.coreCount ? d.coreCount : d.announcedCores;
    if (n > SYSMON_MAX_CORES) n = SYSMON_MAX_CORES;

    if (n == 0) {
        if (force) ui::label(14, kTop + 20, F("waiting for core data..."), C_DIM);
        return;
    }

    const uint8_t rows = coreRows(n);
    const int16_t colW = 150;
    const int16_t rowH = (int16_t)(206 / rows);
    const int16_t barW = 62;

    char buf[10];

    for (uint8_t i = 0; i < n; i++) {
        const bool changed = force || painted.coreUsage[i] != d.coreUsage[i] ||
                             painted.coreFreq[i] != d.coreFreq[i];
        if (!changed) continue;

        const uint8_t col = (uint8_t)(i / rows);
        const uint8_t row = (uint8_t)(i % rows);
        const int16_t x = 10 + col * colW;
        const int16_t y = kTop + 14 + row * rowH;
        if (y + 8 > 236) continue;

        if (force) {
            snprintf(buf, sizeof(buf), "%02u", (unsigned)i);
            tft.setTextSize(1);
            tft.setTextColor(C_DIM, C_BG);
            tft.printAt(x, y, buf);
        }

        const int usage = d.coreUsage[i];
        ui::meter(x + 16, y - 1, barW, 9, usage);

        snprintf(buf, sizeof(buf), "%d%%", usage < 0 ? 0 : usage);
        ui::valueField(x + 16 + barW + 26, y, 26, buf, heatColor(usage));

        if (d.coreFreq[i] > 0) {
            snprintf(buf, sizeof(buf), "%d", d.coreFreq[i]);
            ui::valueField(x + 16 + barW + 62, y, 32, buf, C_DIM);
        }
    }
}

// ===========================================================================
//  D3  memory
// ===========================================================================
void staticMem() {
    ui::box(4, kTop + 4, 312, 78, F("memory"));
    ui::label(170, kTop + 18, F("used"));
    ui::label(170, kTop + 30, F("total"));
    ui::label(170, kTop + 42, F("cached"));
    ui::label(170, kTop + 54, F("commit"));
    ui::box(4, kTop + 90, 312, 130, F("history"));
    ui::graphAxes(8, kTop + 96, 302, 118, 100, "%");
}

void liveMem(const SysData &d, bool force) {
    char buf[16];
    const int pct = memPercent(d);

    if (force || memPercent(painted) != pct) {
        ui::formatValue(buf, sizeof(buf), pct, "");
        tft.fillRect(14, kTop + 16, 116, 40, C_BG);
        ui::bigNumber(14, kTop + 16, buf, heatColor(pct), "%", 5);
        ui::meter(14, kTop + 62, 140, 12, pct);
    }
    if (force || painted.memUsed != d.memUsed) {
        ui::formatMib(buf, sizeof(buf), d.memUsed);
        ui::valueField(306, kTop + 18, 96, buf, C_MEM);
    }
    if (force || painted.memTotal != d.memTotal) {
        ui::formatMib(buf, sizeof(buf), d.memTotal);
        ui::valueField(306, kTop + 30, 96, buf, C_TEXT);
    }
    if (force || painted.memCached != d.memCached) {
        ui::formatMib(buf, sizeof(buf), d.memCached);
        ui::valueField(306, kTop + 42, 96, buf, C_DIM);
    }
    if (force || painted.commitUsed != d.commitUsed ||
        painted.commitLimit != d.commitLimit) {
        pairText(buf, sizeof(buf), d.commitUsed, d.commitLimit);
        ui::valueField(306, kTop + 54, 96, buf, C_DIM);
    }
    if (force || paintedEpoch != graphEpoch) {
        ui::graphPlot(8, kTop + 96, 302, 118, gMem, C_MEM, 20);
    }
}

// ===========================================================================
//  D4  GPU
// ===========================================================================
const int16_t kStripY = kTop + 158;
const int16_t kStripW = 236;

void staticGpu() {
    ui::box(4, kTop + 4, 312, 148, F("gpu"));
    ui::label(170, kTop + 18, F("clock"));
    ui::label(170, kTop + 32, F("temp"));
    ui::label(170, kTop + 46, F("vram"));
    ui::label(170, kTop + 76, F("framerate"));
    ui::box(4, kStripY - 8, 312, 62, F("usage / clock"));
}

void liveGpu(const SysData &d, bool force) {
    char buf[16];

    if (force || painted.gpuUsage != d.gpuUsage) {
        ui::formatValue(buf, sizeof(buf), d.gpuUsage, "");
        tft.fillRect(14, kTop + 16, 116, 40, C_BG);
        ui::bigNumber(14, kTop + 16, buf, heatColor(d.gpuUsage), "%", 5);
    }
    if (force || painted.gpuPower != d.gpuPower) {
        ui::formatTenths(buf, sizeof(buf), d.gpuPower, "");
        tft.fillRect(14, kTop + 66, 120, 24, C_BG);
        ui::bigNumber(14, kTop + 66, buf, C_PWR, "W", 3);
    }
    if (force || painted.gpuClock != d.gpuClock) {
        ui::formatValue(buf, sizeof(buf), d.gpuClock, " MHz");
        ui::valueField(306, kTop + 18, 96, buf, C_GPU_FREQ);
    }
    if (force || painted.gpuTemp != d.gpuTemp) {
        tempText(buf, sizeof(buf), d.gpuTemp);
        ui::valueField(306, kTop + 32, 96, buf, tempColor(d.gpuTemp));
    }
    if (force || painted.vramUsed != d.vramUsed || painted.vramTotal != d.vramTotal) {
        pairText(buf, sizeof(buf), d.vramUsed, d.vramTotal);
        ui::valueField(306, kTop + 46, 96, buf, C_TEXT);
        ui::meter(170, kTop + 58, 136, 9, vramPercent(d));
    }
    if (force || painted.fps != d.fps) {
        // Framerate only means something while something is presenting frames, so
        // an absent value says "idle" rather than showing 0 fps.
        tft.fillRect(230, kTop + 70, 80, 18, C_BG);
        if (d.fps == SYSMON_NA) {
            ui::valueRight(306, kTop + 76, "idle", C_DIM, 1);
        } else {
            ui::formatValue(buf, sizeof(buf), d.fps, "");
            ui::valueRight(288, kTop + 72, buf, C_LOW, 2);
            ui::label(290, kTop + 79, F("fps"), C_DIM);
        }
    }

    if (force || paintedEpoch != graphEpoch) {
        ui::graph(GRAPH_FULL_X, kStripY, kStripW, 20, gGpu, C_GPU);
        ui::graphLine(GRAPH_FULL_X, kStripY + 24, kStripW, 20, gGpuClock, C_GPU_FREQ);
    }
    if (force || painted.gpuUsage != d.gpuUsage) {
        ui::formatValue(buf, sizeof(buf), d.gpuUsage, "%");
        ui::valueField(308, kStripY + 6, 56, buf, C_GPU);
    }
    if (force || painted.gpuClock != d.gpuClock) {
        ui::formatValue(buf, sizeof(buf), d.gpuClock, "");
        ui::valueField(308, kStripY + 30, 56, buf, C_GPU_FREQ);
    }
}

// ===========================================================================
//  D5  power
// ===========================================================================
void staticPower() {
    ui::box(4, kTop + 4, 312, 78, F("power draw"));
    ui::label(190, kTop + 24, F("cpu"));
    ui::label(190, kTop + 38, F("gpu"));
    ui::label(190, kTop + 52, F("other"));
    ui::box(4, kTop + 90, 312, 130, F("history"));
}

void livePower(const SysData &d, bool force) {
    char buf[16];

    if (force || painted.pwrTotal != d.pwrTotal) {
        ui::formatTenths(buf, sizeof(buf), d.pwrTotal, "");
        tft.fillRect(14, kTop + 20, 170, 40, C_BG);
        ui::bigNumber(14, kTop + 20, buf, C_PWR, "W total", 5);
    }
    if (force || painted.pwrCpu != d.pwrCpu) {
        ui::formatTenths(buf, sizeof(buf), d.pwrCpu, " W");
        ui::valueField(306, kTop + 24, 76, buf, C_CPU);
    }
    if (force || painted.pwrGpu != d.pwrGpu) {
        ui::formatTenths(buf, sizeof(buf), d.pwrGpu, " W");
        ui::valueField(306, kTop + 38, 76, buf, C_GPU);
    }
    if (force || painted.pwrOther != d.pwrOther) {
        ui::formatTenths(buf, sizeof(buf), d.pwrOther, " W");
        ui::valueField(306, kTop + 52, 76, buf, C_DIM);
    }
    if (force || paintedEpoch != graphEpoch) {
        ui::graphPlot(8, kTop + 96, 302, 118, gPower, C_PWR, 0);
    }
    // Auto-scaled, so the axis carries the peak instead of a separate label --
    // and is repainted only when that peak actually moves, not every tick.
    if (force || paintedMaxPower != maxPower) {
        ui::graphAxes(8, kTop + 96, 302, 118, (uint16_t)(maxPower / 10), "W");
        paintedMaxPower = maxPower;
    }
}

// ===========================================================================
//  D6  network
// ===========================================================================
void staticNet() {
    ui::box(4, kTop + 4, 312, 50, F("network"));
    ui::label(14, kTop + 16, F("down"));
    ui::label(152, kTop + 21, F("KiB/s"), C_DIM);
    ui::label(14, kTop + 34, F("up"));
    ui::label(152, kTop + 39, F("KiB/s"), C_DIM);
    ui::label(200, kTop + 16, F("rx tot"));
    ui::label(200, kTop + 28, F("tx tot"));
    ui::label(200, kTop + 40, F("link"));
    ui::box(4, kTop + 62, 312, 78, F("down"));
    ui::box(4, kTop + 146, 312, 78, F("up"));
}

void liveNet(const SysData &d, bool force) {
    char buf[16];

    if (force || painted.netRx != d.netRx) {
        ui::formatValue(buf, sizeof(buf), d.netRx, "");
        ui::valueField(150, kTop + 14, 88, buf, C_NET_RX, 2);
    }
    if (force || painted.netTx != d.netTx) {
        ui::formatValue(buf, sizeof(buf), d.netTx, "");
        ui::valueField(150, kTop + 32, 88, buf, C_NET_TX, 2);
    }
    if (force || painted.netRxTotal != d.netRxTotal) {
        snprintf(buf, sizeof(buf), "%ld MiB", (long)d.netRxTotal);
        ui::valueField(306, kTop + 16, 68, buf, C_DIM);
    }
    if (force || painted.netTxTotal != d.netTxTotal) {
        snprintf(buf, sizeof(buf), "%ld MiB", (long)d.netTxTotal);
        ui::valueField(306, kTop + 28, 68, buf, C_DIM);
    }
    if (force || painted.netLink != d.netLink) {
        ui::formatValue(buf, sizeof(buf), d.netLink, " Mb");
        ui::valueField(306, kTop + 40, 68, buf, C_DIM);
    }
    if (force || paintedEpoch != graphEpoch) {
        // Both auto-scaled, and each keeps its own maximum: a 1 KiB/s upstream
        // should not be flattened by a 100 MiB/s download sharing one scale.
        ui::graphPlot(8, kTop + 70, 302, 66, gNetRx, C_NET_RX, 0);
        ui::graphPlot(8, kTop + 154, 302, 66, gNetTx, C_NET_TX, 0);
    }
    // The axis gutter is four characters wide, and a saturated 1 Gbit link peaks
    // near 122070 KiB/s -- so switch to MiB/s once the numbers stop fitting
    // rather than letting the label run into the plot.
    if (force || paintedMaxRx != maxNetRx) {
        if (maxNetRx >= 10240) {
            ui::graphAxes(8, kTop + 70, 302, 66, maxNetRx / 1024, "M");
        } else {
            ui::graphAxes(8, kTop + 70, 302, 66, maxNetRx, "K");
        }
        paintedMaxRx = maxNetRx;
    }
    if (force || paintedMaxTx != maxNetTx) {
        if (maxNetTx >= 10240) {
            ui::graphAxes(8, kTop + 154, 302, 66, maxNetTx / 1024, "M");
        } else {
            ui::graphAxes(8, kTop + 154, 302, 66, maxNetTx, "K");
        }
        paintedMaxTx = maxNetTx;
    }
}

// ===========================================================================
//  D7  processes
// ===========================================================================
void staticProc() {
    ui::box(4, kTop + 2, 312, 42, F("cpu"));
    ui::box(4, kTop + 50, 312, 174, F("processes"));

    tft.setTextSize(1);
    tft.setTextColor(C_LABEL, C_BG);
    tft.printAt(12, kTop + 58, F("name"));
    ui::valueRight(248, kTop + 58, "cpu", C_LABEL, 1);
    ui::valueRight(306, kTop + 58, "mem", C_LABEL, 1);
    tft.drawFastHLine(10, kTop + 68, 300, C_BOX);
}

void liveProc(const SysData &d, bool force) {
    char buf[12];

    if (force || paintedEpoch != graphEpoch) {
        ui::graph(GRAPH_FULL_X, kTop + 8, GRAPH_FULL_W, 30, gCpu);
    }
    if (force || painted.procCpu != d.procCpu) {
        ui::formatValue(buf, sizeof(buf), d.procCpu, "%");
        ui::valueField(310, kTop + 22, 54, buf, heatColor(d.procCpu), 2);
    }

    if (d.procRows == 0) {
        if (force) ui::label(12, kTop + 76, F("waiting for process list..."), C_DIM);
        return;
    }

    for (uint8_t i = 0; i < d.procRows; i++) {
        const int16_t y = kTop + 74 + i * 12;
        if (y + 8 > 232) break;

        const ProcEntry &p = d.procs[i];
        const bool sameRow = !force && i < painted.procRows &&
                             painted.procs[i].cpu == p.cpu &&
                             painted.procs[i].mem == p.mem &&
                             strcmp(painted.procs[i].name, p.name) == 0;
        if (sameRow) continue;

        // Names vary in length, so clear the name cell rather than relying on
        // opaque text, or a shorter name leaves the tail of a longer one behind.
        tft.fillRect(12, y, 180, 8, C_BG);
        tft.setTextSize(1);
        tft.setTextColor(C_TEXT, C_BG);
        tft.printAt(12, y, p.name);

        snprintf(buf, sizeof(buf), "%d%%", p.cpu);
        ui::valueField(248, y, 40, buf, heatColor(p.cpu));

        ui::formatMib(buf, sizeof(buf), p.mem);
        ui::valueField(306, y, 50, buf, C_DIM);
    }

    // Clear rows that existed last time but not now, so a shrinking list does not
    // leave ghosts behind.
    for (uint8_t i = d.procRows; i < painted.procRows; i++) {
        const int16_t y = kTop + 74 + i * 12;
        if (y + 8 > 232) break;
        tft.fillRect(10, y, 300, 9, C_BG);
    }
}

// ===========================================================================
//  Diagnostics  (long press)
// ===========================================================================
void staticDiag() {
    ui::box(4, kTop + 4, 312, 40, F("link"));
    ui::box(4, kTop + 48, 312, 76, F("bytes per 1s tick"));
    ui::box(4, kTop + 128, 312, 96, F("messages from host"));

    ui::label(12, kTop + 16, F("ok"));
    ui::label(90, kTop + 16, F("bad"));
    ui::label(168, kTop + 16, F("last"));
    ui::label(246, kTop + 16, F("age"));
}

void liveDiag(const SysData &d, bool force) {
    (void)d;
    char buf[16];

    // Counters. Rejected is the one that matters: a rising bad count means
    // frames are arriving corrupted, which looks identical to a slow agent from
    // every other screen.
    snprintf(buf, sizeof(buf), "%u", (unsigned)link.accepted());
    ui::valueField(84, kTop + 26, 72, buf, C_LOW);

    snprintf(buf, sizeof(buf), "%u", (unsigned)link.rejected());
    ui::valueField(162, kTop + 26, 72, buf,
                   link.rejected() ? C_CRIT : C_DIM);

    snprintf(buf, sizeof(buf), "%uB", (unsigned)link.lastLineLen());
    ui::valueField(240, kTop + 26, 72, buf, C_TEXT);

    const unsigned long age = link.lastLineAgeMs();
    if (age > 99000UL) {
        snprintf(buf, sizeof(buf), ">99s");
    } else {
        snprintf(buf, sizeof(buf), "%u.%us", (unsigned)(age / 1000),
                 (unsigned)((age % 1000) / 100));
    }
    ui::valueField(310, kTop + 26, 68, buf, age > 6000UL ? C_CRIT : C_DIM);

    // Timing graph: one bucket per second, height = longest message in it.
    if (force || paintedEpoch != graphEpoch) {
        ui::graphPlot(8, kTop + 54, 302, 66, gDiag, C_CPU, 0);
    }
    if (force || paintedMaxDiag != maxDiagLen) {
        ui::graphAxes(8, kTop + 54, 302, 66, maxDiagLen, "B");
        paintedMaxDiag = maxDiagLen;
    }

    // Raw message dump, newest first.
    const uint8_t rows = link.diagCount();
    tft.setTextSize(1);
    for (uint8_t i = 0; i < Link::DIAG_LINES; i++) {
        const int16_t y = kTop + 138 + i * 13;
        tft.fillRect(10, y, 300, 9, C_BG);
        if (i >= rows) continue;

        const char *line = link.diagLine(i);
        // Newest highlighted, older ones progressively quieter, so the order is
        // obvious without numbering every row.
        tft.setTextColor(i == 0 ? C_TEXT : C_DIM, C_BG);
        tft.printAt(10, y, line);
    }
}

}  // namespace

// ---------------------------------------------------------------------------

void recordDiag(uint16_t maxLen, uint8_t count) {
    (void)count;
    if (maxLen > maxDiagLen) maxDiagLen = maxLen;
    gDiag.push(maxLen == 0 ? 0 : scaleTo100((int32_t)maxLen, maxDiagLen));
    graphEpoch++;
}

void resetHistory() {
    gCpu.reset();
    gMem.reset();
    gGpu.reset();
    gGpuClock.reset();
    gPower.reset();
    gNetRx.reset();
    gNetTx.reset();
    maxGpuClock = 1;
    maxNetRx = 1;
    maxNetTx = 1;
    maxPower = 1;
    graphEpoch++;
}

void invalidate() {
    painted = SysData();
    paintedEpoch = 0xFFFF;
    linkKnown = false;
    paintedMaxPower = 0xFFFFFFFFul;
    paintedMaxRx = 0xFFFFFFFFul;
    paintedMaxTx = 0xFFFFFFFFul;
    paintedMaxDiag = 0xFFFFFFFFul;
}

// Advances history by exactly one sample. Driven by the render clock, not by
// arrivals, so the timeline is honest: one pixel is one second whether the agent
// answered on time, early, or not at all. Pushing on arrival instead made the
// horizontal axis meaningless -- a burst of three frames advanced the plot three
// pixels in an instant, and a stalled link left it frozen.
//
// Only the screen on show advances. Datasets for other screens are not being
// received, so pushing their last known value once a second would draw a
// confident flat line out of data nobody sent.
void record(uint8_t id, const SysData &d) {
    switch (id) {
        case SCR_CPU:
            gCpu.push(pctOrNone(d.cpuUsage));
            break;
        case SCR_MEM: {
            const int pct = memPercent(d);
            gMem.push(pct == SYSMON_NA ? GRAPH_NONE : (uint8_t)pct);
            break;
        }
        case SCR_GPU:
            gGpu.push(pctOrNone(d.gpuUsage));
            gGpuClock.push(d.gpuClock == SYSMON_NA
                               ? GRAPH_NONE
                               : scaleTo100(d.gpuClock, maxGpuClock));
            break;
        case SCR_POWER:
            gPower.push(d.pwrTotal == SYSMON_NA ? GRAPH_NONE
                                                : scaleTo100(d.pwrTotal, maxPower));
            break;
        case SCR_NET:
            gNetRx.push(d.netRx == SYSMON_NA ? GRAPH_NONE : scaleTo100(d.netRx, maxNetRx));
            gNetTx.push(d.netTx == SYSMON_NA ? GRAPH_NONE : scaleTo100(d.netTx, maxNetTx));
            break;
        case SCR_PROC:
            // The strip on D7 shares the CPU history, so watching it does not
            // leave a gap in D1's graph.
            gCpu.push(pctOrNone(d.procCpu));
            break;
        default:
            return;
    }
    graphEpoch++;
}

const __FlashStringHelper *name(uint8_t id) {
    switch (id) {
        case SCR_CPU: return F("CPU");
        case SCR_CORES: return F("CORES");
        case SCR_MEM: return F("MEMORY");
        case SCR_GPU: return F("GPU");
        case SCR_POWER: return F("POWER");
        case SCR_NET: return F("NETWORK");
        case SCR_PROC: return F("PROCESSES");
        case SCR_DIAG: return F("DIAGNOSTICS");
        default: return F("?");
    }
}

void drawStatic(uint8_t id) {
    tft.fillScreen(C_BG);
    ui::headerChrome(name(id), id, SCR_COUNT);

    switch (id) {
        case SCR_CPU: staticCpu(); break;
        case SCR_CORES: staticCores(); break;
        case SCR_MEM: staticMem(); break;
        case SCR_GPU: staticGpu(); break;
        case SCR_POWER: staticPower(); break;
        case SCR_NET: staticNet(); break;
        case SCR_PROC: staticProc(); break;
        case SCR_DIAG: staticDiag(); break;
        default: break;
    }
}

void drawLinkState(bool linkUp, bool stale) {
    if (linkKnown && linkUp == paintedLink && stale == paintedStale) return;
    ui::headerLink(linkUp, stale);
    paintedLink = linkUp;
    paintedStale = stale;
    linkKnown = true;
}

void drawLive(uint8_t id, const SysData &d, bool force) {
    switch (id) {
        case SCR_CPU: liveCpu(d, force); break;
        case SCR_CORES: liveCores(d, force); break;
        case SCR_MEM: liveMem(d, force); break;
        case SCR_GPU: liveGpu(d, force); break;
        case SCR_POWER: livePower(d, force); break;
        case SCR_NET: liveNet(d, force); break;
        case SCR_PROC: liveProc(d, force); break;
        case SCR_DIAG: liveDiag(d, force); break;
        default: break;
    }

    painted = d;
    paintedEpoch = graphEpoch;
}

}  // namespace screens
