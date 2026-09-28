// ---------------------------------------------------------------------------
//  persist.h -- remembers which screen you were on, across power cycles.
//
//  A screen is saved once it has been on show for a full minute, and the saved
//  screen is restored at boot. The delay is the whole point: without it, every
//  tap while paging through to the one you want would be written, and the panel
//  would come back on whatever you happened to pass through last.
//
//  EEPROM endurance is the constraint that shapes the rest. The ATmega1280's
//  cells are rated for about 100,000 writes, so this is careful in three ways:
//
//    * nothing is written until a screen has been held for a minute
//    * nothing is written if the stored value already matches
//    * EEPROM.update() is used, which reads first and skips an unchanged byte
//
//  At the theoretical worst -- deliberately alternating between two screens at
//  exactly one-minute intervals -- that is 60 writes an hour, and the rating
//  would last about 190 years. In normal use it writes a handful of times ever.
//
//  Stored as magic + value + check rather than a bare byte. A blank AVR EEPROM
//  reads 0xFF everywhere, and a chip that previously ran other firmware holds
//  whatever that left behind; without the guard, either could be read back as a
//  plausible screen id and the display would boot to something arbitrary.
// ---------------------------------------------------------------------------
#ifndef PERSIST_H
#define PERSIST_H

#include <Arduino.h>
#include <EEPROM.h>

#include "sysmon_wire.h"

#define PERSIST_ADDR_MAGIC  0
#define PERSIST_ADDR_SCREEN 1
#define PERSIST_ADDR_CHECK  2

// Arbitrary, but not 0x00 or 0xFF: those are exactly the values a blank or
// erased cell produces, so they would defeat the check they exist to perform.
#define PERSIST_MAGIC 0xA7

// How long a screen must be on show before it is worth remembering.
#define PERSIST_DWELL_MS 60000UL

inline uint8_t persistCheck(uint8_t screen) {
    return (uint8_t)(PERSIST_MAGIC ^ screen ^ 0x5A);
}

// Returns the stored screen, or 0 if nothing valid is stored.
inline uint8_t persistLoadScreen() {
    if (EEPROM.read(PERSIST_ADDR_MAGIC) != PERSIST_MAGIC) return 0;

    const uint8_t screen = EEPROM.read(PERSIST_ADDR_SCREEN);
    if (EEPROM.read(PERSIST_ADDR_CHECK) != persistCheck(screen)) return 0;

    // Range-checked even after the magic and checksum pass: the firmware may
    // have been built with fewer screens since the value was written.
    if (screen < SCR_FIRST || screen > SCR_LAST) return 0;
    return screen;
}

// Returns true if anything was actually written.
inline bool persistSaveScreen(uint8_t screen) {
    if (screen < SCR_FIRST || screen > SCR_LAST) return false;
    if (persistLoadScreen() == screen) return false;  // already stored

    // update() reads before writing, so a byte that happens to match already
    // costs nothing. Writing the magic last would leave a window where a power
    // cut mid-save produced a valid-looking record with a stale screen, so the
    // check byte goes last: it is what makes the record believable.
    EEPROM.update(PERSIST_ADDR_MAGIC, PERSIST_MAGIC);
    EEPROM.update(PERSIST_ADDR_SCREEN, screen);
    EEPROM.update(PERSIST_ADDR_CHECK, persistCheck(screen));
    return true;
}

#endif  // PERSIST_H
