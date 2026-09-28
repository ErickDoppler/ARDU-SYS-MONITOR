// ---------------------------------------------------------------------------
//  TftSSD1289 -- minimal, dependency-free driver for an SSD1289 320x240 panel
//  on a 16-bit parallel bus, Arduino MEGA.
//
//  Replaces UTFT.  No external library, no patched headers: every pin and port
//  it touches comes from board_config.h.
//
//  Coordinates are screen coordinates for the current rotation.  Rotation 1
//  (landscape, 320x240) reproduces what UNIT-T6 ran, including the register
//  remapping UTFT did internally.
// ---------------------------------------------------------------------------
#ifndef TFT_SSD1289_H
#define TFT_SSD1289_H

#include <Arduino.h>
#include "board_config.h"
#include "font5x7.h"

// --- RGB565 ---------------------------------------------------------------
static inline uint16_t tftColor(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)((r & 0xF8) << 8) | (uint16_t)((g & 0xFC) << 3) | (b >> 3);
}

#define TFT_BLACK       0x0000
#define TFT_WHITE       0xFFFF
#define TFT_RED         0xF800
#define TFT_GREEN       0x07E0
#define TFT_BLUE        0x001F
#define TFT_CYAN        0x07FF
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_ORANGE      0xFD20
#define TFT_LIME        0x87E0
#define TFT_PINK        0xFC9F
#define TFT_PURPLE      0x780F
#define TFT_NAVY        0x000F
#define TFT_TEAL        0x0410
#define TFT_OLIVE       0x7BE0
#define TFT_MAROON      0x7800
#define TFT_GREY        0x8410
#define TFT_DARKGREY    0x4208
#define TFT_LIGHTGREY   0xC618
#define TFT_BROWN       0xA145
#define TFT_GOLD        0xFEA0
#define TFT_SKYBLUE     0x867D
#define TFT_VIOLET      0x915C

class TftSSD1289 : public Print {
public:
    void     begin(uint8_t rotation = TFT_DEFAULT_ROT);
    void     setRotation(uint8_t r);
    uint8_t  rotation() const { return _rot; }
    int16_t  width()    const { return _w; }
    int16_t  height()   const { return _h; }

    // --- primitives ---
    void fillScreen(uint16_t color);
    void drawPixel(int16_t x, int16_t y, uint16_t color);
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
    void drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
    void fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color);
    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, uint16_t color);
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, uint16_t color);
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint16_t color);
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint16_t color);

    // --- text (Print, so tft.print(...) / tft.println(...) just work) ---
    void setCursor(int16_t x, int16_t y) { _cx = x; _cy = y; }
    int16_t cursorX() const { return _cx; }
    int16_t cursorY() const { return _cy; }
    void setTextColor(uint16_t fg)                { _fg = fg; _opaque = false; }
    void setTextColor(uint16_t fg, uint16_t bg)   { _fg = fg; _bg = bg; _opaque = true; }
    void setTextSize(uint8_t s)                   { _size = s ? s : 1; }
    void setTextWrap(bool on)                     { _wrap = on; }
    void drawChar(int16_t x, int16_t y, char c, uint16_t fg, uint16_t bg,
                  uint8_t size, bool opaque);
    void printAt(int16_t x, int16_t y, const char *s);
    void printAt(int16_t x, int16_t y, const __FlashStringHelper *s);
    int16_t textWidth(const char *s) const;
    virtual size_t write(uint8_t c);

    // --- bulk / image ---
    void setAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
    void pushColor(uint16_t color, uint32_t count);
    void pushPixels(const uint16_t *data, uint16_t len);
    // One screen row of RGB565, left to right; handles the panel's scan order.
    void drawPixelRow(int16_t x, int16_t y, int16_t w, const uint16_t *row);
    bool rowReversed() const { return _rowRev; }

    // --- panel controls ---
    void sleep(bool on);
    void invert(bool on);

private:
    void    hardReset();
    void    writeInit();
    void    writeReg(uint8_t reg, uint16_t value);
    void    writeCmd(uint8_t reg);
    void    mapToNative(int16_t x, int16_t y, uint16_t &nx, uint16_t &ny) const;

    uint8_t  _rot    = TFT_DEFAULT_ROT;
    int16_t  _w      = TFT_NATIVE_H;
    int16_t  _h      = TFT_NATIVE_W;
    bool     _rowRev = true;

    int16_t  _cx = 0, _cy = 0;
    uint16_t _fg = TFT_WHITE, _bg = TFT_BLACK;
    uint8_t  _size = 1;
    bool     _opaque = true;
    bool     _wrap = true;
};

extern TftSSD1289 tft;

#endif // TFT_SSD1289_H
