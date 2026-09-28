# Copies SHARED/sysmon_wire.h into both builds.
#
# The file has to be duplicated because the Arduino IDE only compiles sources
# that sit in the sketch folder, and CMake wants it on the desktop include path.
# SHARED/ is the canonical copy; run this after editing it, or the two sides will
# disagree about field order and you will chase phantom sensor bugs.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root "SHARED/sysmon_wire.h"

foreach ($dst in @("DESKTOP/src/sysmon_wire.h", "ARDUINO/SysMonitor/sysmon_wire.h")) {
    $full = Join-Path $root $dst
    Copy-Item $src $full -Force
    Write-Host "synced -> $dst" -ForegroundColor Green
}
