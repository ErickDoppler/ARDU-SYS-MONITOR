# Display firmware

Firmware for an Arduino MEGA driving a 320×240 parallel TFT, showing telemetry
sent by the [desktop agent](../DESKTOP/README.md) across seven tap-switched
screens styled after **btop**, plus a hidden diagnostics screen.

---

## Hardware

| | |
|---|---|
| **Board** | Arduino MEGA — **ATmega1280** (default) or ATmega2560 |
| **Display module** | ITDB02-3.2S / equivalent, 320×240 |
| **Controller** | **SSD1289** |
| **Interface** | **16-bit parallel** (8080-style) |
| **Touch** | ADS7843 / XPT2046 resistive, bit-banged SPI |
| **Shield** | ITDB02 MEGA Shield, or any wiring matching the table below |

Everything the firmware knows about the hardware lives in one file:
**[`SysMonitor/board_config.h`](SysMonitor/board_config.h)**. Nothing else
touches a pin.

### Pinout

| Signal | Arduino pin | AVR port |
|---|---|---|
| LCD RS (register select) | 38 | PD7 |
| LCD WR (write strobe, active low) | 39 | PG2 |
| LCD CS (chip select, active low) | 40 | PG1 |
| LCD RST (reset, active low) | 41 | PG0 |
| LCD DB0–DB7 (low byte) | 22–29 | **PORTA** |
| LCD DB8–DB15 (high byte) | 30–37 | **PORTC** |
| Touch T_CLK | 6 | |
| Touch T_CS | 5 | |
| Touch T_DIN | 4 | |
| Touch T_DOUT | 3 | |
| Touch T_IRQ | 2 | |

The data pins are **not** free to choose. 16-bit mode writes two whole ports at
once, so the shield hardwires the bus onto PORTA and PORTC. Note that Arduino
pins 30–37 map to **PC7 down to PC0** — reversed — which is why bit order is a
configurable option and not an assumption.

Pins left free on a MEGA: 0–1 (USB serial), 7, 10–21, 42–53, A0–A15.

---

## The one thing that will bite you: WR strobe width

**This panel needs a far wider write strobe than its own datasheet asks for.**
Measured on real hardware at 16 MHz:

| padding | WR low | result |
|---|---|---|
| 0 nops | ~125 ns | **fails** — a handful of pixels latch, the rest are lost |
| 2 nops | ~250 ns | **fails** |
| **4 nops** | **~375 ns** | **works** |
| 8 nops | ~500 ns | works |

The datasheet says 50 ns. Reality wanted 250–375. `TFT_WR_NOPS` ships at **6**.

This is a trap for anyone modernising a UTFT project. A plain
`PORTG &= ~_BV(2)` compiles to one 2-cycle `cbi` — far too fast. UTFT survives
only because it reaches WR through a *pointer with a runtime bitmask*, which gcc
cannot reduce to `cbi`/`sbi`; it emits load/mask/store, roughly 6 cycles an edge.
**UTFT is not being careful here, it is accidentally slow, and that accident is
load bearing.** Replace it with "clean, fast, direct port writes" and the panel
dies.

The failure mode is misleading too: too short a strobe gives a **blank white
screen with a speck of colour in one corner**, not garbage. The specks are the
writes that happened to follow a slow register write. If you see that, raise
`TFT_WR_NOPS`.

---

## Screens

Tap anywhere to advance.

| # | Screen | Shows |
|---|---|---|
| 1 | CPU | usage, frequency, temperature, process count, uptime, history |
| 2 | CORES | per-core bars and clocks, two columns |
| 3 | MEMORY | used / total / cached, commit, history |
| 4 | GPU | usage, watts, clock, VRAM, framerate; twin-scale strip, usage orange over clock blue |
| 5 | POWER | combined draw with a per-rail breakdown and history |
| 6 | NETWORK | up/down rates, session totals, link speed, two graphs |
| 7 | PROCESSES | 30 px CPU strip plus the top 12 by CPU, with memory |

**Diagnostics — hold for 3 seconds.** Shows accepted/rejected frame counts, last
frame size, age since the last frame, a 1-second-per-bucket timing graph, and the
last six raw lines from the host. A **short tap after 2 seconds** returns.

