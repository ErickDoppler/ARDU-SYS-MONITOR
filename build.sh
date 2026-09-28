#!/usr/bin/env sh
# ===========================================================================
#  Multi Screen System Monitor -- build both halves (POSIX / Git Bash / WSL)
#
#    ./build.sh                 desktop agent + firmware
#    ./build.sh desktop         just the agent   (Windows hosts only)
#    ./build.sh firmware        just the firmware
#    ./build.sh upload COM20    build the firmware and flash it
#    ./build.sh clean
#
#  Toolchains are looked for in this order, so an existing setup always wins
#  over anything this project downloaded:
#      1. PATH
#      2. .toolchain/           (created by bootstrap.sh)
#      3. /c/workenv/           (a common local layout)
#      4. the Arduino IDE 2 installation, for arduino-cli
# ===========================================================================
set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
TC="$ROOT/.toolchain"
TARGET="${1:-all}"
PORT="${2:-}"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;;
    *) IS_WINDOWS=0 ;;
esac

# --- locate g++ ------------------------------------------------------------
CXX=""
if command -v g++ >/dev/null 2>&1; then
    CXX="g++"
elif [ -x "$TC/w64devkit/bin/g++.exe" ]; then
    CXX="$TC/w64devkit/bin/g++.exe"
    PATH="$TC/w64devkit/bin:$PATH"; export PATH
elif [ -x "/c/workenv/w64devkit/bin/g++.exe" ]; then
    CXX="/c/workenv/w64devkit/bin/g++.exe"
    PATH="/c/workenv/w64devkit/bin:$PATH"; export PATH
fi

# --- locate arduino-cli ----------------------------------------------------
ACLI=""
if command -v arduino-cli >/dev/null 2>&1; then
    ACLI="arduino-cli"
elif [ -x "$TC/arduino-cli/arduino-cli.exe" ]; then
    ACLI="$TC/arduino-cli/arduino-cli.exe"
elif [ -x "$TC/arduino-cli/arduino-cli" ]; then
    ACLI="$TC/arduino-cli/arduino-cli"
elif [ -x "/c/workenv/arduino-cli/arduino-cli.exe" ]; then
    ACLI="/c/workenv/arduino-cli/arduino-cli.exe"
elif [ -x "$LOCALAPPDATA/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe" ]; then
    ACLI="$LOCALAPPDATA/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
fi

# Keep arduino-cli's data beside the tool when we installed it, so an existing
# Arduino IDE configuration is left alone.
if [ -d "$TC/arduino-data" ]; then
    ARDUINO_DIRECTORIES_DATA="$TC/arduino-data"
    ARDUINO_DIRECTORIES_DOWNLOADS="$TC/arduino-data/staging"
    export ARDUINO_DIRECTORIES_DATA ARDUINO_DIRECTORIES_DOWNLOADS
fi

FQBN="arduino:avr:mega:cpu=atmega1280"

# The core default receive buffer is 64 bytes, about 5.5 ms of stream at 115200.
# A graph repaint takes ~45 ms, so a frame arriving during one would be truncated
# and dropped. The core is compiled with this flag too, hence a cache path keyed
# to the size: mixing sizes leaves HardwareSerial disagreeing with its own cached
# object, and the error says nothing about a stale cache.
RXBUF=768
CACHE="${TMPDIR:-/tmp}/sysmon-core-cache-$RXBUF"
BUILDDIR="${TMPDIR:-/tmp}/sysmon-build-$RXBUF"

build_desktop() {
    if [ "$IS_WINDOWS" = "0" ]; then
        echo "[skip] desktop agent: it is a Win32 program and does not build on $(uname -s)."
        echo "       To cross-compile: make -C DESKTOP CXX=x86_64-w64-mingw32-g++ WINDRES=x86_64-w64-mingw32-windres"
        return 0
    fi
    if [ -z "$CXX" ]; then
        echo "ERROR: no C++ compiler found. Run ./bootstrap.sh, or put g++ on PATH." >&2
        return 1
    fi
    echo "=== desktop agent ========================================="
    echo "using $CXX"
    make -C "$ROOT/DESKTOP"
}

build_firmware() {
    if [ -z "$ACLI" ]; then
        echo "ERROR: arduino-cli not found. Run ./bootstrap.sh, or install the Arduino IDE." >&2
        return 1
    fi
    echo
    echo "=== display firmware ======================================"
    echo "using $ACLI"
    "$ACLI" compile --fqbn "$FQBN" \
        --build-property "build.extra_flags=-DSERIAL_RX_BUFFER_SIZE=$RXBUF" \
        --build-cache-path "$CACHE" --build-path "$BUILDDIR" \
        "$ROOT/ARDUINO/SysMonitor"
}

upload_firmware() {
    if [ -z "$PORT" ]; then
        echo "ERROR: no port given.  Usage:  ./build.sh upload COM20" >&2
        echo "Ports currently present:" >&2
        [ -n "$ACLI" ] && "$ACLI" board list || true
        return 1
    fi
    echo
    echo "Flashing to $PORT ..."
    # The agent holds the port while it runs, and avrdude cannot open a port that
    # is already in use.
    if [ "$IS_WINDOWS" = "1" ]; then
        taskkill //IM sysmon.exe //F >/dev/null 2>&1 || true
    fi
    "$ACLI" upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILDDIR" \
        "$ROOT/ARDUINO/SysMonitor"
    echo "Flashed."
}

case "$TARGET" in
    all)
        build_desktop
        build_firmware
        echo
        echo "==========================================================="
        echo " Both halves built."
        echo "   agent     DESKTOP/build/sysmon.exe   (run as administrator)"
        echo "   firmware  flash with:  ./build.sh upload COM20"
        echo "==========================================================="
        ;;
    desktop)  build_desktop ;;
    firmware) build_firmware ;;
    upload)   build_firmware; upload_firmware ;;
    clean)
        echo "Cleaning..."
        make -C "$ROOT/DESKTOP" clean 2>/dev/null || rm -rf "$ROOT/DESKTOP/build"
        rm -rf "$CACHE" "$BUILDDIR"
        echo "Done."
        ;;
    *)
        echo "Unknown target: $TARGET" >&2
        echo "Use: all | desktop | firmware | upload <PORT> | clean" >&2
        exit 1
        ;;
esac
