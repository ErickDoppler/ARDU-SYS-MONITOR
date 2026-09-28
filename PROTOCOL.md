# Multi Screen System Monitor — wire protocol

Line-oriented ASCII over USB serial, **115200 8N1**. Every message is one line
terminated by `\n`.

The display drives the conversation. It asks for the screen it is showing; the
desktop answers with exactly that screen's dataset and keeps resending it at the
polling interval until a different screen is requested. Nothing else is sent, so
a 320×240 panel never spends time parsing numbers it is not going to draw.

```
  Arduino                          Desktop
     |  {Q;D4}                        |     tap switched to the GPU screen
     |------------------------------->|
     |                 {D4;73;2841;1905;6120;12288;144;61*5C}
     |<-------------------------------|     answered immediately
     |                                |
     |                 {D4;75;2903;1910;6144;12288;141;62*4A}
     |<-------------------------------|     ...then every interval
```

## Framing

```
{ payload *CC }
```

- `{` … `}` delimit the message. Anything outside is ignored, so line noise and
  a half-received message cannot be mistaken for data.
- `payload` is `;`-separated fields. The first field is the message type.
- `*CC` is two uppercase hex digits: XOR of every byte of `payload`.
  A line whose checksum does not match is dropped silently.

Receivers must tolerate unknown message types and **extra trailing fields** —
that is the forward-compatible way to add a value to a screen later.

## Value conventions

| Kind | Encoding | Example |
|---|---|---|
| percentage | integer 0…100 | `73` |
| frequency | integer MHz | `4275` |
| temperature | integer °C | `61` |
| power | integer **tenths of a watt** | `2841` = 284.1 W |
| memory | integer MiB | `12288` |
| network rate | integer KiB/s | `1044` |
| framerate | integer fps | `144` |
| time | integer seconds | `86460` |

**`-1` means "no sensor for this on this machine"**, and is distinct from `0`.
The display renders it as `n/a` rather than as a zero, which matters: a missing
CPU temperature and a CPU running at 0 °C should not look the same.

## Desktop → Arduino

### `{HELLO;<app>;<protocolVersion>;<cores>*CC}`

Sent in reply to `{Q;HELLO}`. `cores` lets the per-core screen size its layout
before it has ever received core data.

```
{HELLO;ARDU-SYS-MONITOR;1;32*2E}
```

### `{D1;…}` — CPU, general

| # | Field |
|---|---|
| 1 | total CPU usage % |
| 2 | current frequency MHz |
| 3 | package temperature °C |
| 4 | process count |
| 5 | uptime seconds |

```
{D1;14;4275;52;284;86460*7B}
```

### `{D2;…}` — CPU, per core

| # | Field |
|---|---|
| 1 | core count `n` |
| 2…n+1 | one group per core: `usage,freqMHz,tempC` |

```
{D2;32;55,4200,-1;12,3900,-1;…*C4}
```

Per-core temperature is `-1` on most desktop CPUs — silicon reports per-CCD or
package only, not per core.

### `{D3;…}` — memory

| # | Field |
|---|---|
| 1 | used MiB |
| 2 | total MiB |
| 3 | cached MiB |
| 4 | commit used MiB |
| 5 | commit limit MiB |

### `{D4;…}` — GPU

| # | Field |
|---|---|
| 1 | usage % |
| 2 | power, tenths W |
| 3 | core clock MHz |
| 4 | VRAM used MiB |
| 5 | VRAM total MiB |
| 6 | framerate fps |
| 7 | temperature °C |

### `{D5;…}` — power

| # | Field |
|---|---|
| 1 | total, tenths W |
| 2 | CPU, tenths W |
| 3 | GPU, tenths W |
| 4 | other, tenths W |

`total` is the sum of the sources that actually reported. It is a sum of
sensors, **not** a wall-socket measurement: it excludes drives, fans, RAM, VRM
losses and PSU inefficiency, so expect it to read well under a meter at the
plug.

### `{D6;…}` — network

| # | Field |
|---|---|
| 1 | receive KiB/s |
| 2 | transmit KiB/s |
| 3 | received total MiB since app start |
| 4 | transmitted total MiB since app start |
| 5 | link speed Mbit/s |

### `{D7;…}` — processes

| # | Field |
|---|---|
| 1 | total CPU usage % (for the strip graph) |
| 2 | entry count `n` |
| 3…n+2 | one group per process: `name,cpu%,memMiB` |

```
{D7;23;3;chrome.exe,14,1820;devenv.exe,9,940;explorer.exe,1,142*3D}
```

Names are the executable only, never a path, and are truncated to 15
characters. A name is sanitised so it can never contain `;`, `,`, `*`, `{`, `}`
or a control character — otherwise a process could rename itself and corrupt the
framing.

## Arduino → Desktop

### `{Q;HELLO*CC}`

Sent at boot and after any serial timeout, until a `HELLO` comes back.

### `{Q;D<n>*CC}`

Request the dataset for display `n`. Sent on boot and on every screen change.
The desktop answers at once and then repeats at the polling interval.

## Timing and recovery

- The desktop answers a request immediately, then resets its interval timer, so
  a tap feels instant no matter how slow the polling interval is.
- Graphs live on the **Arduino**, in ring buffers. The desktop sends only
  current values, so the wire stays small and history survives a dropped frame.
  One sample advances a graph by one pixel, so at a 30 s interval a 160 px graph
  spans 80 minutes.
- If the Arduino receives nothing for **4 polling intervals** it shows the
  screen as stale (dimmed values, `LINK` indicator red) and re-sends its
  request. It does not clear the data — last known values are more useful than
  an empty screen.
- The desktop treats the port as down if a write fails or no request has arrived
  for 60 s, and reconnects. The tray icon follows that state.

## Why the display asks, instead of the desktop pushing

The desktop cannot know which screen a tap just selected, so a push design
either sends everything — far more than a 16 MHz AVR can parse between frames —
or lags a screen behind. Having the panel name what it needs also means the
display ID arrives *with* the data, so after a reset the firmware knows which
screen the bytes belong to without a handshake.

## Serial line handling: do not assert DTR

The agent opens the port with **DTR and RTS deasserted**, and this is not
optional.

On a board with an FTDI or 16U2 bridge, DTR is wired to RESET through a
capacitor, so asserting it reboots the MCU. Opening the port with
`DTR_CONTROL_ENABLE` therefore resets the display on *every* connect — and the
agent reconnects on a settings change, on a link timeout, and on any write
error. The symptom is not an obvious one: the firmware's splash screen
reappears and every graph is empty, because `setup()` genuinely did run again.
It looks like the firmware is crashing on disconnect.

Nothing needs that reset. The firmware re-sends its request every 2 s while it
has no link, so port auto-detection still works — it waits for the next retry
instead of forcing one, which is why the AUTO probe window is 3.5 s rather than
2.5 s.

If you write another client for this protocol, deassert DTR or you will wipe the
history every time you connect.
