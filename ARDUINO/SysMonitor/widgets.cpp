#include "widgets.h"

#include <stdio.h>
#include <string.h>

#include "sysmon_wire.h"

namespace ui {

void box(int16_t x, int16_t y, int16_t w, int16_t h, const __FlashStringHelper *title,
         uint16_t outline, uint16_t titleColor) {
    tft.drawRoundRect(x, y, w, h, 4, outline);

    if (!title) return;

    // btop sets the title into the border rather than above it: clear a gap in
    // the top edge and drop the text into it.
    tft.setTextSize(1);
    tft.setTextColor(titleColor, C_BG);

    char buf[24];
    strncpy_P(buf, (PGM_P)title, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    const int16_t textW = (int16_t)strlen(buf) * FONT_ADVANCE;

    tft.drawFastHLine(x + 6, y, textW + 4, C_BG);
    tft.printAt(x + 8, y - 3, buf);
}

void headerChrome(const __FlashStringHelper *title, uint8_t index, uint8_t total) {
    tft.fillRect(0, 0, tft.width(), 14, C_BG);

    tft.setTextSize(1);
    tft.setTextColor(C_TITLE, C_BG);
    tft.printAt(4, 3, title);

    // Position dots, so it is obvious how many screens there are and where you
    // are without a label taking up the bar.
    const int16_t dotsX = tft.width() - 78;
    for (uint8_t i = 0; i < total; i++) {
        const int16_t cx = dotsX + i * 7;
        if (i + 1 == index) {
            tft.fillRect(cx, 5, 5, 5, C_TITLE);
        } else {
            tft.fillRect(cx, 6, 3, 3, C_BOX_HI);
        }
    }

    tft.drawFastHLine(0, 13, tft.width(), C_BOX);
}

void headerLink(bool linkUp, bool stale) {
    const uint16_t linkColor = linkUp ? (stale ? C_MID : C_LINK_OK) : C_LINK_BAD;
    tft.fillRect(tft.width() - 22, 4, 7, 7, linkColor);

    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_BG);
    tft.fillRect(tft.width() - 12, 3, 8, 8, C_BG);
    tft.printAt(tft.width() - 12, 3, linkUp ? (stale ? F("~") : F("+")) : F("-"));
}

void label(int16_t x, int16_t y, const __FlashStringHelper *text, uint16_t color) {
    tft.setTextSize(1);
    tft.setTextColor(color, C_BG);
    tft.printAt(x, y, text);
}

void valueRight(int16_t rightX, int16_t y, const char *text, uint16_t color,
                uint8_t size) {
    tft.setTextSize(size);
    tft.setTextColor(color, C_BG);
    const int16_t w = (int16_t)strlen(text) * FONT_ADVANCE * size;
    tft.printAt(rightX - w, y, text);
}

void valueField(int16_t rightX, int16_t y, int16_t fieldW, const char *text,
                uint16_t color, uint8_t size) {
    const int16_t h = (int16_t)(8 * size);
    tft.fillRect(rightX - fieldW, y, fieldW, h, C_BG);

    tft.setTextSize(size);
    tft.setTextColor(color, C_BG);
    const int16_t w = (int16_t)strlen(text) * FONT_ADVANCE * size;
    tft.printAt(rightX - w, y, text);
}

void formatValue(char *buf, uint8_t bufLen, int32_t value, const char *suffix) {
    if (value == SYSMON_NA) {
        snprintf(buf, bufLen, "n/a");
        return;
    }
    snprintf(buf, bufLen, "%ld%s", (long)value, suffix ? suffix : "");
}

void formatTenths(char *buf, uint8_t bufLen, int tenths, const char *suffix) {
    if (tenths == SYSMON_NA) {
        snprintf(buf, bufLen, "n/a");
        return;
    }
    // avr-libc's snprintf has no float support compiled in by default, so the
    // decimal is assembled from integer parts rather than with %.1f.
    snprintf(buf, bufLen, "%d.%d%s", tenths / 10, abs(tenths % 10), suffix ? suffix : "");
}

void formatMib(char *buf, uint8_t bufLen, int32_t mib) {
    if (mib == SYSMON_NA) {
        snprintf(buf, bufLen, "n/a");
        return;
    }
    if (mib >= 10240) {
        snprintf(buf, bufLen, "%ld.%ldG", (long)(mib / 1024), (long)((mib % 1024) * 10 / 1024));
    } else {
        snprintf(buf, bufLen, "%ldM", (long)mib);
    }
}

void meter(int16_t x, int16_t y, int16_t w, int16_t h, int percent, uint16_t fixed) {
    tft.drawRect(x, y, w, h, C_BOX);

    const int16_t innerW = w - 2;
    tft.fillRect(x + 1, y + 1, innerW, h - 2, C_BG);
    if (percent <= 0) return;
    if (percent > 100) percent = 100;

    const int16_t fillW = (int16_t)((int32_t)innerW * percent / 100);

    if (fixed) {
        tft.fillRect(x + 1, y + 1, fillW, h - 2, fixed);
        return;
    }
    // Colour each column by the value it represents, so the bar itself shows
    // where the reading crossed into amber and red.
    for (int16_t i = 0; i < fillW; i++) {
        const int at = (int)((int32_t)(i + 1) * 100 / innerW);
        tft.drawFastVLine(x + 1 + i, y + 1, h - 2, heatColor(at));
    }
}

void meterV(int16_t x, int16_t y, int16_t w, int16_t h, int percent, uint16_t fixed) {
    tft.fillRect(x, y, w, h, C_BG);
    tft.drawRect(x, y, w, h, C_BOX);

    const int16_t innerH = h - 2;
    if (percent <= 0) return;
    if (percent > 100) percent = 100;

    const int16_t fillH = (int16_t)((int32_t)innerH * percent / 100);
    const uint16_t color = fixed ? fixed : heatColor(percent);
    tft.fillRect(x + 1, y + h - 1 - fillH, w - 2, fillH, color);
}

// Single pass, column by column: background above the bar and the bar itself are
// written in one sweep, so every pixel is touched exactly once.
//
// The obvious version -- fillRect the area, then draw the bars -- writes most
// pixels twice with the background visible in between, and on a panel this slow
// that reads as a distinct flash on every refresh. Redrawing a 300x118 graph is
// ~35k pixels; doing it twice is what you were seeing blink.
// The value shown at a plot column, or GRAPH_NONE where the plot is still empty.
static uint8_t columnValue(const Graph &g, int16_t w, int16_t col) {
    const uint16_t n = g.count();
    // Right-align: newest sample at the right edge, older data scrolling off to
    // the left, which is the direction btop scrolls.
    const int16_t first = (int16_t)w - (int16_t)n;
    const int16_t idx = col - first;
    if (idx < 0 || idx >= (int16_t)n) return GRAPH_NONE;
    return g.at((uint16_t)idx);
}

// Bar height for a stored sample, given the byte that means full height.
static int16_t barHeightFor(uint8_t v, int16_t h, uint8_t fullScale) {
    if (v == GRAPH_NONE || fullScale == 0) return 0;
    int16_t barH = (int16_t)((int32_t)v * h / fullScale);
    if (barH < 1 && v > 0) barH = 1;  // a nonzero reading never vanishes
    if (barH > h) barH = h;
    return barH;
}

// Row for a sample, used by the line series.
static int16_t rowFor(uint8_t v, int16_t y, int16_t h, uint8_t fullScale) {
    const int16_t barH = barHeightFor(v, h, fullScale);
    return y + h - 1 - (barH > 0 ? barH - 1 : 0);
}

// Paints one column's background plus any horizontal rules crossing it. Shared so
// the single and dual plots cannot drift apart on grid appearance.
static void paintColumnBackground(int16_t cx, int16_t y, int16_t h, int16_t w,
                                  int16_t col, int16_t bgH, uint8_t hGridPercent) {
    // Static grid, measured back from the right edge so the newest column is
    // always a division and the lines agree with the time labels below. It does
    // not move with the data: the bars sliding across a fixed reference is what
    // makes the motion legible.
    const bool onGrid = (((w - 1 - col) % GRAPH_GRID_SAMPLES) == 0);
    if (bgH > 0) tft.drawFastVLine(cx, y, bgH, onGrid ? C_GRID : C_BG);

    // Horizontal rules live in the background, so repainting a column erases
    // whatever crossed it -- restore them here, for this column only.
    if (hGridPercent && h >= GRAPH_HGRID_MIN_H) {
        for (uint16_t pct = hGridPercent; pct <= 100;
             pct = (uint16_t)(pct + hGridPercent)) {
            const int16_t rowY = y + h - 1 - (int16_t)((int32_t)pct * (h - 1) / 100);
            if (rowY < y + bgH) tft.drawPixel(cx, rowY, C_GRID);
        }
    }
}

// Draws the filled bar with its brightened two-pixel cap.
static void paintBar(int16_t cx, int16_t top, int16_t barH, uint16_t accent) {
    if (barH <= 0) return;
    const uint16_t cap = capColor(accent);
    const uint16_t body = dimColor(accent, GRAPH_BODY_PERCENT);
    const int16_t capH = (barH >= 3) ? 2 : barH;
    if (barH > capH) tft.drawFastVLine(cx, top + capH, barH - capH, body);
    tft.drawFastVLine(cx, top, capH, cap);
}

void graphDual(int16_t x, int16_t y, int16_t w, int16_t h,
               const Graph &bars, uint16_t barColor,
               const Graph &line, uint16_t lineColor,
               uint8_t fullScale, uint8_t hGridPercent, bool full) {
    for (int16_t col = 0; col < w; col++) {
        const uint8_t vb = columnValue(bars, w, col);
        const uint8_t vl = columnValue(line, w, col);

        if (!full && col > 0) {
            const uint8_t vbL = columnValue(bars, w, col - 1);
            const uint8_t vlL = columnValue(line, w, col - 1);
            // The line segment at a column spans from its left neighbour's row to
            // its own, so it also changes when that neighbour moved -- hence the
            // third term, comparing one column further back than the bars need.
            const uint8_t vlLL = (col > 1) ? columnValue(line, w, col - 2) : GRAPH_NONE;
            if (vb == vbL && vl == vlL && vlL == vlLL) continue;
        }

        const int16_t cx = x + col;
        const int16_t barH = barHeightFor(vb, h, fullScale);
        paintColumnBackground(cx, y, h, w, col, h - barH, hGridPercent);
        paintBar(cx, y + h - barH, barH, barColor);

        // Line last, so it stays readable where it crosses the fill.
        if (vl != GRAPH_NONE) {
            const int16_t cy = rowFor(vl, y, h, fullScale);
            const uint8_t vlL = (col > 0) ? columnValue(line, w, col - 1) : GRAPH_NONE;
            if (vlL != GRAPH_NONE) {
                const int16_t py = rowFor(vlL, y, h, fullScale);
                const int16_t top = (cy < py) ? cy : py;
                const int16_t len = (int16_t)(cy > py ? cy - py : py - cy) + 1;
                tft.drawFastVLine(cx, top, len, lineColor);
            } else {
                tft.drawPixel(cx, cy, lineColor);
            }
        }
    }
}

void graph(int16_t x, int16_t y, int16_t w, int16_t h, const Graph &g, uint16_t fixed,
           uint8_t hGridPercent, bool full, uint8_t fullScale) {
    for (int16_t col = 0; col < w; col++) {
        const uint8_t v = columnValue(g, w, col);

        // --- skip columns whose content did not actually change -------------
        //
        // History advances exactly one sample per render tick, so the plot
        // scrolls left by exactly one pixel: column c now shows what column c+1
        // showed before. Rearranged, column c is unchanged whenever its value
        // equals its LEFT neighbour's -- because that neighbour is what used to
        // be here.
        //
        // So only the edges in the waveform need repainting. An idle graph costs
        // a couple of columns per tick instead of 276, and the grid behind the
        // untouched ones is left alone rather than repainted identically.
        //
        // Valid only when one push happened since the last draw and neither the
        // geometry nor the scale moved; callers pass full = true otherwise.
        // Column 0 always repaints -- what used to be there has scrolled off, so
        // there is nothing left to compare against.
        if (!full && col > 0 && v == columnValue(g, w, col - 1)) continue;

        const int16_t cx = x + col;
        const int16_t barH = barHeightFor(v, h, fullScale);

        paintColumnBackground(cx, y, h, w, col, h - barH, hGridPercent);

        // The nominal accent becomes the CAP, and the body is dimmed from it.
        // Doing it the other way round -- body at the accent, cap brightened --
        // produces no visible cap at all for a colour already at full saturation,
        // which is every accent in this palette.
        paintBar(cx, y + h - barH, barH, fixed ? fixed : heatColor(v));
    }
}

// The separate run-based horizontalRules() pass is gone. It had to run over the
// whole plot to know where bars ended, which defeats a partial repaint; drawing
// each rule inside the column that erased it is both cheaper and correct.
void graphPlot(int16_t x, int16_t y, int16_t w, int16_t h, const Graph &g,
               uint16_t fixed, uint8_t hGridPercent, bool full) {
    const int16_t px = x + GRAPH_AXIS_W;
    const int16_t pw = w - GRAPH_AXIS_W;
    const int16_t ph = h - GRAPH_AXIS_H;
    if (pw <= 0 || ph <= 0) return;

    graph(px, y, pw, ph, g, fixed, hGridPercent, full);
}

void graphAxes(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t topValue,
               const char *unit) {
    const int16_t px = x + GRAPH_AXIS_W;
    const int16_t pw = w - GRAPH_AXIS_W;
    const int16_t ph = h - GRAPH_AXIS_H;
    if (pw <= 0 || ph <= 0) return;

    tft.drawFastVLine(px - 1, y, ph, C_AXIS);
    tft.drawFastHLine(px - 1, y + ph, pw + 1, C_AXIS);

    char buf[12];
    tft.setTextSize(1);
    tft.setTextColor(C_AXIS, C_BG);

    // Value scale: full, half, zero. Three marks is as much as fits without the
    // labels crowding each other on a 60 px tall plot.
    for (uint8_t i = 0; i < 3; i++) {
        const uint32_t value = topValue * (2 - i) / 2;
        const int16_t tickY = y + (int16_t)((int32_t)ph * i / 2);

        int16_t ty = tickY;
        if (i == 0) ty = y;             // keep the top label inside the plot
        if (i == 2) ty = y + ph - 7;    // and the bottom one too

        snprintf(buf, sizeof(buf), "%lu%s", (unsigned long)value, unit ? unit : "");
        const int16_t tw = (int16_t)strlen(buf) * FONT_ADVANCE;

        tft.fillRect(x, ty, GRAPH_AXIS_W - 2, 7, C_BG);
        tft.printAt(px - 3 - tw, ty, buf);
        tft.drawFastHLine(px - 3, tickY, 2, C_AXIS);
    }

    // Time scale, anchored to the right edge because "now" is the one column
    // whose meaning never shifts. One sample per second, so a division is
    // GRAPH_GRID_SAMPLES seconds.
    tft.fillRect(px, y + ph + 1, pw, GRAPH_AXIS_H - 1, C_BG);
    for (int16_t back = 0; back <= pw; back += GRAPH_GRID_SAMPLES * 3) {
        const int16_t tx = px + pw - 1 - back;
        if (tx < px) break;
        tft.drawFastVLine(tx, y + ph + 1, 2, C_AXIS);

        if (back == 0) {
            snprintf(buf, sizeof(buf), "now");
        } else {
            snprintf(buf, sizeof(buf), "-%ds", (int)back);
        }
        const int16_t tw = (int16_t)strlen(buf) * FONT_ADVANCE;
        int16_t lx = tx - tw / 2;
        if (lx < px) lx = px;
        if (lx + tw > px + pw) lx = px + pw - tw;
        tft.printAt(lx, y + ph + 3, buf);
    }
}

// Also single pass. Each column is cleared and drawn in one go, with a vertical
// segment spanning to the previous sample's height so the trace stays connected
// on steep changes instead of breaking into dots.
void graphLine(int16_t x, int16_t y, int16_t w, int16_t h, const Graph &g,
               uint16_t color) {
    const uint16_t n = g.count();
    const int16_t first = (int16_t)w - (int16_t)n;
    int16_t prevY = -1;

    for (int16_t col = 0; col < w; col++) {
        const int16_t cx = x + col;
        const int16_t idx = col - first;

        uint8_t v = GRAPH_NONE;
        if (idx >= 0 && idx < (int16_t)n) v = g.at((uint16_t)idx);

        tft.drawFastVLine(cx, y, h, C_BG);
        if (v == GRAPH_NONE) {
            prevY = -1;  // a gap breaks the line rather than interpolating over it
            continue;
        }

        const int16_t cy = y + h - 1 - (int16_t)((int32_t)v * (h - 1) / 100);
        if (prevY >= 0 && prevY != cy) {
            const int16_t top = (cy < prevY) ? cy : prevY;
            const int16_t len = (int16_t)(cy > prevY ? cy - prevY : prevY - cy) + 1;
            tft.drawFastVLine(cx, top, len, color);
        } else {
            tft.drawPixel(cx, cy, color);
        }
        prevY = cy;
    }
}

void bigNumber(int16_t x, int16_t y, const char *text, uint16_t color, const char *unit,
               uint8_t size) {
    tft.setTextSize(size);
    tft.setTextColor(color, C_BG);
    tft.printAt(x, y, text);

    if (!unit) return;

    const int16_t w = (int16_t)strlen(text) * FONT_ADVANCE * size;
    tft.setTextSize(1);
    tft.setTextColor(C_DIM, C_BG);
    // Sit the unit on the number's baseline rather than its top.
    tft.printAt(x + w + 2, y + (8 * size) - 8, unit);
}

}  // namespace ui
