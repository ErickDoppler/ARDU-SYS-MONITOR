#include "TftSSD1289.h"

TftSSD1289 tft;

// ---------------------------------------------------------------------------
//  bus level
// ---------------------------------------------------------------------------
static inline void busWrite16(uint8_t hi, uint8_t lo) {
    TFT_SET_BUS(hi, lo);
    TFT_WR_STROBE();
}

void TftSSD1289::writeCmd(uint8_t reg) {
    TFT_RS_LOW();                 // index register
    busWrite16(0x00, reg);
    TFT_RS_HIGH();
}

void TftSSD1289::writeReg(uint8_t reg, uint16_t value) {
    writeCmd(reg);
    busWrite16(value >> 8, value & 0xFF);
}

// ---------------------------------------------------------------------------
//  bring-up
// ---------------------------------------------------------------------------
void TftSSD1289::hardReset() {
    TFT_CS_HIGH();
    TFT_RST_HIGH();
    delay(5);
    TFT_RST_LOW();
    delay(15);
    TFT_RST_HIGH();
    delay(15);
}

// SSD1289 power-on sequence.  Same register values UTFT shipped for ITDB32S --
// this panel is fussy about them, so they are reproduced verbatim.
void TftSSD1289::writeInit() {
    writeReg(0x00, 0x0001);   // oscillator on
    writeReg(0x03, 0xA8A4);   // power control 1
    writeReg(0x0C, 0x0000);   // power control 2
    writeReg(0x0D, 0x080C);   // power control 3
    writeReg(0x0E, 0x2B00);   // power control 4
    writeReg(0x1E, 0x00B7);   // power control 5
    writeReg(0x01, 0x2B3F);   // driver output: 320 lines, RL + REV + TB
    writeReg(0x02, 0x0600);   // LCD drive AC
    writeReg(0x10, 0x0000);   // sleep off
    writeReg(0x11, 0x6070);   // entry mode: 65k colour, x then y, increment
    writeReg(0x05, 0x0000);
    writeReg(0x06, 0x0000);
    writeReg(0x16, 0xEF1C);   // horizontal porch
    writeReg(0x17, 0x0003);   // vertical porch
    writeReg(0x07, 0x0233);   // display control: display ON
    writeReg(0x0B, 0x0000);
    writeReg(0x0F, 0x0000);   // gate scan position
    writeReg(0x41, 0x0000);
    writeReg(0x42, 0x0000);
    writeReg(0x48, 0x0000);
    writeReg(0x49, 0x013F);
    writeReg(0x4A, 0x0000);
    writeReg(0x4B, 0x0000);
    writeReg(0x44, 0xEF00);   // window x: 0..239
    writeReg(0x45, 0x0000);   // window y start
    writeReg(0x46, 0x013F);   // window y end = 319
    writeReg(0x30, 0x0707);   // gamma
    writeReg(0x31, 0x0204);
    writeReg(0x32, 0x0204);
    writeReg(0x33, 0x0502);
    writeReg(0x34, 0x0507);
    writeReg(0x35, 0x0204);
    writeReg(0x36, 0x0204);
    writeReg(0x37, 0x0502);
    writeReg(0x3A, 0x0302);
    writeReg(0x3B, 0x0302);
    writeReg(0x23, 0x0000);
    writeReg(0x24, 0x0000);
    writeReg(0x25, 0x8000);   // frame frequency
    writeReg(0x4F, 0x0000);   // GRAM cursor y
    writeReg(0x4E, 0x0000);   // GRAM cursor x
    writeCmd(0x22);           // ready for pixel data
}

void TftSSD1289::begin(uint8_t rotation) {
    TFT_BUS_OUTPUT();
    TFT_CTRL_OUTPUT();
    TFT_WR_HIGH();        // WR idles high; data latches on its rising edge
    TFT_RS_HIGH();
    TFT_CS_HIGH();

    hardReset();

    TFT_CS_LOW();
    writeInit();
    TFT_CS_HIGH();

    setRotation(rotation);
    setTextColor(TFT_WHITE, TFT_BLACK);
    setCursor(0, 0);
    fillScreen(TFT_BLACK);
}

