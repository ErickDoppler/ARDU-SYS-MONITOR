#!/usr/bin/env sh
# ===========================================================================
#  Multi Screen System Monitor -- toolchain bootstrap (POSIX / Git Bash / WSL)
#
#  Downloads what is needed into .toolchain/, touching nothing outside this
#  folder. Already have a toolchain? build.sh finds g++ and arduino-cli on PATH
#  and skips all of this.
#
#    ./bootstrap.sh        ask before each download
#    ./bootstrap.sh -y     take everything without asking (for CI)
#
#  On Linux and macOS this installs arduino-cli only. The desktop agent is a
#  Win32 program and does not build there -- see README.
# ===========================================================================
set -eu

ROOT="$(cd "$(dirname "$0")" && pwd)"
TC="$ROOT/.toolchain"
ASSUME_YES=0
[ "${1:-}" = "-y" ] && ASSUME_YES=1

# Pinned on purpose: a build script that silently tracks "latest" stops being
# reproducible the day upstream changes a flag. See README to bump.
W64_VER="2.10.0"
W64_URL="https://github.com/skeeto/w64devkit/releases/download/v${W64_VER}/w64devkit-x64-${W64_VER}.7z.exe"

case "$(uname -s)" in
    Linux*)   OS=linux;   ACLI_ASSET="arduino-cli_latest_Linux_64bit.tar.gz" ;;
    Darwin*)  OS=macos;   ACLI_ASSET="arduino-cli_latest_macOS_64bit.tar.gz" ;;
    MINGW*|MSYS*|CYGWIN*) OS=windows; ACLI_ASSET="arduino-cli_latest_Windows_64bit.zip" ;;
    *)        OS=unknown; ACLI_ASSET="arduino-cli_latest_Linux_64bit.tar.gz" ;;
esac
ACLI_URL="https://downloads.arduino.cc/arduino-cli/${ACLI_ASSET}"

command -v curl >/dev/null 2>&1 || { echo "ERROR: curl is required." >&2; exit 1; }

ask() {
    [ "$ASSUME_YES" = "1" ] && return 0
    printf '%s [y/N] ' "$1"
    read -r reply
    case "$reply" in [Yy]*) return 0 ;; *) return 1 ;; esac
}

mkdir -p "$TC"

# ---------------------------------------------------------------------------
#  arduino-cli
# ---------------------------------------------------------------------------
if [ -x "$TC/arduino-cli/arduino-cli" ] || [ -x "$TC/arduino-cli/arduino-cli.exe" ]; then
    echo "[ok] arduino-cli already present"
elif command -v arduino-cli >/dev/null 2>&1; then
    echo "[ok] arduino-cli already on PATH"
else
    echo
    echo "About to download arduino-cli:"
    echo "  $ACLI_URL"
    if ask "Proceed?"; then
        mkdir -p "$TC/arduino-cli"
        case "$ACLI_ASSET" in
            *.zip)
                curl -L --fail --progress-bar -o "$TC/acli.zip" "$ACLI_URL"
                # bsdtar (Windows) and unzip both handle this; prefer whichever exists.
                if command -v unzip >/dev/null 2>&1; then
                    unzip -q -o "$TC/acli.zip" -d "$TC/arduino-cli"
                else
                    tar -xf "$TC/acli.zip" -C "$TC/arduino-cli"
                fi
                rm -f "$TC/acli.zip"
                ;;
            *.tar.gz)
                curl -L --fail --progress-bar -o "$TC/acli.tgz" "$ACLI_URL"
                tar -xzf "$TC/acli.tgz" -C "$TC/arduino-cli"
                rm -f "$TC/acli.tgz"
                ;;
        esac
        echo "[ok] arduino-cli installed"
    else
        echo "Skipped."
    fi
fi

# Resolve whichever arduino-cli we ended up with.
ACLI=""
[ -x "$TC/arduino-cli/arduino-cli" ] && ACLI="$TC/arduino-cli/arduino-cli"
[ -x "$TC/arduino-cli/arduino-cli.exe" ] && ACLI="$TC/arduino-cli/arduino-cli.exe"
[ -z "$ACLI" ] && command -v arduino-cli >/dev/null 2>&1 && ACLI="arduino-cli"

if [ -n "$ACLI" ]; then
    echo "Installing the arduino:avr core (the compiler for the board)..."
    # Keep the core's data beside the tool so an existing Arduino IDE setup is
    # left alone.
    ARDUINO_DIRECTORIES_DATA="$TC/arduino-data"
    ARDUINO_DIRECTORIES_DOWNLOADS="$TC/arduino-data/staging"
    export ARDUINO_DIRECTORIES_DATA ARDUINO_DIRECTORIES_DOWNLOADS
    "$ACLI" core update-index >/dev/null
    "$ACLI" core install arduino:avr
    echo "[ok] arduino:avr core ready"
fi

# ---------------------------------------------------------------------------
#  C++ toolchain for the desktop agent -- Windows only
# ---------------------------------------------------------------------------
if [ "$OS" != "windows" ]; then
    echo
    echo "[note] The desktop agent is a Win32 program and is not built on $OS."
    echo "       The firmware above is all this platform can produce."
    echo "       To cross-compile the agent: install mingw-w64 and run"
    echo "         make -C DESKTOP CXX=x86_64-w64-mingw32-g++ WINDRES=x86_64-w64-mingw32-windres"
    echo
    echo "Bootstrap complete. Now run:  ./build.sh"
    exit 0
fi

if [ -x "$TC/w64devkit/bin/g++.exe" ]; then
    echo "[ok] w64devkit already present"
elif command -v g++ >/dev/null 2>&1; then
    echo "[ok] a g++ is already on PATH; skipping w64devkit"
else
    echo
    echo "w64devkit is distributed ONLY as a self-extracting archive -- upstream"
    echo "stopped publishing a plain .zip, so unpacking it means running the"
    echo "downloaded file. It extracts into $TC/w64devkit and writes nothing"
    echo "outside that folder."
    echo
    echo "  $W64_URL"
    echo
    echo "If you would rather not run a downloaded executable, install any"
    echo "MinGW-w64 toolchain yourself and put g++ on PATH; build.sh will use it."
    echo
    if ask "Download and extract w64devkit?"; then
        curl -L --fail --progress-bar -o "$TC/w64devkit.exe" "$W64_URL"
        echo "Extracting..."
        # 7-Zip SFX switches: -y accept, -o target directory.
        (cd "$TC" && ./w64devkit.exe -y -o"$(pwd -W 2>/dev/null || pwd)" >/dev/null)
        rm -f "$TC/w64devkit.exe"
        if [ -x "$TC/w64devkit/bin/g++.exe" ]; then
            echo "[ok] w64devkit installed"
        else
            echo "ERROR: extraction finished but g++ is missing." >&2
            exit 1
        fi
    else
        echo "Skipped. The firmware will still build; the desktop agent will not."
    fi
fi

echo
echo "Bootstrap complete. Now run:  ./build.sh"
