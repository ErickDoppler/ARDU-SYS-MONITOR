// ---------------------------------------------------------------------------
//  board_config.h  --  ALL hardware knowledge for the ARDU-DISPLAY-UNIT lives
//                      here.  Nothing else in the project touches a pin.
//
//  Board:    Arduino MEGA (ATmega1280 / ATmega2560)
//  Shield:   ITDB02 MEGA Shield  +  ITDB02-3.2S display module
//  Panel:    320x240 TFT, SSD1289 controller, 16-bit parallel bus
//  Touch:    ADS7843 / XPT2046 compatible, bit-banged SPI
//  SD card:  hardware SPI on the Mega's 50/51/52, chip select 53
//
//  Config recovered from the old UNIT-T6 project (UTFT + tinyFAT +
//  ITDB02_Touch).  See ../HARDWARE.md for the full provenance of every value.
// ---------------------------------------------------------------------------
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <Arduino.h>

#if !defined(__AVR_ATmega1280__) && !defined(__AVR_ATmega2560__)
#  error "This board_config.h describes an Arduino MEGA (ATmega1280/2560)."
#endif

// ===========================================================================
//  1. TFT  --  16-bit parallel bus
// ===========================================================================
//  Original code:  UTFT myGLCD(ITDB32S, 38, 39, 40, 41);
//                                        RS  WR  CS  RST
//
//  On the MEGA the 16 data lines are not free to choose -- the shield wires
//  them straight onto two whole ports:
//
//     shield DB8..DB15  ->  Arduino 22..29  ->  PORTA  (high byte)
//     shield DB0..DB7   ->  Arduino 30..37  ->  PORTC  (low byte)
//
//  Pin numbers below are documentation / pinMode only; the fast paths use the
//  port macros underneath them.

#define TFT_PIN_RS        38        // PD7   register select (0 = index, 1 = data)
#define TFT_PIN_WR        39        // PG2   write strobe, active low
#define TFT_PIN_CS        40        // PG1   chip select, active low
#define TFT_PIN_RST       41        // PG0   reset, active low
#define TFT_PIN_DATA_LO   22        // 22..29 -> PORTA
#define TFT_PIN_DATA_HI   30        // 30..37 -> PORTC

// --- data bus -------------------------------------------------------------
//  Which port carries which half of the 16-bit word is the one thing the old
//  project could not prove: UTFT writes raw ports and never records the
//  shield's silkscreen order.
//
//  Get this wrong and you do NOT get wrong colours -- you get nothing at all.
//  Commands go out as 0x00:reg, so a reversed pair makes the controller read
//  reg:0x00, every init register write lands somewhere bogus, and the panel
//  stays blank white.
//
//  Also note pins 30..37 map to PC7 down to PC0, so bit order within a port
//  can need flipping too, depending on how the shield routes DB0..DB7.
//
//  Diagnostics/BusProbe walks all four combinations and tells you which one
//  this board wants.
#define TFT_BUS_SWAPPED   0    // 1 = PORTC carries the high byte
#define TFT_BUS_REVERSED  0    // 1 = flip bit order within each port

#if TFT_BUS_SWAPPED
#  define TFT_PORT_HIGH   PORTC
#  define TFT_PORT_LOW    PORTA
#  define TFT_DDR_HIGH    DDRC
#  define TFT_DDR_LOW     DDRA
#else
#  define TFT_PORT_HIGH   PORTA
#  define TFT_PORT_LOW    PORTC
#  define TFT_DDR_HIGH    DDRA
#  define TFT_DDR_LOW     DDRC
#endif

#if TFT_BUS_REVERSED
static inline uint8_t tftBusLane(uint8_t v) {
    v = (uint8_t)((v >> 4) | (v << 4));
    v = (uint8_t)(((v & 0xCC) >> 2) | ((v & 0x33) << 2));
    v = (uint8_t)(((v & 0xAA) >> 1) | ((v & 0x55) << 1));
    return v;
}
#  define TFT_LANE(v)  tftBusLane(v)
#else
#  define TFT_LANE(v)  (v)
#endif

#define TFT_BUS_OUTPUT()      do { TFT_DDR_HIGH = 0xFF; TFT_DDR_LOW = 0xFF; } while (0)
#define TFT_SET_BUS(hi, lo)   do { TFT_PORT_HIGH = TFT_LANE(hi);            \
                                   TFT_PORT_LOW  = TFT_LANE(lo); } while (0)

// --- control lines --------------------------------------------------------
#define TFT_CTRL_OUTPUT()  do {                                  \
        DDRD |= _BV(7);                                          \
        DDRG |= _BV(2) | _BV(1) | _BV(0);                        \
    } while (0)

#define TFT_RS_LOW()    do { PORTD &= ~_BV(7); } while (0)
#define TFT_RS_HIGH()   do { PORTD |=  _BV(7); } while (0)
#define TFT_CS_LOW()    do { PORTG &= ~_BV(1); } while (0)
#define TFT_CS_HIGH()   do { PORTG |=  _BV(1); } while (0)
#define TFT_RST_LOW()   do { PORTG &= ~_BV(0); } while (0)
#define TFT_RST_HIGH()  do { PORTG |=  _BV(0); } while (0)

#define TFT_WR_HIGH()   do { PORTG |=  _BV(2); } while (0)