void TftSSD1289::setRotation(uint8_t r) {
    _rot = r & 3;
    if (_rot & 1) { _w = TFT_NATIVE_H; _h = TFT_NATIVE_W; }  // landscape 320x240
    else          { _w = TFT_NATIVE_W; _h = TFT_NATIVE_H; }  // portrait  240x320
    // The panel always scans along native x.  For rotations 0 and 3 that runs
    // left-to-right across a screen row; for 1 and 2 it runs right-to-left.
    _rowRev = (_rot == 1 || _rot == 2);
}

void TftSSD1289::sleep(bool on) {
    TFT_CS_LOW();
    writeReg(0x10, on ? 0x0001 : 0x0000);
    writeReg(0x07, on ? 0x0000 : 0x0233);
    TFT_CS_HIGH();
}

void TftSSD1289::invert(bool on) {
    TFT_CS_LOW();
    writeReg(0x07, on ? 0x0033 : 0x0233);
    TFT_CS_HIGH();
}

// ---------------------------------------------------------------------------
//  addressing
// ---------------------------------------------------------------------------
void TftSSD1289::mapToNative(int16_t x, int16_t y, uint16_t &nx, uint16_t &ny) const {
    switch (_rot) {
        case 0:  nx = x;                     ny = y;                     break;
        case 1:  nx = y;                     ny = TFT_NATIVE_H - 1 - x;  break;
        case 2:  nx = TFT_NATIVE_W - 1 - x;  ny = TFT_NATIVE_H - 1 - y;  break;
        default: nx = TFT_NATIVE_W - 1 - y;  ny = x;                     break;
    }
}

void TftSSD1289::setAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
    uint16_t ax, ay, bx, by;
    mapToNative(x0, y0, ax, ay);
    mapToNative(x1, y1, bx, by);

    uint16_t nx0 = ax < bx ? ax : bx, nx1 = ax < bx ? bx : ax;
    uint16_t ny0 = ay < by ? ay : by, ny1 = ay < by ? by : ay;

    writeReg(0x44, (uint16_t)(nx1 << 8) | nx0);   // x window, packed as end:start
    writeReg(0x45, ny0);                          // y start
    writeReg(0x46, ny1);                          // y end
    writeReg(0x4E, nx0);                          // cursor x
    writeReg(0x4F, ny0);                          // cursor y
    writeCmd(0x22);                               // write to GRAM
}

void TftSSD1289::pushColor(uint16_t color, uint32_t count) {
    // The bus holds its value between strobes, so a solid run only needs the
    // write pulse repeated -- about 6 cycles per pixel.
    TFT_SET_BUS(color >> 8, color & 0xFF);
    while (count--) TFT_WR_STROBE();
}

void TftSSD1289::pushPixels(const uint16_t *data, uint16_t len) {
    while (len--) {
        uint16_t c = *data++;
        busWrite16(c >> 8, c & 0xFF);
    }
}

void TftSSD1289::drawPixelRow(int16_t x, int16_t y, int16_t w, const uint16_t *row) {
    if (w <= 0 || y < 0 || y >= _h) return;
    if (x < 0) { row -= x; w += x; x = 0; }
    if (x + w > _w) w = _w - x;
    if (w <= 0) return;

    TFT_CS_LOW();
    setAddrWindow(x, y, x + w - 1, y);
    if (_rowRev) {
        const uint16_t *p = row + w - 1;
        while (w--) { uint16_t c = *p--; busWrite16(c >> 8, c & 0xFF); }
    } else {
        pushPixels(row, (uint16_t)w);
    }
    TFT_CS_HIGH();
}

// ---------------------------------------------------------------------------
//  primitives
// ---------------------------------------------------------------------------
void TftSSD1289::fillScreen(uint16_t color) {
    TFT_CS_LOW();
    setAddrWindow(0, 0, _w - 1, _h - 1);
    pushColor(color, (uint32_t)TFT_NATIVE_W * TFT_NATIVE_H);
    TFT_CS_HIGH();
}

