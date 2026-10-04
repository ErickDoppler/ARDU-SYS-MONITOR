// ===========================================================================
//  Multi Screen System Monitor -- display firmware
//
//  Arduino MEGA (ATmega1280/2560) + ITDB02-3.2S TFT (SSD1289, 320x240, 16-bit)
//  + resistive touch. Shows live system telemetry sent by the desktop agent.
//
//  Tap anywhere to move to the next screen. The firmware asks the desktop for
//  whichever screen is showing and nothing else, so the link stays small enough
//  for a 16 MHz AVR to parse between frames -- see PROTOCOL.md.
//
//  --- how drawing is decoupled from the data ------------------------------
//  Receiving and painting run on separate clocks, which is what stops the panel
//  stalling and then dumping several updates back to back:
//
//    receive   parses into the back buffer and publishes it, pointer-flip style,
//              only once a whole dataset has validated. Happens whenever bytes
//              turn up, at whatever rate the agent and the USB stack manage.
//    render    every 2 s exactly, reads the published buffer and repaints only
//              the fields whose values moved. It never waits for data, and it
//              never paints a half-parsed frame.
//
//  Note TFT_WR_NOPS in board_config.h: this panel needs a much wider write
//  strobe than its datasheet claims, and a too-short one shows up as a blank
//  white screen rather than as anything obviously timing related. See
//  ARDUINO/README.md for the measurements.
//
//  Build:  tools\upload.ps1   (or the Arduino IDE, board = Arduino Mega, 1280)
// ===========================================================================

#include "board_config.h"
#include "gesture.h"
#include "link.h"

// The AVR core's default 64-byte receive buffer holds only ~5.5 ms of stream at
// 115200. A graph repaint takes around 35 ms, so a per-core frame arriving during
// one is truncated, fails its checksum and is dropped -- which the firmware then
// has to wait out and re-request. That is the "stalls, then dumps several at
// once" behaviour.
//
// tools\upload.ps1 passes -DSERIAL_RX_BUFFER_SIZE=768, which covers a whole
// frame. Building from the Arduino IDE cannot set it, so the firmware still
// works there -- it will just drop a frame occasionally under load.
#if !defined(SERIAL_RX_BUFFER_SIZE) || SERIAL_RX_BUFFER_SIZE < 256
#warning "SERIAL_RX_BUFFER_SIZE < 256: frames may be dropped during redraws. Build with tools\\upload.ps1 for -DSERIAL_RX_BUFFER_SIZE=512."
#endif

#include "persist.h"
#include "screens.h"
#include "theme.h"
#include "watchdog.h"
#include "TftSSD1289.h"
#include "TouchPanel.h"
#include "widgets.h"

static DataStore store;
static TouchGesture gesture;

// The data screen being requested from the agent. It keeps its value while the
// diagnostics overlay is up, so traffic continues and the diagnostics screen has
// something live to report on -- which is the whole point of it.
static uint8_t currentScreen = SCR_CPU;

// Diagnostics is an overlay rather than an eighth screen: it is not in the tap
// rotation and the agent never hears about it.
static bool diagMode = false;
static unsigned long diagEnteredMs = 0;

// Taps are ignored for this long after diagnostics opens. Without it, the finger
// still resting on the glass from the long press would be read as the tap that
// dismisses it.
static const unsigned long kDiagTapGuardMs = 2000;

// When the current screen was selected, and whether this dwell has already been
// written to EEPROM. Together these are what limit persistence to one write per
// settled screen rather than one per tap -- see persist.h on cell endurance.
static unsigned long screenSinceMs = 0;
static bool screenPersisted = false;

// Link state as of the previous loop, so the drop can be acted on once rather
// than every iteration while it stays down.
static bool linkWasUp = false;

// The render clock. Fixed and independent of arrivals, so the panel updates in
// tempo whether the agent is early, late or bursty. History advances one sample
// per tick, so this is also the horizontal scale of every graph: 1 px = 1 s, and
// a 276 px plot spans about 4.5 minutes.
static const unsigned long kRenderPeriodMs = 1000;
static unsigned long lastRender = 0;

// Set when the layout itself must be repainted: at boot and on a screen change.
static bool layoutDirty = true;

static void gotoScreen(uint8_t screen) {
    if (screen < SCR_FIRST) screen = SCR_LAST;
    if (screen > SCR_LAST) screen = SCR_FIRST;

    currentScreen = screen;
    link.request(currentScreen);  // the desktop answers at once
    layoutDirty = true;

    // Restart the dwell clock: only a screen you settle on gets remembered,
    // not every one you page past on the way to it.
    screenSinceMs = millis();
    screenPersisted = false;

    Serial.print(F("# screen "));
    Serial.println(currentScreen);
}

static void splash() {
    tft.fillScreen(C_BG);

    tft.setTextSize(2);
    tft.setTextColor(C_TITLE, C_BG);
    tft.printAt(28, 78, F("SYSTEM MONITOR"));

    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_BG);
    tft.printAt(28, 104, F("waiting for the desktop agent..."));
    tft.printAt(28, 118, F("tap to change screen"));

    tft.drawRoundRect(18, 62, 284, 76, 5, C_BOX);
}

