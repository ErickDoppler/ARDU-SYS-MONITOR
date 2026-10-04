// ---------------------------------------------------------------------------
//  watchdog.h -- self-recovery, and evidence about why a restart happened.
//
//  Symptom this exists for: the panel goes blank white and stays that way until
//  the board is power cycled. White means the SSD1289 was reset without its
//  init sequence completing -- so the MCU is not merely stuck mid-frame, it
//  restarted and did not get far enough to configure the panel, or it stopped
//  altogether. Either way nothing recovers, because nothing is watching.
//
//  Three parts:
//
//  1. A watchdog. Any hang longer than four seconds resets the board, setup()
//     runs, the panel is reconfigured and the screen comes back. A fault that
//     used to need a power cycle now costs four seconds.
//
//  2. The reset cause, captured before anything can clear it. MCUSR
//     distinguishes power-on from brown-out from external reset from watchdog,
//     which is the difference between "the supply sagged", "something pulled
//     RESET" and "the firmware hung" -- three very different bugs that look
//     identical from the outside.
//
//  3. A stack low-water mark. RAM is 70% full; if the stack has been reaching
//     down into the globals then a corrupted display driver would explain the
//     white screen exactly, and this says so instead of leaving it a theory.
//
//  WATCHDOG SAFETY ON A MEGA: the stk500v2 bootloader does not clear WDRF or
//  disable the watchdog, so a watchdog reset can leave the board resetting
//  forever before the sketch ever runs -- the classic way to brick an AVR with
//  a watchdog. The .init3 hook below disables it before main(), which is what
//  makes enabling it safe here. The four second period is also deliberately
//  longer than the bootloader's ~1 s wait.
// ---------------------------------------------------------------------------
#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <Arduino.h>
#include <avr/wdt.h>

// Survives a reset: .noinit is neither zeroed nor initialised at startup.
extern uint8_t g_resetCause;

// Runs before main(), before .data/.bss are set up. Saves the reset cause and
// then makes the watchdog safe for the next boot.
void sysmonEarlyInit(void) __attribute__((naked, used, section(".init3")));

// Fills unused RAM with a known byte so the deepest stack excursion can be
// measured later. .init1 is early enough that the stack has barely been used.
void sysmonPaintStack(void) __attribute__((naked, used, section(".init1")));

#define STACK_CANARY 0xC5

// Bytes of RAM never touched by the stack. Small means trouble: at zero the
// stack has been writing over the globals, which on this firmware means the
// display driver's own state.
uint16_t sysmonStackFree();

// "power-on", "brown-out", "watchdog", "external", or a hex fallback.
const __FlashStringHelper *sysmonResetCause();

inline void sysmonWatchdogEnable() { wdt_enable(WDTO_4S); }
inline void sysmonWatchdogPat() { wdt_reset(); }

#endif  // WATCHDOG_H