// --- write strobe width ---------------------------------------------------
//  This panel needs a MUCH wider WR pulse than the SSD1289 datasheet's 50 ns.
//  Measured on this unit with Diagnostics/StrobeTest, at 16 MHz:
//
//      padding   WR low      result
//      0 nops    ~125 ns     fails   (a handful of pixels latch, rest lost)
//      1 nop     ~190 ns     fails
//      2 nops    ~250 ns     fails
//      4 nops    ~375 ns     WORKS
//      8 nops    ~500 ns     works
//
//  So the threshold sits between 250 and 375 ns. 6 is shipped: comfortably over
//  the measured minimum, still only ~1 us per pixel (~77 ms for a full screen).
//
//  Why this is easy to get wrong: a bare `PORTG &= ~_BV(2)` compiles to a
//  single 2-cycle cbi, which is far too fast. UTFT survives only because it
//  reaches WR through a pointer with a runtime bitmask, which gcc cannot turn
//  into cbi/sbi -- it emits load/mask/store, about 6 cycles an edge. UTFT is
//  not being careful here, it is accidentally slow, and that accident is load
//  bearing. Here the timing is deliberate instead.
//
//  The trailing pad also buys RS its hold time after the latching edge, which
//  matters for the command writes in writeCmd().
//
//  If you ever see torn or speckled output, raise this first.
#ifndef TFT_WR_NOPS
#  define TFT_WR_NOPS 6
#endif

#define TFT_WR_PAD()    __builtin_avr_delay_cycles(TFT_WR_NOPS)

#define TFT_WR_STROBE() do {                                     \
        PORTG &= ~_BV(2); TFT_WR_PAD();                          \
        PORTG |=  _BV(2); TFT_WR_PAD();                          \
    } while (0)

// --- geometry -------------------------------------------------------------
#define TFT_NATIVE_W      240      // panel scans portrait; landscape is remapped
#define TFT_NATIVE_H      320
#define TFT_DEFAULT_ROT   1        // 1 == LANDSCAPE, what UNIT-T6 used

// ===========================================================================
//  2. Touch panel  --  bit-banged SPI to an ADS7843-class controller
// ===========================================================================
//  Original code:  ITDB02_Touch myTouch(6, 5, 4, 3, 2);
//                                      CLK CS DIN DOUT IRQ
#define TOUCH_PIN_CLK      6
#define TOUCH_PIN_CS       5
#define TOUCH_PIN_DIN      4
#define TOUCH_PIN_DOUT     3
#define TOUCH_PIN_IRQ      2

#define TOUCH_SAMPLES      4       // UNIT-T6 ran PREC_MEDIUM; 4 reads averaged

//  Raw 12-bit corner values, derived from ITDB02_Touch's own calibration
//  constants rather than guessed:
//
//      #define PixSizeX  13.78      #define PixOffsX  411
//      #define PixSizeY  11.01      #define PixOffsY  378
//
//  Its portrait mapping was  x = 240 - (TP_X - 411)/13.78
//                            y = 320 - (TP_Y - 378)/11.01
//  so the raw values sweep 411 .. 411 + 240*13.78 = 3718  on the X channel
//  and                     378 .. 378 + 320*11.01 = 3901  on the Y channel.
//
//  Your panel may differ a little; demo 9 shows the live raw pair if you want
//  to tighten them.
#define TOUCH_RAW_X_MIN    411
#define TOUCH_RAW_X_MAX    3718
#define TOUCH_RAW_Y_MIN    378
#define TOUCH_RAW_Y_MAX    3901

//  UNIT-T6 read the panel in portrait and patched it up by hand:
//      touchX = 319 - myTouch.getY();   touchY = 239 - myTouch.getX();
//  Substituting the portrait formulas above, the two 240/320 offsets cancel
//  against the 319/239 ones, and what is left is simply
//      screen_x = (TP_Y - 378)/11.01        <- from the Y channel, ascending
//      screen_y = (TP_X - 411)/13.78        <- from the X channel, ascending
//  So it is an axis swap and NO inversion. The inversions in the old source
//  are an artefact of ITDB02_Touch counting down from 240/320 internally, not
//  a property of the hardware -- which is why taking those two "319 -" and
//  "239 -" terms at face value produces a panel that feels rotated.
#define TOUCH_SWAP_XY      1
#define TOUCH_INVERT_X     0
#define TOUCH_INVERT_Y     0

// ===========================================================================
//  3. SD card  --  hardware SPI
// ===========================================================================
//  tinyFAT's initFAT() used the Mega's hardware SPI with SS on 53.  The
//  standard SD library wants the same wiring, it just needs to be told.
#define SD_PIN_CS         53       // PB0 / SS
#define SD_PIN_MOSI       51
#define SD_PIN_MISO       50
#define SD_PIN_SCK        52

//  Some shields wire chip select somewhere other than 53.  With this on,
//  sdBegin() falls back to these before giving up -- handy the first time you
//  meet a board, but each attempt costs about 1.5 s, so booting with no card
//  inserted takes ~6 s.  Set to 0 once you know your wiring.
#define SD_SCAN_ALTERNATE_CS  1
#define SD_CS_CANDIDATES      { 53, 4, 10, 8 }

// ===========================================================================
//  4. Extra hardware that was on the UNIT-T6 handheld
// ===========================================================================
//  Left here as documentation.  Uncomment what your build actually has.
//#define UNIT_PIN_SPEAKER    8      // PWM
//#define UNIT_PIN_VIBRATOR   9      // PWM
//#define UNIT_ADC_AXIS_Z     2      // analogRead(A2)  accelerometer
//#define UNIT_ADC_AXIS_X     3      // analogRead(A3)
//#define UNIT_ADC_AXIS_Y     4      // analogRead(A4)
//#define UNIT_GPS_SERIAL     Serial1
//#define UNIT_GPS_BAUD       4800

#define CONSOLE_BAUD        115200

#endif // BOARD_CONFIG_H