void setup() {
    tft.begin(TFT_DEFAULT_ROT);
    tft.fillScreen(C_BG);

    touch.begin();
    touch.setScreenSize(tft.width(), tft.height());
    gesture.begin();

    link.begin();
    Serial.println();
    Serial.println(F("# ARDU-SYS-MONITOR display firmware"));

    // Why the board restarted. A watchdog cause means the firmware hung and
    // recovered itself; brown-out means the supply sagged, which is a hardware
    // problem no amount of firmware will fix. Worth knowing which.
    Serial.print(F("# reset: "));
    Serial.print(sysmonResetCause());
    Serial.print(F("  stack free: "));
    Serial.println(sysmonStackFree());

    // Come back on whichever screen was last settled on. Falls through to the
    // CPU screen when the EEPROM is blank, holds a foreign value, or was written
    // by a build with a different set of screens.
    const uint8_t saved = persistLoadScreen();
    if (saved) {
        currentScreen = saved;
        Serial.print(F("# restored screen "));
        Serial.println(currentScreen);
    }

    screens::resetHistory();
    splash();

    link.requestHello();
    link.request(currentScreen);

    // The restored screen is already what is stored, so starting the dwell clock
    // here costs nothing: persistSaveScreen() will find it unchanged and skip.
    screenSinceMs = millis();
    screenPersisted = false;

    // Hold the splash for one render period rather than clearing it instantly,
    // so it is readable on a cold start.
    lastRender = millis();

}

void loop() {
    // --- input --------------------------------------------------------------
    // Non-blocking, unlike the old waitForRelease(): a three-second hold cannot
    // be recognised while the loop is parked waiting for a finger to lift, and
    // serial would back up for the whole press.
    switch (gesture.poll()) {
        case TouchGesture::LongPress:
            if (!diagMode) {
                diagMode = true;
                diagEnteredMs = millis();
                layoutDirty = true;
                Serial.println(F("# diagnostics"));
            }
            break;

        case TouchGesture::Tap:
            if (diagMode) {
                if (millis() - diagEnteredMs >= kDiagTapGuardMs) {
                    diagMode = false;
                    layoutDirty = true;
                    Serial.println(F("# diagnostics closed"));
                }
            } else {
                gotoScreen((uint8_t)(currentScreen + 1));
            }
            break;

        default:
            break;
    }

    // --- receive: as fast as bytes arrive ----------------------------------
    // Parsing writes into the back buffer; publish() flips it to the front only
    // when a full dataset has validated, so the renderer never sees a partial
    // frame no matter when its timer fires.
    const uint8_t arrived = link.poll(store.back());
    if (arrived) {
        store.publish();
        // Note: history is NOT advanced here. It advances on the render tick, so
        // one pixel is always one second regardless of when frames turn up.
    }

    link.tick(currentScreen);

    // --- the link just went down -------------------------------------------
    // Drop everything the agent told us. Keeping the last values would leave
    // plausible-looking readings on screen next to a red link indicator, and --
    // worse -- the render tick would go on pushing them into history once a
    // second, drawing a confident flat line out of data that stopped arriving.
    //
    // History keeps advancing after this, but with empty samples, so the time
    // axis stays honest: the outage shows up as a gap of exactly its own length
    // rather than as a plateau.
    const bool linkUpNow = link.linkUp();
    if (!linkUpNow && linkWasUp) {
        store.clear();
        screens::clearDataHistory();
        layoutDirty = true;  // repaint so every field reads n/a immediately
        Serial.println(F("# link lost - data cleared"));
    }
    linkWasUp = linkUpNow;

    // --- remember a settled screen -----------------------------------------
    // Unsigned subtraction, so this still behaves when millis() wraps at ~49
    // days. The write blocks for a few milliseconds, but interrupts stay live
    // throughout, so the serial receive buffer keeps filling meanwhile.
    if (!screenPersisted && (millis() - screenSinceMs) >= PERSIST_DWELL_MS) {
        screenPersisted = true;  // set first: one attempt per dwell either way
        if (persistSaveScreen(currentScreen)) {
            Serial.print(F("# saved screen "));
            Serial.println(currentScreen);
        }
    }

    // --- render: strictly on its own 2 s clock ------------------------------
    const unsigned long now = millis();
    const bool due = (now - lastRender >= kRenderPeriodMs);

    // A screen change repaints at once so a tap feels instant. The very first
    // paint waits for the timer instead, which is what leaves the splash on
    // screen long enough to read on a cold start.
    static bool everDrawn = false;
    const bool wantLayout = layoutDirty && (everDrawn || due);

    if (wantLayout || due) {
        const uint8_t shown = diagMode ? SCR_DIAG : currentScreen;

        bool forceLive = false;
        if (wantLayout) {
            screens::drawStatic(shown);
            screens::invalidate();  // the wipe took the old values with it
            layoutDirty = false;
            everDrawn = true;
            // Everything must be repainted once over fresh chrome, including the
            // parts that only draw on a forced pass, such as the core indices.
            forceLive = true;
        }

        // One sample per tick, taken from whatever the published buffer holds
        // right now. A layout repaint does not add one -- switching screens
        // should not punch an extra pixel into the timeline.
        // Diagnostics history is collected on every tick whatever is on screen,
        // so opening it shows what already happened rather than starting blank.
        screens::recordDiag(link.takeTickMaxLen(), link.takeTickCount());
        if (!forceLive && !diagMode) screens::record(currentScreen, store.actual());

        screens::drawLinkState(link.linkUp(), link.stale());
        // Otherwise only changed fields are touched, which is what keeps a
        // refresh invisible instead of a full-panel blink.
        screens::drawLive(shown, store.actual(), forceLive);

        lastRender = now;
    }
}
