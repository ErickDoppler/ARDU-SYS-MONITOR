// ---------------------------------------------------------------------------
//  gesture.h -- turns a noisy resistive panel into tap / long-press events.
//
//  A resistive panel's IRQ line is not a clean signal. It chatters on contact,
//  and during a steady hold it drops out briefly whenever finger pressure
//  wavers. Reading it directly gives phantom taps, and -- worse for a long press
//  -- every dropout restarts the timer, so a deliberate three-second hold can
//  never complete.
//
//  Two things fix that:
//
//    Asymmetric debounce. A press is accepted after 30 ms, but a release needs
//    80 ms of continuous quiet. A hold therefore survives the dropouts, while a
//    real lift is still recognised faster than anyone can notice.
//
//    Confirmation by conversion. On accepting a press the controller is actually
//    read. The IRQ can assert from electrical noise; a conversion that yields no
//    usable sample means nothing is touching the glass.
//
//  The long press fires the moment the threshold is crossed, while the finger is
//  still down, rather than on release -- so the screen changes under your finger
//  at three seconds and you know it worked without having to let go and see.
// ---------------------------------------------------------------------------
#ifndef GESTURE_H
#define GESTURE_H

#include <Arduino.h>

#include "TouchPanel.h"

class TouchGesture {
public:
    enum Event : uint8_t { None = 0, Tap, LongPress };

    void begin() {
        m_raw = false;
        m_stable = false;
        m_since = millis();
        m_pressStart = 0;
        m_longFired = false;
    }

    // Call every loop. Returns at most one event per call.
    Event poll() {
        const unsigned long now = millis();
        const bool raw = touch.touched();

        if (raw != m_raw) {
            m_raw = raw;
            m_since = now;  // restart the settling window on any edge
        }

        // Longer settling on release than on press: that asymmetry is what lets
        // a hold ride through a momentary loss of contact.
        const unsigned long settle = m_raw ? kPressDebounceMs : kReleaseDebounceMs;

        if (m_raw != m_stable && (now - m_since) >= settle) {
            if (m_raw) {
                // A conversion with no valid sample means the IRQ fired on noise.
                if (!touch.read()) {
                    m_raw = false;
                    m_since = now;
                    return None;
                }
                m_stable = true;
                m_pressStart = now;
                m_longFired = false;
            } else {
                m_stable = false;
                // A hold that already fired its long press must not also emit a
                // tap when the finger finally comes off.
                if (!m_longFired) return Tap;
            }
        }

        if (m_stable && !m_longFired && (now - m_pressStart) >= kLongPressMs) {
            m_longFired = true;
            return LongPress;
        }
        return None;
    }

    bool held() const { return m_stable; }

    // How long the current press has lasted, for drawing hold feedback.
    unsigned long heldMs() const {
        return m_stable ? (millis() - m_pressStart) : 0;
    }

    static const unsigned long kLongPressMs = 3000;

private:
    static const unsigned long kPressDebounceMs = 30;
    static const unsigned long kReleaseDebounceMs = 80;

    bool m_raw = false;
    bool m_stable = false;
    unsigned long m_since = 0;
    unsigned long m_pressStart = 0;
    bool m_longFired = false;
};

#endif  // GESTURE_H
