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
void graph(int16_t x, int16_t y, int16_t w, int16_t h, const Graph &g, uint16_t fixed) {
    const uint16_t n = g.count();

    // Right-align: newest sample at the right edge, older data scrolling off to
    // the left, which is the direction btop scrolls.
    const int16_t first = (int16_t)w - (int16_t)n;

    for (int16_t col = 0; col < w; col++) {
        const int16_t cx = x + col;
        const int16_t idx = col - first;

        // Static grid, measured back from the right edge so the newest column is
        // always a division and the lines agree with the time labels below. It
        // does not move with the data: the bars sliding across a fixed reference
        // is what makes the motion legible.
        const bool onGrid = (((w - 1 - col) % GRAPH_GRID_SAMPLES) == 0);

        uint8_t v = GRAPH_NONE;
        if (idx >= 0 && idx < (int16_t)n) v = g.at((uint16_t)idx);

        int16_t barH = 0;
        if (v != GRAPH_NONE) {
            barH = (int16_t)((int32_t)v * h / 100);
            if (barH < 1 && v > 0) barH = 1;  // a nonzero reading never vanishes
            if (barH > h) barH = h;
        }

        // Background above the bar. A grid column simply uses the grid colour,
        // so the time grid costs nothing beyond the fill that happens anyway.
        const int16_t bgH = h - barH;
        if (bgH > 0) tft.drawFastVLine(cx, y, bgH, onGrid ? C_GRID : C_BG);

        if (barH > 0) {
            // The nominal accent becomes the CAP, and the body is dimmed from it.
            // Doing it the other way round -- body at the accent, cap brightened
            // -- produces no visible cap at all for a colour already at full
            // saturation, which is every accent in this palette. Deriving both
            // ends from one accent also means a new graph colour cannot be added
            // without its cap working.
            const uint16_t accent = fixed ? fixed : heatColor(v);
            const uint16_t cap = capColor(accent);
            const uint16_t body = dimColor(accent, GRAPH_BODY_PERCENT);
            const int16_t top = y + bgH;

            const int16_t capH = (barH >= 3) ? 2 : barH;
            if (barH > capH) tft.drawFastVLine(cx, top + capH, barH - capH, body);
            tft.drawFastVLine(cx, top, capH, cap);
        }
    }
}

// Horizontal rules at fixed fractions of full scale, drawn after the bars and
// only across the exposed background above them.
//
// Emitted as runs rather than per pixel: a rule crossing a 276 px plot would
// otherwise be 276 separate one-pixel writes, each paying a full address-window
// setup. Runs collapse that to a handful of spans, which is the difference
// between ~14 ms and well under 1 ms per rule.
static void horizontalRules(int16_t x, int16_t y, int16_t w, int16_t h,
                            const Graph &g, uint8_t stepPercent) {
    if (stepPercent == 0 || h < GRAPH_HGRID_MIN_H) return;

    const uint16_t n = g.count();
    const int16_t first = (int16_t)w - (int16_t)n;

    // Inclusive of 100: the full-scale rule is the one that says where the
    // ceiling is, and without it a graph pinned near the top has no reference.
    // It lands on the plot.s top row, and is naturally hidden wherever a bar
    // actually reaches full height.
    for (uint16_t pct = stepPercent; pct <= 100; pct = (uint16_t)(pct + stepPercent)) {
        const int16_t rowY = y + h - 1 - (int16_t)((int32_t)pct * (h - 1) / 100);
        int16_t runStart = -1;

        // One extra iteration so a run reaching the right edge still gets closed.
        for (int16_t col = 0; col <= w; col++) {
            bool exposed = false;
            if (col < w) {
                const int16_t idx = col - first;
                uint8_t v = GRAPH_NONE;
                if (idx >= 0 && idx < (int16_t)n) v = g.at((uint16_t)idx);

                int16_t barH = 0;
                if (v != GRAPH_NONE) {
                    barH = (int16_t)((int32_t)v * h / 100);
                    if (barH < 1 && v > 0) barH = 1;
                    if (barH > h) barH = h;
                }
                exposed = (rowY < y + h - barH);
            }

            if (exposed) {
                if (runStart < 0) runStart = col;
            } else if (runStart >= 0) {
                tft.drawFastHLine(x + runStart, rowY, col - runStart, C_GRID);
                runStart = -1;
            }
        }
    }
}

void graphPlot(int16_t x, int16_t y, int16_t w, int16_t h, const Graph &g,
               uint16_t fixed, uint8_t hGridPercent) {
    const int16_t px = x + GRAPH_AXIS_W;
    const int16_t pw = w - GRAPH_AXIS_W;
    const int16_t ph = h - GRAPH_AXIS_H;
    if (pw <= 0 || ph <= 0) return;

    graph(px, y, pw, ph, g, fixed);
    horizontalRules(px, y, pw, ph, g, hGridPercent);
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