void TftSSD1289::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= _w || y >= _h) return;
    TFT_CS_LOW();
    setAddrWindow(x, y, x, y);
    busWrite16(color >> 8, color & 0xFF);
    TFT_CS_HIGH();
}

void TftSSD1289::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= _w || y >= _h) return;
    if (x + w > _w) w = _w - x;
    if (y + h > _h) h = _h - y;
    if (w <= 0 || h <= 0) return;

    TFT_CS_LOW();
    setAddrWindow(x, y, x + w - 1, y + h - 1);
    pushColor(color, (uint32_t)w * (uint32_t)h);
    TFT_CS_HIGH();
}

void TftSSD1289::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    fillRect(x, y, w, 1, color);
}

void TftSSD1289::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
    fillRect(x, y, 1, h, color);
}

void TftSSD1289::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void TftSSD1289::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    if (y0 == y1) { drawFastHLine(x0 < x1 ? x0 : x1, y0, abs(x1 - x0) + 1, color); return; }
    if (x0 == x1) { drawFastVLine(x0, y0 < y1 ? y0 : y1, abs(y1 - y0) + 1, color); return; }

    int16_t dx = abs(x1 - x0), dy = abs(y1 - y0);
    int16_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int16_t err = dx - dy;
    for (;;) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = err << 1;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void TftSSD1289::drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    if (r <= 0) return;
    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
    drawPixel(cx, cy + r, color);
    drawPixel(cx, cy - r, color);
    drawPixel(cx + r, cy, color);
    drawPixel(cx - r, cy, color);
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        drawPixel(cx + x, cy + y, color); drawPixel(cx - x, cy + y, color);
        drawPixel(cx + x, cy - y, color); drawPixel(cx - x, cy - y, color);
        drawPixel(cx + y, cy + x, color); drawPixel(cx - y, cy + x, color);
        drawPixel(cx + y, cy - x, color); drawPixel(cx - y, cy - x, color);
    }
}

void TftSSD1289::fillCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
    if (r <= 0) return;
    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r;
    drawFastVLine(cx, cy - r, 2 * r + 1, color);
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        drawFastVLine(cx + x, cy - y, 2 * y + 1, color);
        drawFastVLine(cx - x, cy - y, 2 * y + 1, color);
        drawFastVLine(cx + y, cy - x, 2 * x + 1, color);
        drawFastVLine(cx - y, cy - x, 2 * x + 1, color);
    }
}

void TftSSD1289::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                              int16_t x2, int16_t y2, uint16_t color) {
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
}

void TftSSD1289::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                              int16_t x2, int16_t y2, uint16_t color) {
    int16_t t;
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }

    if (y0 == y2) {                       // degenerate: collapses to one line
        int16_t a = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        int16_t b = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        drawFastHLine(a, y0, b - a + 1, color);
        return;
    }

    int16_t dx01 = x1 - x0, dy01 = y1 - y0;
    int16_t dx02 = x2 - x0, dy02 = y2 - y0;
    int16_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;

    int16_t last = (y1 == y2) ? y1 : y1 - 1;
    int16_t y;
    for (y = y0; y <= last; y++) {
        int16_t a = x0 + sa / dy01;
        int16_t b = x0 + sb / dy02;
        sa += dx01; sb += dx02;
        if (a > b) { t = a; a = b; b = t; }
        drawFastHLine(a, y, b - a + 1, color);
    }
    sa = (int32_t)dx12 * (y - y1);
    sb = (int32_t)dx02 * (y - y0);
    for (; y <= y2; y++) {
        int16_t a = x1 + sa / dy12;
        int16_t b = x0 + sb / dy02;
        sa += dx12; sb += dx02;
        if (a > b) { t = a; a = b; b = t; }
        drawFastHLine(a, y, b - a + 1, color);
    }
}

