# Desktop agent

A Windows tray application that collects system telemetry and feeds it to the
[display firmware](../ARDUINO/README.md) over USB serial.

Plain **C++20 on Win32** — no frameworks, no package manager, no runtime to
install. NVML and the two shared-memory bridges are resolved at runtime, so the
binary starts on a machine that has none of them.

---

## What it does

Lives in the notification area as a microchip icon that carries the link state:

- **blue ON** — the board is talking to the agent
- **dark red OFF** — no link

Right-click for **Settings** and **Exit**.

### Settings

| | |
|---|---|
| **Autostart** | start with Windows, already elevated |
| **Serial port** | `AUTO` (probe every port) or a specific COM port |
| **Polling interval** | 2 / 5 / 10 / 15 / 30 seconds |

The window also reports which sensor sources are live, so a value reading `n/a`
can be explained without digging through a log.

---

## Recovering the link

The agent drops and reopens the port when Windows broadcasts a resume from
sleep. This is not belt-and-braces: while the machine is suspended the USB stack
re-enumerates, and the handle the agent still holds refers to a device that no
longer exists. Such a handle does not reliably fail -- `ReadFile` can keep
succeeding with zero bytes for ever -- so without the resume notice the link
simply never comes back.

Three mechanisms cover the rest:

- **`ClearCommError` after every read.** It talks to the driver and fails once
  the device is gone, which is what actually detects an unplugged adapter.
  Latched framing and overrun flags are cleared but not treated as fatal: one
  glitch on a hot-plugged cable fails its checksum and is dropped, and killing
  the link over a single bad byte would be worse than the glitch.
- **A 30 s silence watchdog.** Distinct from the 20 s no-valid-request timeout:
  that one only applies once a link has existed, whereas a port that was never
  alive, or is open but dead, produces no bytes at all.
- **Device-tree changes**, but only while already disconnected. `WM_DEVICECHANGE`
  fires for any USB device on the machine, so acting on it unconditionally would
  interrupt a healthy link every time a memory stick was plugged in.

The window that receives all of this is a normal top-level window that is never
shown, not a message-only one. `WM_POWERBROADCAST` and `WM_DEVICECHANGE` are
delivered to top-level windows only, and `SetForegroundWindow` -- which
`TrackPopupMenuEx` needs to report the clicked command reliably -- cannot succeed
on a message-only window at all.

## Requires administrator

The agent **exits with an explanation** if it is not elevated.

The reason is the process list: without elevation `OpenProcess` fails for
services and anything running as another user, so the heaviest process on the
machine can be invisible. The manifest requests `requireAdministrator`, so a
normal double-click raises the UAC prompt rather than starting only to refuse.

Autostart is therefore a **scheduled task with RunLevel HIGHEST**, not an
`HKCU\...\Run` entry. A Run entry launches unelevated, which would start the app
at every logon purely to show its own error, and there is no way to raise
privileges from one without prompting every time.

---

## What it can and cannot measure

| Metric | Source | Needs |
|---|---|---|
| CPU usage, per-core usage and clock | PDH | — |
| memory, commit | `GetPerformanceInfo` | — |
| network rates and totals | `GetIfTable2` | — |
| top processes | Toolhelp + `GetProcessTimes` | **elevation** |
| GPU usage, clock, power, VRAM, temp | NVML (`nvml.dll`) | an NVIDIA GPU |
| **CPU temperature, CPU package power** | MSI Afterburner shared memory | **Afterburner running** |
| **framerate** | RivaTuner (RTSS) shared memory | **RTSS running** |

The last two rows are not laziness. On AMD Ryzen, package temperature and power
come from SMU/MSR reads that require **ring 0** — every user-mode tool that shows
them (Afterburner, HWiNFO, LibreHardwareMonitor) ships its own signed kernel
driver to do it. Rather than add a third driver to the machine for two numbers,
the agent reads what Afterburner's driver already collected.

