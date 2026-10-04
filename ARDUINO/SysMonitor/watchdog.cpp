#include "watchdog.h"

#include <avr/io.h>

// .noinit: not cleared by the C runtime, so it carries a value written before
// main() through to setup().
uint8_t g_resetCause __attribute__((section(".noinit")));

// Linker symbols marking the end of the variables and the top of RAM.
extern uint8_t _end;
extern uint8_t __stack;

void sysmonEarlyInit(void) {
    // MCUSR must be read before anything else touches it, and cleared before
    // the watchdog is disabled -- leaving WDRF set holds the watchdog on.
    g_resetCause = MCUSR;
    MCUSR = 0;
    wdt_disable();
}

// Written in assembly because it has to run before the C runtime has set up a
// stack frame to call into. Fills from the end of the variables up to the top of
// RAM with the canary byte.
void sysmonPaintStack(void) {
    __asm volatile(
        "    ldi r30, lo8(_end)\n"
        "    ldi r31, hi8(_end)\n"
        "    ldi r24, %0\n"
        "    ldi r25, hi8(__stack)\n"
        "    rjmp .Lcmp\n"
        ".Lloop:\n"
        "    st  Z+, r24\n"
        ".Lcmp:\n"
        "    cpi r30, lo8(__stack)\n"
        "    cpc r31, r25\n"
        "    brlo .Lloop\n"
        :
        : "i"(STACK_CANARY)
        : "r24", "r25", "r30", "r31");
}

uint16_t sysmonStackFree() {
    const uint8_t *p = &_end;
    uint16_t untouched = 0;
    while (p < &__stack && *p == STACK_CANARY) {
        untouched++;
        p++;
    }
    return untouched;
}

const __FlashStringHelper *sysmonResetCause() {
    // Tested in order of how much they tell you. Several bits can be set at
    // once -- a brown-out during a watchdog reset, say -- and the more specific
    // cause is the more useful one to report.
    if (g_resetCause & (1 << WDRF)) return F("watchdog");
    if (g_resetCause & (1 << BORF)) return F("brown-out");
    if (g_resetCause & (1 << EXTRF)) return F("external");
    if (g_resetCause & (1 << PORF)) return F("power-on");
    return F("unknown");
}
