// ---------------------------------------------------------------------------
//  theme.h -- the btop look, in RGB565.
//
//  btop's identity is not one colour, it is a set of habits: a near-black
//  background, dim rounded boxes whose titles sit in the border itself, and
//  meters that run green -> yellow -> red by value rather than by category. The
//  numbers carry the information; the chrome stays out of the way.
//
//  Colours are picked for a 16-bit panel with a cheap backlight rather than
//  copied from a terminal palette: anything below roughly 25% luminance turns
//  into the same muddy black here, so the "dim" tones are lifted well above
//  where they would sit on a monitor.
// ---------------------------------------------------------------------------
#ifndef THEME_H
#define THEME_H

#include "TftSSD1289.h"
#include "sysmon_wire.h"  // SYSMON_NA, used by the temperature scale below

// --- surfaces -------------------------------------------------------------
//  Contrast note: this panel is a cheap TN with a weak backlight, and anything
//  a monitor would render as a comfortable mid grey disappears on it entirely.
//  btop.s palette leans on dim greys for hierarchy; that does not survive here.
//  So every grey that carries TEXT is 0xDEFB (#DDDDDD) and hierarchy comes from
//  colour and size instead of from brightness.
#define C_BG        0x0861  // near-black with a blue cast, not pure 0
#define C_BOX       0x9CD3  // panel borders        #999999
#define C_BOX_HI    0xBDD7  // emphasised border    #BBBBBB
#define C_TITLE     0x07FF  // box titles: btop.s cyan
#define C_LABEL     0xDEFB  // field labels        #DDDDDD
#define C_TEXT      0xFFFF  // primary readouts, full white so they lead
#define C_DIM       0xDEFB  // secondary text and units  #DDDDDD
#define C_STALE     0x8410  // values no longer being refreshed, visibly muted
#define C_AXIS      0xDEFB  // axis lines and scale labels   #DDDDDD
#define C_GRID      0xAD55  // time grid behind the plot     #AAAAAA

// --- meters, low to high --------------------------------------------------
#define C_LOW       0x2E6A  // calm green
#define C_MID       0xFE60  // amber
#define C_HIGH      0xF9A0  // orange
#define C_CRIT      0xF8A6  // red

// --- per-quantity accents -------------------------------------------------
#define C_CPU       0x07FF  // cyan
#define C_MEM       0xFD20  // orange
#define C_GPU       0xFD20  // orange, per the brief's usage graph
#define C_GPU_FREQ  0x341F  // blue, per the brief's frequency graph
#define C_NET_RX    0x2FEA  // green down
#define C_NET_TX    0xFC10  // pink up
#define C_PWR       0xFFE0  // yellow
// Framerate: bright lime. Its own constant rather than reusing C_LOW, which is
// the calm green the meters heat-colour with -- brightening that would repaint
// every meter on every screen.
//
// Deliberately green-dominant rather than the marginally brighter chartreuse:
// this line is drawn over the yellow power fill, so it has to separate by hue as
// well as by brightness. Luma 206 against the dimmed fill's 163.
#define C_FPS       0x67E6  // #62FF31
#define C_TEMP      0xFB40

// --- link indicator -------------------------------------------------------
#define C_LINK_OK   0x2FEA
#define C_LINK_BAD  0xF800

// Value-driven colour, the way btop tints a meter: green while there is headroom,
// red once there is not. Thresholds are deliberately late -- a CPU at 60% is
// working, not in trouble, and colouring it amber trains you to ignore the
// colour.
inline uint16_t heatColor(int percent) {
    if (percent < 0) return C_DIM;
    if (percent < 55) return C_LOW;
    if (percent < 75) return C_MID;
    if (percent < 90) return C_HIGH;
    return C_CRIT;
}

// Full-brightness version of a colour, hue preserved: #00BB00 becomes #00FF00.
// Used for the two-pixel cap on each history bar, which gives the plot a crisp
// leading edge instead of a flat slab of colour.
inline uint16_t brighten(uint16_t c) {
    // Unpack RGB565 and expand each channel back to a full 8 bits. The low-bit
    // replication matters: without it a 5-bit 0x1F expands to 0xF8, never 0xFF,
    // and the cap comes out slightly dark.
    uint8_t r = (uint8_t)(((c >> 11) & 0x1F) << 3);
    uint8_t g = (uint8_t)(((c >> 5) & 0x3F) << 2);
    uint8_t b = (uint8_t)((c & 0x1F) << 3);
    r = (uint8_t)(r | (r >> 5));
    g = (uint8_t)(g | (g >> 6));
    b = (uint8_t)(b | (b >> 5));

    uint8_t peak = r;
    if (g > peak) peak = g;
    if (b > peak) peak = b;
    if (peak == 0) return 0xFFFF;  // a black bar caps white rather than invisibly

    // Scale so the dominant channel reaches 255. The ratios hold, so the hue does.
    const uint16_t rr = (uint16_t)((uint16_t)r * 255u / peak);
    const uint16_t gg = (uint16_t)((uint16_t)g * 255u / peak);
    const uint16_t bb = (uint16_t)((uint16_t)b * 255u / peak);

    return (uint16_t)(((rr & 0xF8) << 8) | ((gg & 0xFC) << 3) | (bb >> 3));
}

// Scales a colour's brightness, hue preserved. Works on the packed 5/6/5 fields
// directly, which is cheap and accurate enough for a palette shift.
inline uint16_t dimColor(uint16_t c, uint8_t percent) {
    uint16_t r = (uint16_t)((c >> 11) & 0x1F);
    uint16_t g = (uint16_t)((c >> 5) & 0x3F);
    uint16_t b = (uint16_t)(c & 0x1F);
    r = (uint16_t)(r * percent / 100);
    g = (uint16_t)(g * percent / 100);
    b = (uint16_t)(b * percent / 100);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// How much darker a history bar's body is than its cap. Every accent in this
// palette is already near full saturation, so brightening the cap alone achieves
// nothing -- 255 has nowhere to go. Dimming the body is what creates the
// contrast, and it makes the plots calmer to look at as a side effect.
#define GRAPH_BODY_PERCENT 70

// Blends toward white, for lifting a colour that is already fully saturated.
inline uint16_t lightenColor(uint16_t c, uint8_t percent) {
    uint16_t r = (uint16_t)((c >> 11) & 0x1F);
    uint16_t g = (uint16_t)((c >> 5) & 0x3F);
    uint16_t b = (uint16_t)(c & 0x1F);
    r = (uint16_t)(r + (31 - r) * percent / 100);
    g = (uint16_t)(g + (63 - g) * percent / 100);
    b = (uint16_t)(b + (31 - b) * percent / 100);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// The colour for a bar's two-pixel cap.
//
// Saturating first, because that is the pleasing result when there is headroom:
// #00BB00 becomes #00FF00, hue untouched. But when the accent is ALREADY at full
// saturation -- true of most of this palette -- saturating returns it unchanged
// and the cap disappears. Red is the worst case: it carries only 21% of
// luminance, so a red cap over a red body was nearly invisible even after the
// body was dimmed. There, lift toward white instead.
inline uint16_t capColor(uint16_t accent) {
    const uint16_t saturated = brighten(accent);
    if (saturated != accent) return saturated;
    return lightenColor(accent, 35);
}

// Temperature uses its own scale; 80 C is where a desktop part starts throttling.
inline uint16_t tempColor(int c) {
    if (c == SYSMON_NA) return C_DIM;
    if (c < 60) return C_LOW;
    if (c < 75) return C_MID;
    if (c < 85) return C_HIGH;
    return C_CRIT;
}

#endif  // THEME_H
