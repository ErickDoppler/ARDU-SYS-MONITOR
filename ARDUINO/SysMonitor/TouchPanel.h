// ---------------------------------------------------------------------------
//  TouchPanel -- bit-banged SPI driver for the ADS7843 / XPT2046 resistive
//  touch controller on the ITDB02 module.  Replaces ITDB02_Touch.
//
//  The old project read the panel in its native portrait orientation and then
//  patched the result up by hand:
//        touchX = 319 - myTouch.getY();
//        touchY = 239 - myTouch.getX();
//  That swap-and-invert now lives in board_config.h as TOUCH_SWAP_XY /
//  TOUCH_INVERT_X / TOUCH_INVERT_Y, and is applied per rotation.
// ---------------------------------------------------------------------------
#ifndef TOUCH_PANEL_H
#define TOUCH_PANEL_H

#include <Arduino.h>
#include "board_config.h"

class TouchPanel {
public:
    void begin();

    // True while the panel is being pressed (controller pulls IRQ low).
    bool touched();

    // Take a fresh averaged sample.  Returns false if the press went away
    // mid-read or the values look like noise.
    bool read();

    int16_t x() const { return _x; }          // screen coords, current rotation
    int16_t y() const { return _y; }
    uint16_t rawX() const { return _rawX; }   // 12-bit, for calibration
    uint16_t rawY() const { return _rawY; }

    // Screen size the raw values get mapped onto.  Call again after
    // tft.setRotation().
    void setScreenSize(int16_t w, int16_t h) { _w = w; _h = h; }

    // Block until released, so a tap is consumed once and not re-triggered.
    void waitForRelease();
    // Block until a tap happens, or until timeoutMs elapses (0 = forever).
    bool waitForTap(uint32_t timeoutMs = 0);

private:
    uint16_t transfer(uint8_t command);
    void     mapToScreen();

    int16_t  _x = -1, _y = -1;
    uint16_t _rawX = 0, _rawY = 0;
    int16_t  _w = 320, _h = 240;
};

extern TouchPanel touch;

// Override the board_config.h orientation flags at runtime -- needed if you
// drive the display in a rotation other than the default landscape.
void touchSetOrientation(bool swapXY, bool invertX, bool invertY);

#endif // TOUCH_PANEL_H
