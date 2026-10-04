# Multi Screen System Monitor

A hardware system monitor. A Windows agent collects telemetry and sends it to an
**Arduino MEGA** driving a **320×240 SSD1289 TFT**, which displays it across
seven tap-switched screens styled after **btop**, plus a hidden diagnostics
screen.

```
┌─ Windows ────────────┐         USB serial          ┌─ Arduino MEGA ───────┐
│  sysmon.exe          │  115200 8N1, line framed    │  320x240 SSD1289 TFT │
│  PDH / NVML /        │ ──────────────────────────► │  7 screens, tap to   │
│  Afterburner / RTSS  │ ◄────────────────────────── │  switch, btop style  │
└──────────────────────┘   the display asks first    └──────────────────────┘
```

---

## Quick start

**Windows**

```bash
bootstrap.cmd
```

```bash
build.cmd
```

```bash
build.cmd upload COM20
```

**Git Bash, WSL, Linux, macOS**

```bash
./bootstrap.sh && ./build.sh
```

Then run `DESKTOP\build\sysmon.exe`. It asks for elevation, puts a microchip in
the notification area, and finds the board on its own.

`bootstrap.cmd` downloads everything into `.toolchain/` and touches nothing
outside this folder. If you already have `g++` and `arduino-cli` on PATH, skip it
— `build` finds them and uses those instead.

> On Linux and macOS only the **firmware** builds. The agent is a Win32 program;
> `build.sh` says so rather than failing obscurely, and prints the mingw-w64
> cross-compile command.

---

## Documentation

| | |
|---|---|
| **[ARDUINO/README.md](ARDUINO/README.md)** | display type, full pinout, screens, the WR strobe trap, firmware limits |
| **[DESKTOP/README.md](DESKTOP/README.md)** | sensors and what each one needs, why elevation, build details |
| **[PROTOCOL.md](PROTOCOL.md)** | the wire format, and why the display drives the conversation |

---

## Hardware at a glance

| | |
|---|---|
| Board | Arduino MEGA, **ATmega1280** (2560 also supported) |
| Display | ITDB02-3.2S or equivalent, **320×240**, **SSD1289**, **16-bit parallel** |
| Touch | ADS7843 / XPT2046 resistive |
| Control pins | RS 38, WR 39, CS 40, RST 41 |
| Data bus | DB0–7 → pins 22–29 (PORTA), DB8–15 → pins 30–37 (PORTC) |
| Touch pins | CLK 6, CS 5, DIN 4, DOUT 3, IRQ 2 |
| Link | USB serial, 115200 8N1 |

Every pin lives in one file:
[`ARDUINO/SysMonitor/board_config.h`](ARDUINO/SysMonitor/board_config.h).

---

## The screens

Tap to advance. **Hold 3 seconds** for diagnostics; a short tap after 2 seconds
returns.

| # | Screen | Shows |
|---|---|---|
| 1 | CPU | usage, frequency, temperature, process count, uptime, history |
| 2 | CORES | per-core bars and clocks |
| 3 | MEMORY | used / total / cached, commit, history |
| 4 | GPU | framerate headline, GPU load bar, clock, temp, power, VRAM; combined power + framerate history |
| 5 | POWER | combined draw, per-rail breakdown, history |
| 6 | NETWORK | up/down rates, session totals, link speed |
| 7 | PROCESSES | CPU strip plus the top 12 by CPU and memory |

A screen held for a minute is saved to EEPROM and restored at the next power-up,
so the panel comes back where you left it.

---

## What it can and cannot measure

| Metric | Source | Needs |
|---|---|---|
| CPU usage, per-core clock, memory, network, processes | PDH / Win32 | elevation (for the process list) |
| GPU usage, clock, power, VRAM, temperature | NVML | an NVIDIA GPU |
| **CPU temperature and package power** | Afterburner shared memory | **MSI Afterburner running** |
| **framerate** | RTSS shared memory | **RivaTuner running** |

On AMD Ryzen, package temperature and power require **ring 0** — every user-mode
tool that shows them ships a signed kernel driver. Rather than add another driver
to your machine for two numbers, the agent reads what Afterburner's driver
already collected. Framerate belongs to whichever process is presenting frames,
which from outside means ETW tracing; RTSS already has it.

With those closed, those values report `n/a` and everything else keeps working.
**Nothing is invented to fill a gap** — the protocol distinguishes `-1` from `0`,
because a desktop at idle has no framerate; it is not running at 0 fps.

**Power is a sum of sensors, not a wall measurement.** It excludes drives, fans,
RAM, VRM losses and PSU inefficiency.

---

## Repository layout

```
build.cmd  build.sh        build both halves
bootstrap.cmd  .sh         fetch the toolchain into .toolchain/
PROTOCOL.md                the wire contract
SHARED/sysmon_wire.h       the contract both sides compile against
tools/sync-shared.ps1      copies it into both builds
ARDUINO/                   display firmware, no third-party libraries
DESKTOP/                   Windows agent, C++20 + Win32
```

### Editing the protocol

`SHARED/sysmon_wire.h` is canonical. Both builds hold a copy, because the Arduino
IDE only compiles files inside the sketch folder. After editing it run
`tools/sync-shared.ps1`; the CMake build also copies it at configure time so a
stale copy cannot ship.

---

## Bumping the pinned toolchain

`bootstrap` pins versions on purpose — a build script that silently tracks
"latest" stops being reproducible the day upstream changes a flag. To update,
edit `W64_VER` in `bootstrap.cmd` and `bootstrap.sh`. arduino-cli uses Arduino's
stable `_latest_` URL.

w64devkit is distributed **only** as a self-extracting archive; upstream stopped
publishing a plain `.zip`. `bootstrap` therefore has to run the downloaded file
to unpack it, shows you the URL first, and asks before doing so. If you would
rather not, install any MinGW-w64 yourself and put `g++` on PATH.

---

## Contributing

Clone freely, branch freely, and open a pull request. `main` is protected:
changes land through a reviewed PR.

---

## Licence

MIT — see [LICENSE](LICENSE).
