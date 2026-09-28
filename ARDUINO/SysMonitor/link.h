// ---------------------------------------------------------------------------
//  link.h -- the serial conversation and the data it produces.
//
//  This side drives: it asks for the screen currently on show, the desktop
//  answers and then keeps resending until a different screen is requested. See
//  PROTOCOL.md.
//
//  Parsing happens in place in the receive buffer, with no String and no heap.
//  On an ATmega1280 the fragmentation from building Strings every couple of
//  seconds is what eventually wedges a long-running sketch.
// ---------------------------------------------------------------------------
#ifndef LINK_H
#define LINK_H

#include <Arduino.h>

#include "sysmon_wire.h"

#define PROC_NAME_BUF (SYSMON_PROC_NAME_MAX + 1)

// NOTE ON WIDTHS: `int` is 16 bits on AVR, so anything that can exceed 32767 has
// to be spelled int32_t explicitly. That is not theoretical here -- this machine
// reports 130981 MiB of RAM, which wraps to -91 in an int and made the memory
// screen read n/a. The rule below is: MiB and KiB/s quantities are 32-bit,
// percentages, temperatures, clocks and fps stay 16-bit.
struct ProcEntry {
    char name[PROC_NAME_BUF];
    int8_t cpu;
    int32_t mem;  // MiB -- a VM or database can hold more than 32 GiB
};

// Everything the screens draw. Values are exactly as they arrive on the wire,
// including SYSMON_NA, so "no sensor" survives all the way to the pixel.
struct SysData {
    // D1
    int cpuUsage = SYSMON_NA;
    int cpuFreq = SYSMON_NA;
    int cpuTemp = SYSMON_NA;
    int procCount = SYSMON_NA;
    long uptime = 0;

    // D2
    uint8_t coreCount = 0;
    int8_t coreUsage[SYSMON_MAX_CORES];
    int16_t coreFreq[SYSMON_MAX_CORES];

    // D3 -- MiB, 32-bit: 128 GiB of RAM is 131072, far past an AVR int
    int32_t memUsed = SYSMON_NA, memTotal = SYSMON_NA, memCached = SYSMON_NA;
    int32_t commitUsed = SYSMON_NA, commitLimit = SYSMON_NA;

    // D4
    int gpuUsage = SYSMON_NA, gpuPower = SYSMON_NA, gpuClock = SYSMON_NA;
    int32_t vramUsed = SYSMON_NA, vramTotal = SYSMON_NA;  // MiB, 48 GiB cards exist
    int fps = SYSMON_NA;
    int gpuTemp = SYSMON_NA;

    // D5
    int pwrTotal = SYSMON_NA, pwrCpu = SYSMON_NA, pwrGpu = SYSMON_NA;
    int pwrOther = SYSMON_NA;

    // D6 -- KiB/s and MiB, 32-bit: a 1 Gbit link peaks near 122070 KiB/s, and the
    // session totals pass 32767 MiB after 32 GiB moved
    int32_t netRx = SYSMON_NA, netTx = SYSMON_NA;
    int32_t netRxTotal = 0, netTxTotal = 0;
    int netLink = SYSMON_NA;

    // D7
    int procCpu = SYSMON_NA;
    uint8_t procRows = 0;
    ProcEntry procs[SYSMON_PROC_ROWS];

    // Highest core count the desktop has ever reported, from HELLO. Lets the
    // per-core screen lay itself out before any D2 has arrived.
    uint8_t announcedCores = 0;
};

// Two SysData buffers with a pointer flip between them.
//
// The parser always writes into the back buffer and only publishes it once a
// whole dataset has been validated, so the renderer can never read a half-parsed
// frame -- previously a burst of packets repainted the screen mid-parse, which is
// what made updates arrive in lumps instead of in tempo.
//
// Publishing seeds the new back buffer from the one just published. Each dataset
// fills the fields for one screen only, so without that carry-forward, switching
// screens would blank everything the other screens had collected.
class DataStore {
public:
    SysData &back() { return m_buf[m_backIndex]; }
    const SysData &actual() const { return m_buf[(uint8_t)(1 - m_backIndex)]; }

    void publish() {
        const uint8_t published = m_backIndex;
        m_backIndex = (uint8_t)(1 - m_backIndex);
        m_buf[m_backIndex] = m_buf[published];  // carry forward
        m_generation++;
    }

    // Changes whenever a dataset is published; lets the renderer skip work when
    // nothing new has arrived since it last painted.
    uint16_t generation() const { return m_generation; }

private:
    SysData m_buf[2];
    uint8_t m_backIndex = 0;
    uint16_t m_generation = 0;
};

class Link {
public:
    void begin();

    // Pumps the serial port. Returns the screen id of a dataset that arrived
    // this call, or 0. The caller redraws on a nonzero return.
    uint8_t poll(SysData &data);

    // Asks for a screen. Also called on a timeout to re-establish the stream.
    void request(uint8_t screen);
    void requestHello();

    bool linkUp() const { return m_linkUp; }
    // True when the link is up but the last dataset is older than it should be.
    bool stale() const { return m_stale; }

    // Call from loop(): handles timeouts and re-requests.
    void tick(uint8_t currentScreen);

    // --- diagnostics ------------------------------------------------------
    // A short history of what actually arrived, kept so the diagnostics screen
    // can show the raw traffic rather than only its parsed result. Rejected
    // lines are logged too -- a checksum failure is exactly the thing you want
    // to see, and it is invisible everywhere else.
    static const uint8_t DIAG_LINES = 6;
    static const uint8_t DIAG_LEN = 45;  // 44 chars + terminator

    const char *diagLine(uint8_t index) const;  // 0 = newest
    uint8_t diagCount() const { return m_diagCount; }

    uint16_t accepted() const { return m_accepted; }
    uint16_t rejected() const { return m_rejected; }
    uint16_t lastLineLen() const { return m_lastLen; }
    unsigned long lastLineAgeMs() const { return millis() - m_lastDataMs; }

    // Per-tick accumulators for the timing graph. Reading clears them, so the
    // renderer takes exactly one bucket per tick with no double counting.
    uint16_t takeTickMaxLen() {
        const uint16_t v = m_tickMaxLen;
        m_tickMaxLen = 0;
        return v;
    }
    uint8_t takeTickCount() {
        const uint8_t v = m_tickCount;
        m_tickCount = 0;
        return v;
    }

private:
    bool validate(char *line, uint16_t len, char **payloadOut, uint16_t *payloadLen);
    uint8_t dispatch(char *payload, SysData &data);
    void send(const char *payload);
    void logLine(const char *line, uint16_t len);

    char m_buf[SYSMON_MAX_LINE];
    uint16_t m_len = 0;

    unsigned long m_lastDataMs = 0;
    unsigned long m_lastRequestMs = 0;
    bool m_linkUp = false;
    bool m_stale = false;
    bool m_overflow = false;

    char m_diag[DIAG_LINES][DIAG_LEN];
    uint8_t m_diagCount = 0;
    uint8_t m_diagHead = 0;
    uint16_t m_accepted = 0;
    uint16_t m_rejected = 0;
    uint16_t m_lastLen = 0;
    uint16_t m_tickMaxLen = 0;
    uint8_t m_tickCount = 0;
};

extern Link link;

#endif  // LINK_H