void TftSSD1289::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t r, uint16_t color) {
    int16_t maxR = ((w < h ? w : h) - 1) / 2;
    if (r > maxR) r = maxR;
    if (r <= 0) { drawRect(x, y, w, h, color); return; }

    drawFastHLine(x + r, y, w - 2 * r, color);
    drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
    drawFastVLine(x, y + r, h - 2 * r, color);
    drawFastVLine(x + w - 1, y + r, h - 2 * r, color);

    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, px = 0, py = r;
    while (px < py) {
        if (f >= 0) { py--; ddF_y += 2; f += ddF_y; }
        px++; ddF_x += 2; f += ddF_x;
        drawPixel(x + w - 1 - r + px, y + h - 1 - r + py, color);
        drawPixel(x + r - px,         y + h - 1 - r + py, color);
        drawPixel(x + w - 1 - r + py, y + h - 1 - r + px, color);
        drawPixel(x + r - py,         y + h - 1 - r + px, color);
        drawPixel(x + w - 1 - r + px, y + r - py, color);
        drawPixel(x + r - px,         y + r - py, color);
        drawPixel(x + w - 1 - r + py, y + r - px, color);
        drawPixel(x + r - py,         y + r - px, color);
    }
}

void TftSSD1289::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t r, uint16_t color) {
    int16_t maxR = ((w < h ? w : h) - 1) / 2;
    if (r > maxR) r = maxR;
    if (r <= 0) { fillRect(x, y, w, h, color); return; }

    fillRect(x + r, y, w - 2 * r, h, color);

    int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, px = 0, py = r;
    while (px < py) {
        if (f >= 0) { py--; ddF_y += 2; f += ddF_y; }
        px++; ddF_x += 2; f += ddF_x;
        drawFastVLine(x + w - 1 - r + px, y + r - py, h - 2 * r + 2 * py - 1, color);
        drawFastVLine(x + r - px,         y + r - py, h - 2 * r + 2 * py - 1, color);
        drawFastVLine(x + w - 1 - r + py, y + r - px, h - 2 * r + 2 * px - 1, color);
        drawFastVLine(x + r - py,         y + r - px, h - 2 * r + 2 * px - 1, color);
    }
}

// ---------------------------------------------------------------------------
//  text
// ---------------------------------------------------------------------------
void TftSSD1289::drawChar(int16_t x, int16_t y, char c, uint16_t fg, uint16_t bg,
                          uint8_t size, bool opaque) {
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    const uint8_t *glyph = &Font5x7[(uint8_t)(c - FONT_FIRST) * FONT_W];

    for (uint8_t col = 0; col < FONT_ADVANCE; col++) {
        uint8_t bits = (col < FONT_W) ? pgm_read_byte(glyph + col) : 0x00;
        // Emit vertical runs instead of single pixels: one register setup per
        // run rather than per pixel, which matters a lot on a parallel bus.
        uint8_t row = 0;
        while (row < 8) {
            bool on = bits & (1 << row);
            uint8_t run = 1;
            while (row + run < 8 && (bool)(bits & (1 << (row + run))) == on) run++;
            if (on || opaque) {
                uint16_t c16 = on ? fg : bg;
                if (size == 1) drawFastVLine(x + col, y + row, run, c16);
                else fillRect(x + col * size, y + row * size, size, run * size, c16);
            }
            row += run;
        }
    }
}

size_t TftSSD1289::write(uint8_t c) {
    int16_t advance = FONT_ADVANCE * _size;
    int16_t lineH   = 8 * _size;

    if (c == '\r') { _cx = 0; return 1; }
    if (c == '\n') { _cx = 0; _cy += lineH; return 1; }

    if (_wrap && (_cx + advance) > _w) { _cx = 0; _cy += lineH; }
    if (_cy >= _h) return 1;

    drawChar(_cx, _cy, (char)c, _fg, _bg, _size, _opaque);
    _cx += advance;
    return 1;
}

void TftSSD1289::printAt(int16_t x, int16_t y, const char *s) {
    setCursor(x, y);
    print(s);
}

void TftSSD1289::printAt(int16_t x, int16_t y, const __FlashStringHelper *s) {
    setCursor(x, y);
    print(s);
}

int16_t TftSSD1289::textWidth(const char *s) const {
    int16_t n = 0;
    while (*s++) n++;
    return n * FONT_ADVANCE * _size;
}