The 2-second guard is deliberate: after a 3-second hold your finger is usually
still on the glass, and the lift would otherwise instantly dismiss the screen you
just opened.

---

## Rendering model

Receiving and painting run on **separate clocks**, which is what keeps the panel
in tempo when the agent is early, late or bursty.

- **receive** — parses into a back buffer and publishes it pointer-flip style,
  only once a whole frame has validated. The renderer can never see a
  half-parsed frame.
- **render** — every **1 second** exactly, reads the published buffer and
  repaints only the fields whose values moved.

History lives on the device in ring buffers and advances **one sample per render
tick**, not per arrival. So the horizontal axis is honest: **1 px = 1 s**, and a
276 px plot spans about 4.5 minutes. Pushing on arrival instead made a burst of
three frames jump the plot three pixels at once while a stalled link froze it.

Only the screen on show advances its history. Data for other screens is not being
received, so pushing their last known value once a second would draw a confident
flat line out of nothing.

---

## Limitations

- **32 cores maximum** (`SYSMON_MAX_CORES`). Not arbitrary: 64 cores would need
  ~780 bytes on the wire, overflowing `SYSMON_MAX_LINE` (512), and 32 bars is
  already the most a 320×240 panel shows legibly.
- **`int` is 16-bit on AVR.** Every MiB and KiB/s quantity is explicitly
  `int32_t` and parsed with `atol`. This is not theoretical — a 128 GB machine
  reports 130981 MiB, which wraps to −91 in an `int` and made the memory screen
  read `n/a`.
- **Serial receive buffer.** The core default of 64 bytes is ~5.5 ms of stream at
  115200, but a graph repaint takes ~45 ms, so a frame arriving mid-repaint gets
  truncated and dropped. The build scripts pass
  `-DSERIAL_RX_BUFFER_SIZE=768`. **Building from the Arduino IDE GUI cannot set
  this** — the firmware still works, it just drops a frame occasionally under
  load, and emits a `#warning` saying so.
- **RAM is 69% full** (5721 of 8192 bytes on an ATmega1280), leaving ~2.4 KB of
  stack. Another full-history screen would need something trimmed.
- **No per-core temperature.** Desktop CPUs expose package or per-CCD sensors,
  not one per core. The field is sent as `-1` rather than faking it.
- **Horizontal 20% rules are skipped on plots under 40 px**, so the GPU strips
  get vertical grid only — rules every 4 px would be solid noise.

---

## Building

From the repository root:

```bash
build.cmd firmware
```

```bash
build.cmd upload COM20
```

Or with the Arduino IDE: open `SysMonitor/SysMonitor.ino`, choose **Arduino Mega
or Mega 2560** with processor **ATmega1280**, and upload. Mind the serial buffer
note above.

For a MEGA 2560, change the FQBN to `arduino:avr:mega:cpu=atmega2560`.

### Layout

```
SysMonitor/
  SysMonitor.ino     touch handling, the render clock, the diagnostics overlay
  board_config.h     <-- every pin, port and timing value
  TftSSD1289.h/.cpp  display driver: shapes, text, bulk pixel push
  TouchPanel.h/.cpp  ADS7843 touch driver
  gesture.h          debounced tap / long-press recognition
  font5x7.h          480-byte ASCII font, flash-resident
  theme.h            btop palette, heat colours, bar cap derivation
  widgets.h/.cpp     boxes, meters, graphs, axes, formatters
  screens.h/.cpp     the seven screens plus diagnostics
  link.h/.cpp        wire protocol client and the double-buffered store
  sysmon_wire.h      the shared contract -- do not edit here, see ../../SHARED
```

---

## Touch calibration

Calibration was recovered from the original ITDB02_Touch library rather than
guessed: raw values sweep **411–3718** on the X channel and **378–3901** on Y.
Demo screen 9 in the old display-unit project shows live raw values if you need
to retune for a different panel.

Three things about this panel are easy to get subtly wrong, and none fail
loudly — they just make touch feel rotated or offset:

- `TP_Y` comes from command `0x90`, `TP_X` from `0xD0`
- the ADS7843 shifts bits out on the **falling** clock edge
- the old code's `319 - getY()` / `239 - getX()` are **not** inversions; they
  cancel against the library's internal countdown, leaving a plain axis swap
