#include "TouchPanel.h"

TouchPanel touch;

// Orientation flags start from board_config.h and can be overridden at runtime
// if you change tft.setRotation() away from the default landscape.
static bool s_swapXY  = TOUCH_SWAP_XY;
static bool s_invertX = TOUCH_INVERT_X;
static bool s_invertY = TOUCH_INVERT_Y;

void touchSetOrientation(bool swapXY, bool invertX, bool invertY) {
    s_swapXY = swapXY; s_invertX = invertX; s_invertY = invertY;
}

void TouchPanel::begin() {
    pinMode(TOUCH_PIN_CLK, OUTPUT);
    pinMode(TOUCH_PIN_CS,  OUTPUT);
    pinMode(TOUCH_PIN_DIN, OUTPUT);
    pinMode(TOUCH_PIN_DOUT, INPUT);
    pinMode(TOUCH_PIN_IRQ,  INPUT_PULLUP);

    // Idle state as ITDB02_Touch left it.
    digitalWrite(TOUCH_PIN_CS, HIGH);
    digitalWrite(TOUCH_PIN_CLK, HIGH);
    digitalWrite(TOUCH_PIN_DIN, HIGH);
}

bool TouchPanel::touched() {
    return digitalRead(TOUCH_PIN_IRQ) == LOW;
}

// One ADS7843 conversion: 8-bit command out, then 12 result bits in.
// Bit timing follows ITDB02_Touch exactly -- see HARDWARE.md.
uint16_t TouchPanel::transfer(uint8_t command) {
    // Command out, MSB first; the controller latches DIN on the rising edge.
    digitalWrite(TOUCH_PIN_CLK, LOW);
    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(TOUCH_PIN_DIN, (command & 0x80) ? HIGH : LOW);
        command <<= 1;
        digitalWrite(TOUCH_PIN_CLK, LOW);
        digitalWrite(TOUCH_PIN_CLK, HIGH);
    }

    // One idle clock, covering the controller's busy bit.
    digitalWrite(TOUCH_PIN_CLK, HIGH);
    digitalWrite(TOUCH_PIN_CLK, LOW);

    // 12 result bits, MSB first. The ADS7843 shifts out on the FALLING edge,
    // so sample after CLK goes low. Sampling on the rising edge instead still
    // returns plausible-looking numbers -- shifted by one bit.
    uint16_t value = 0;
    for (uint8_t i = 0; i < 12; i++) {
        value <<= 1;
        digitalWrite(TOUCH_PIN_CLK, HIGH);
        digitalWrite(TOUCH_PIN_CLK, LOW);
        if (digitalRead(TOUCH_PIN_DOUT)) value |= 1;
    }
    return value;
}

bool TouchPanel::read() {
    uint32_t sumX = 0, sumY = 0;
    uint8_t  good = 0;

    digitalWrite(TOUCH_PIN_CS, LOW);
    for (uint8_t i = 0; i < TOUCH_SAMPLES; i++) {
        // Channel assignment matters and is easy to invert: ITDB02_Touch took
        // its TP_Y from 0x90 and its TP_X from 0xD0, which is also the ADS7843
        // convention. Swapping these two lines silently rotates the panel.
        uint16_t y = transfer(0x90);     // Y channel
        uint16_t x = transfer(0xD0);     // X channel
        // Rails mean "not actually pressed"; drop those samples.
        if (x > 16 && x < 4080 && y > 16 && y < 4080) {
            sumX += x; sumY += y; good++;
        }
    }
    digitalWrite(TOUCH_PIN_CS, HIGH);

    if (!good) { _x = _y = -1; _rawX = _rawY = 0; return false; }

    _rawX = sumX / good;
    _rawY = sumY / good;
    mapToScreen();
    return true;
}

void TouchPanel::mapToScreen() {
    long u = constrain((long)_rawX, (long)TOUCH_RAW_X_MIN, (long)TOUCH_RAW_X_MAX);
    long v = constrain((long)_rawY, (long)TOUCH_RAW_Y_MIN, (long)TOUCH_RAW_Y_MAX);

    // Normalise each axis to 0..1000 first, so the calibration numbers stay
    // independent of which screen rotation is active.
    long su = (u - TOUCH_RAW_X_MIN) * 1000L / (TOUCH_RAW_X_MAX - TOUCH_RAW_X_MIN);
    long sv = (v - TOUCH_RAW_Y_MIN) * 1000L / (TOUCH_RAW_Y_MAX - TOUCH_RAW_Y_MIN);

    long fx = s_swapXY ? sv : su;
    long fy = s_swapXY ? su : sv;
    if (s_invertX) fx = 1000 - fx;
    if (s_invertY) fy = 1000 - fy;

    _x = (int16_t)(fx * (_w - 1) / 1000);
    _y = (int16_t)(fy * (_h - 1) / 1000);
}

void TouchPanel::waitForRelease() {
    while (touched()) delay(10);
    delay(30);                          // debounce
}

bool TouchPanel::waitForTap(uint32_t timeoutMs) {
    uint32_t start = millis();
    while (!touched()) {
        if (timeoutMs && (millis() - start) >= timeoutMs) return false;
        delay(5);
    }
    read();
    waitForRelease();
    return true;
}
