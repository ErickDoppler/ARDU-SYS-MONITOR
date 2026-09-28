// ---------------------------------------------------------------------------
//  widgets.h -- the drawing vocabulary the screens are built from.
//
//  Everything here is btop's furniture: titled boxes, meters that colour by
//  value, and history graphs. Graphs keep their own samples in a ring buffer on
//  the device, so the desktop only ever sends current values and a dropped
//  packet costs one pixel rather than the whole history.
// ---------------------------------------------------------------------------
#ifndef WIDGETS_H
#define WIDGETS_H

#include <Arduino.h>

#include "theme.h"

// A fixed-width history of percentages, one byte per sample.
// 0..100, or 0xFF for "no reading", which draws as a gap rather than a zero.
#define GRAPH_NONE 0xFF

class Graph {
public:
    void reset() {
        m_count = 0;
        m_head = 0;
        m_total = 0;
        for (uint16_t i = 0; i < CAPACITY; i++) m_data[i] = GRAPH_NONE;
    }

    void push(uint8_t value) {
        m_data[m_head] = value;
        m_head = (uint16_t)((m_head + 1) % CAPACITY);
        if (m_count < CAPACITY) m_count++;
        m_total++;
    }

    // Total samples ever pushed. Grid lines are anchored to this rather than to a
    // screen column, so they travel left with the data instead of standing still
    // -- which is the only thing that shows the plot is still moving once the
    // buffer has filled and the right-hand edge stops changing shape.
    uint32_t total() const { return m_total; }

    // Index 0 is the oldest sample that will be drawn.
    uint8_t at(uint16_t index) const {
        if (index >= CAPACITY) return GRAPH_NONE;
        const uint16_t start = (uint16_t)((m_head + CAPACITY - m_count) % CAPACITY);
        return m_data[(start + index) % CAPACITY];
    }

    uint16_t count() const { return m_count; }

    // One sample per pixel across the full width of a panel box. The boxes are
    // 312 px wide with a 6 px inset each side, so 300 columns is exactly what a
    // full-width graph draws -- keeping these equal is what stops a graph from
    // filling only part of its box.
    static const uint16_t CAPACITY = 300;

private:
    uint8_t m_data[CAPACITY];
    uint16_t m_head = 0;
    uint16_t m_count = 0;
    uint32_t m_total = 0;
};

// Width of a full-width graph inside a standard 312 px box, and the x it starts
// at. Named so call sites cannot drift from CAPACITY again.
#define GRAPH_FULL_X 10
#define GRAPH_FULL_W 300

namespace ui {

// Screen furniture -----------------------------------------------------------

// A btop-style panel: rounded outline with the title inset into the top edge.
void box(int16_t x, int16_t y, int16_t w, int16_t h, const __FlashStringHelper *title,
         uint16_t outline = C_BOX, uint16_t titleColor = C_TITLE);

// The top bar, in two parts so the link pip can change without repainting the
// title and the position dots.
void headerChrome(const __FlashStringHelper *title, uint8_t index, uint8_t total);
void headerLink(bool linkUp, bool stale);

// Text -----------------------------------------------------------------------

void label(int16_t x, int16_t y, const __FlashStringHelper *text,
           uint16_t color = C_LABEL);

// Right-aligned, which is what makes a column of numbers readable.
void valueRight(int16_t rightX, int16_t y, const char *text, uint16_t color,
                uint8_t size = 1);

// Right-aligned inside a fixed-width field, clearing only that field.
//
// This is the flicker-free way to update a number in place. Opaque text alone is
// not enough: when a value shrinks from "100" to "99" the leftover glyph stays on
// screen, and clearing the whole panel to avoid that is what makes the display
// blink. Clearing one small field instead is invisible.
void valueField(int16_t rightX, int16_t y, int16_t fieldW, const char *text,
                uint16_t color, uint8_t size = 1);

// Formats an integer, or "n/a" for SYSMON_NA, into buf.
void formatValue(char *buf, uint8_t bufLen, int32_t value, const char *suffix);

// Tenths of a unit as "123.4". Used for watts.
void formatTenths(char *buf, uint8_t bufLen, int tenths, const char *suffix);

// A byte count in MiB rendered as MiB or GiB, whichever reads better. Takes a
// 32-bit value: an AVR int tops out at 32767 MiB, which is only 32 GiB.
void formatMib(char *buf, uint8_t bufLen, int32_t mib);

// Meters ---------------------------------------------------------------------

// Horizontal meter. Colour follows the value unless `fixed` is given.
void meter(int16_t x, int16_t y, int16_t w, int16_t h, int percent,
           uint16_t fixed = 0);

// Vertical meter, for the per-core grid.
void meterV(int16_t x, int16_t y, int16_t w, int16_t h, int percent,
            uint16_t fixed = 0);

// Graphs ---------------------------------------------------------------------

// One grid division, in samples. With the renderer pushing one sample per
// second, a line every 20 samples is a line every 20 seconds.
#define GRAPH_GRID_SAMPLES 20

// Gutters a full graph panel reserves for its scale.
#define GRAPH_AXIS_W 26  // left, for value labels
#define GRAPH_AXIS_H 9   // bottom, for time labels

// Filled area graph, newest sample at the right. Colours each column by its own
// value, which is what gives btop's graphs their gradient, and caps each bar with
// two pixels of a brightened shade so the surface of the plot stays legible.
void graph(int16_t x, int16_t y, int16_t w, int16_t h, const Graph &g,
           uint16_t fixed = 0);

// A graph panel is drawn in two halves, for the same reason the screens are:
// its scale does not change from one second to the next, and repainting text
// that has not changed is just a flicker.
//
//   graphAxes()   rules and labels. Belongs in the static pass. For an
//                 auto-scaled graph, call it again only when topValue moves.
//   graphPlot()   bars and grid. Belongs in the per-tick pass.
//
// Both take the WHOLE panel rect and apply the same inset internally, so the two
// can never drift out of alignment.
//
// The grid is STATIC -- anchored to the right-hand edge, so the "now" column is
// always a division and the lines agree with the time labels beneath them. The
// bars travel across it. A grid that scrolled with the data moved in lockstep
// with the bars, which is exactly why nothing appeared to move.

// `topValue` is the reading at full height, `unit` is appended to the labels:
// 100 and "%" for a percentage graph, or the running maximum for a scaled one.
void graphAxes(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t topValue,
               const char *unit);

// `hGridPercent` adds horizontal rules every N percent of full scale (20 for a
// utilisation graph, 0 for none). Ignored on plots too short to carry them.
void graphPlot(int16_t x, int16_t y, int16_t w, int16_t h, const Graph &g,
               uint16_t fixed, uint8_t hGridPercent = 0);

// Shortest plot that can carry horizontal rules without them merging together.
#define GRAPH_HGRID_MIN_H 40

// Single-colour line graph, for overlaying a second series in a shared strip.
void graphLine(int16_t x, int16_t y, int16_t w, int16_t h, const Graph &g,
               uint16_t color);

// Big readouts ----------------------------------------------------------------

// The oversized primary number on a screen, with a small unit after it.
void bigNumber(int16_t x, int16_t y, const char *text, uint16_t color,
               const char *unit = nullptr, uint8_t size = 4);

}  // namespace ui

#endif  // WIDGETS_H
