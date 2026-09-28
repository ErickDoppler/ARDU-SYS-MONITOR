// ---------------------------------------------------------------------------
//  protocol.h -- the wire contract, shared verbatim by both sides.
//
//  CANONICAL COPY: ARDU-SYS-MONITOR/SHARED/sysmon_wire.h
//  The Arduino sketch and the desktop app each hold a copy, because the Arduino
//  IDE only compiles files sitting in the sketch folder. Run
//  tools/sync-shared.ps1 after editing this file, or the two sides will disagree
//  about field order and you will chase phantom sensor bugs.
//
//  Plain C++ with no includes and no dynamic anything, so it is equally at home
//  in an AVR sketch and in the desktop build.
//
//  See PROTOCOL.md for the full specification and the reasoning.
// ---------------------------------------------------------------------------
#ifndef SYSMON_PROTOCOL_H
#define SYSMON_PROTOCOL_H

#define SYSMON_PROTOCOL_VERSION 1
#define SYSMON_BAUD             115200
#define SYSMON_APP_NAME         "ARDU-SYS-MONITOR"

// Longest line either side will ever build or accept. The per-core screen is the
// worst case: 32 cores x "100,4275,-1;" is about 400 bytes, so 512 leaves room
// without spending 640 bytes of an ATmega1280.s 8 KB on a receive buffer.
#define SYSMON_MAX_LINE         512

// A value no sensor could legitimately produce, meaning "not measurable here".
// Distinct from 0 on purpose: an absent CPU temperature and a 0 C CPU must not
// look the same on screen.
#define SYSMON_NA               (-1)

// Screen identifiers. The order is the tap order on the device.
enum SysmonScreen {
    SCR_CPU      = 1,   // D1  total usage, frequency, temperature
    SCR_CORES    = 2,   // D2  per-core bars
    SCR_MEM      = 3,   // D3  RAM and commit
    SCR_GPU      = 4,   // D4  usage, watts, clock, VRAM, fps
    SCR_POWER    = 5,   // D5  combined power draw
    SCR_NET      = 6,   // D6  receive/transmit rates
    SCR_PROC     = 7,   // D7  CPU strip plus top process list
    SCR_FIRST    = SCR_CPU,
    SCR_LAST     = SCR_PROC,
    SCR_COUNT    = SCR_LAST
};

// How many processes the desktop sends for D7, and how long a name may be.
#define SYSMON_PROC_ROWS        12
#define SYSMON_PROC_NAME_MAX    15

// Hard ceiling on cores reported in D2. Not arbitrary: 64 cores would need about
// 780 bytes, overflowing SYSMON_MAX_LINE, and 32 bars is already the most a
// 320x240 panel can show legibly. The desktop truncates to this.
#define SYSMON_MAX_CORES        32

// Framing characters.
#define SYSMON_OPEN             '{'
#define SYSMON_CLOSE            '}'
#define SYSMON_SEP              ';'
#define SYSMON_SUBSEP           ','
#define SYSMON_CHECK            '*'

// XOR of every payload byte -- everything between the braces, excluding the
// checksum marker and its two hex digits. Same routine on both sides so a
// mismatch is always a transport fault, never a disagreement about the sum.
inline unsigned char sysmonChecksum(const char *payload, unsigned int len) {
    unsigned char sum = 0;
    for (unsigned int i = 0; i < len; i++) sum ^= (unsigned char)payload[i];
    return sum;
}

inline char sysmonHexDigit(unsigned char nibble) {
    return (char)(nibble < 10 ? ('0' + nibble) : ('A' + (nibble - 10)));
}

// Returns -1 for a character that is not an uppercase hex digit.
inline int sysmonHexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

#endif // SYSMON_PROTOCOL_H
