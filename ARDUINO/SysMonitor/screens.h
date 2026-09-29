// ---------------------------------------------------------------------------
//  screens.h -- one layout per display, split into chrome and live values.
//
//  Drawing is deliberately in two halves:
//
//    drawStatic()   boxes, titles, labels. Everything that cannot change while a
//                   screen is on show. Painted once, on a screen change.
//    drawLive()     the numbers, meters and graphs. Painted on the render tick,
//                   and only for fields whose value actually moved.
//
//  That split is what removes the blink. Repainting the whole panel meant
//  clearing 76800 pixels and redrawing every label two or three times a second;
//  now a refresh touches a few hundred pixels unless a graph advanced.
//
//  History lives here too, in ring buffers, so the wire carries only current
//  values and a dropped packet costs one pixel rather than the whole graph.
// ---------------------------------------------------------------------------
#ifndef SCREENS_H
#define SCREENS_H

#include <Arduino.h>

#include "link.h"
#include "widgets.h"

// Firmware-local screen id. Deliberately outside the SCR_1..7 range so it never
// enters the tap rotation and is never confused with a wire dataset id -- the
// desktop has no idea this screen exists.
#define SCR_DIAG 200

namespace screens {

// Advances the diagnostics timing graph by one bucket. Called every render tick
// regardless of which screen is showing, so opening diagnostics reveals history
// that was already being collected rather than starting from an empty plot.
void recordDiag(uint16_t maxLen, uint8_t count);

// Feeds the ring buffers. Called once per published dataset, so one sample is
// one pixel of graph regardless of which screen is being watched.
void record(uint8_t screenId, const SysData &d);

// Chrome only. Clears the panel and paints everything that will not change.
void drawStatic(uint8_t screenId);

// Live values. Redraws only what moved since the last call, unless `force`.
void drawLive(uint8_t screenId, const SysData &d, bool force);

// The link pip in the header, which changes independently of the data.
void drawLinkState(bool linkUp, bool stale);

// Forgets what is currently painted, so the next drawLive() repaints in full.
// Called after drawStatic(), since the chrome wipe takes the old values with it.
void invalidate();

const __FlashStringHelper *name(uint8_t screenId);

void resetHistory();

// Clears the per-screen history but deliberately KEEPS the diagnostics timing
// graph. Called when the link drops: the measurement graphs are showing data
// that is no longer arriving, whereas the diagnostics graph is showing the
// outage itself, which is the one thing worth looking at at that moment.
void clearDataHistory();

}  // namespace screens

#endif  // SCREENS_H