Framerate is the same story: it belongs to whichever process is presenting
frames, and measuring it from outside means ETW Present tracing — what PresentMon
does in several thousand lines. RTSS already hooks every 3D application and
publishes per-process frame timing.

With those apps closed, the affected values report `n/a` and everything else
keeps working. **Nothing is invented to fill a gap** — which is why the protocol
distinguishes `-1` from `0`. A desktop at idle has no framerate; it is not
running at 0 fps.

### Limitations worth stating

- **Power is a sum of sensors, not a wall measurement.** It excludes drives,
  fans, RAM, VRM losses and PSU inefficiency, so expect it to read well under a
  meter at the plug.
- **GPU is NVIDIA-only.** AMD and Intel would need ADL / Level Zero; the source
  is a drop-in `SensorSource`, so adding one is self-contained.
- **Device 0 only** if several NVIDIA GPUs are present.
- **Per-core temperature is not reported** — silicon exposes package or per-CCD
  sensors, not one per core, so the field is `-1` rather than a fabricated copy
  of the package value.
- **Windows 10 1803 or later**, x64.

---

## Building

From the repository root:

```bash
build.cmd desktop
```

Or directly, with any MinGW-w64 on PATH:

```bash
make -C DESKTOP
```

`CMakeLists.txt` is also provided for IDE users, but nothing in the bootstrap
path depends on CMake or Ninja — w64devkit already ships `make`, so a
from-scratch setup needs one download instead of three.

Cross-compiling from Linux:

```bash
make -C DESKTOP CXX=x86_64-w64-mingw32-g++ WINDRES=x86_64-w64-mingw32-windres
```

### The self-test harness

```bash
make -C DESKTOP selftest
```

`build/selftest.exe` prints every sensor reading and **the exact bytes the
display would receive**, then round-trips each line through the checksum parser.
It answers "is the sensor wrong or is the firmware drawing it wrong?" in one
step, without a board attached.

### Layout

```
src/
  main.cpp             entry point, elevation check, icon export
  App.h/.cpp           hidden owner window, tray lifetime
  TrayIcon.h/.cpp      notification icon and menu
  IconFactory.h/.cpp   the microchip, drawn procedurally
  SettingsDialog.*     the settings window
  Settings.h/.cpp      persistence in HKCU
  Autostart.h/.cpp     scheduled task, RunLevel HIGHEST
  SerialPort.h/.cpp    COM port + enumeration
  Protocol.h/.cpp      wire encoding
  Monitor.h/.cpp       the worker thread that owns the link
  sensors/             one file per data source
resources/
  app.rc               icon + manifest + dialog template
  app.manifest         requireAdministrator, visual styles, DPI
  appicon.ico          GENERATED -- see below
tools/
  mkicon.cpp           writes appicon.ico
  mkpreview.cpp        renders the icon variants to a BMP for review
  selftest.cpp         the console harness
```

### The icon is generated, not committed art

`appicon.ico` is produced by `mkicon`, which links only `IconFactory.cpp`. That
keeps the executable icon, the window icon and the tray icon provably the same
drawing, at whatever size Windows asks for, with correct alpha at every size.

`make icon` regenerates it; `make clean` deliberately leaves it alone.

---

## Two non-obvious build details

Both cost real debugging time and are easy to reintroduce:

**`NTDDI_VERSION` is not redundant with `_WIN32_WINNT`.** `iphlpapi.h` gates its
include of `netioapi.h` on NTDDI, and inside `netioapi.h` the `MIB_IF_ROW2` /
`GetIfTable2` block sits behind `#ifdef _WS2IPDEF_`. So `ws2ipdef.h` must be
included first *and* NTDDI set, or `GetIfTable2` simply does not exist and the
error reads as though the SDK were too old.

**PDH must use the English counter APIs.** `PdhAddEnglishCounterW` resolves
counter names independently of the system language. The localised variants
resolve against the display language, so on a non-English Windows
`\Processor Information(*)\% Processor Time` does not exist and every counter
fails to add. This is the single most common way PDH code breaks on someone
else's PC.
