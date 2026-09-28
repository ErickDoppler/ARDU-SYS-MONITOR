// ---------------------------------------------------------------------------
//  NetworkSensor -- throughput from GetIfTable2.
//
//  Byte counters are cumulative, so a rate needs two readings and the elapsed
//  time between them. The interval is measured rather than assumed: the polling
//  interval is nominal, and a 2 s setting that actually took 2.4 s would
//  otherwise inflate every rate by 20%.
//
//  Loopback and non-operational interfaces are skipped. Loopback in particular
//  would count local traffic that never touches a wire, and on a machine with
//  virtual adapters -- this one has Virtual Desktop and Meta monitors installed
//  -- that noise dominates the real link.
//
//  Counters can go backwards when an adapter resets or a VPN comes up. A
//  negative delta is dropped rather than clamped, because clamping to zero would
//  still let the totals drift.
// ---------------------------------------------------------------------------
// Include order here is load bearing, and the failure is confusing if it is
// wrong -- "MIB_IF_TABLE2 was not declared", as though the SDK were too old.
// Two gates have to be open:
//   * iphlpapi.h only pulls in netioapi.h when NTDDI_VERSION >= 0x06000000
//   * inside netioapi.h the MIB_IF_ROW2 / GetIfTable2 block sits behind
//     #ifdef _WS2IPDEF_, and netioapi.h skips including ws2ipdef.h itself once
//     iphlpapi.h is in play
// So ws2ipdef.h must come first, and winsock2.h before it or windows.h drags in
// the old winsock.h and the two collide.
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <algorithm>

#include "Sensors.h"

namespace {

class NetworkSensor final : public SensorSource {
public:
    bool init() override {
        m_available = true;
        // Prime, so the first real sample has a baseline to subtract.
        uint64_t rx = 0, tx = 0, link = 0;
        if (!readTotals(rx, tx, link)) {
            m_available = false;
            return false;
        }
        m_prevRx = rx;
        m_prevTx = tx;
        m_prevTick = GetTickCount64();
        return true;
    }

    void sample(SensorSnapshot &out) override {
        if (!m_available) return;

        uint64_t rx = 0, tx = 0, link = 0;
        if (!readTotals(rx, tx, link)) return;

        const uint64_t now = GetTickCount64();
        const uint64_t elapsedMs = (now > m_prevTick) ? now - m_prevTick : 0;

        if (elapsedMs >= 200 && rx >= m_prevRx && tx >= m_prevTx) {
            const uint64_t dRx = rx - m_prevRx;
            const uint64_t dTx = tx - m_prevTx;

            out.netRxKibs = static_cast<int>((dRx * 1000ULL) / elapsedMs / 1024ULL);
            out.netTxKibs = static_cast<int>((dTx * 1000ULL) / elapsedMs / 1024ULL);

            m_sessionRx += dRx;
            m_sessionTx += dTx;
        } else {
            // Adapter reset or too soon to be meaningful: report 0 this round
            // rather than a fabricated spike, and re-baseline below.
            out.netRxKibs = 0;
            out.netTxKibs = 0;
        }

        out.netRxTotalMib = static_cast<int>(m_sessionRx >> 20);
        out.netTxTotalMib = static_cast<int>(m_sessionTx >> 20);
        out.netLinkMbps = link ? static_cast<int>(link / 1000000ULL) : SYSMON_NA;

        m_prevRx = rx;
        m_prevTx = tx;
        m_prevTick = now;
    }

    const char *name() const override { return "Win32/Network"; }
    bool available() const override { return m_available; }

private:
    // Sums octets over real, up interfaces. linkOut receives the fastest such
    // link's speed in bits/s, which is the one the user thinks of as "the" NIC.
    static bool readTotals(uint64_t &rxOut, uint64_t &txOut, uint64_t &linkOut) {
        MIB_IF_TABLE2 *table = nullptr;
        if (GetIfTable2(&table) != NO_ERROR || !table) return false;

        uint64_t rx = 0, tx = 0, link = 0;
        for (ULONG i = 0; i < table->NumEntries; i++) {
            const MIB_IF_ROW2 &row = table->Table[i];
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (row.OperStatus != IfOperStatusUp) continue;
            if (row.InterfaceAndOperStatusFlags.FilterInterface) continue;

            rx += row.InOctets;
            tx += row.OutOctets;
            link = std::max<uint64_t>(link, row.ReceiveLinkSpeed);
        }
        FreeMibTable(table);

        rxOut = rx;
        txOut = tx;
        linkOut = link;
        return true;
    }

    bool m_available = false;
    uint64_t m_prevRx = 0, m_prevTx = 0;
    uint64_t m_sessionRx = 0, m_sessionTx = 0;
    uint64_t m_prevTick = 0;
};

}  // namespace

std::unique_ptr<SensorSource> makeNetworkSource() {
    return std::make_unique<NetworkSensor>();
}
