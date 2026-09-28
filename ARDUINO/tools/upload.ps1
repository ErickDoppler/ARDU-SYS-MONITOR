# Compile and flash the display firmware.
#
#   .\tools\upload.ps1
#   .\tools\upload.ps1 -Port COM7 -Fqbn arduino:avr:mega:cpu=atmega2560
#
# The desktop agent holds the serial port while it runs, and avrdude cannot open
# a port that is already in use, so the agent is stopped first and restarted
# afterwards if it was running.

param(
    [string]$Port = "COM20",
    [string]$Fqbn = "arduino:avr:mega:cpu=atmega1280",
    [switch]$NoRestart
)

$ErrorActionPreference = "Stop"

$root   = Split-Path -Parent $PSScriptRoot
$sketch = Join-Path $root "SysMonitor"
$cli    = Join-Path $env:LOCALAPPDATA "Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"

if (-not (Test-Path $cli)) {
    Write-Host "arduino-cli not found at: $cli" -ForegroundColor Red
    exit 1
}

$agent = Get-Process sysmon -ErrorAction SilentlyContinue
if ($agent) {
    Write-Host "Stopping the desktop agent so the port is free" -ForegroundColor Yellow
    $agent | Stop-Process -Force
    Start-Sleep -Milliseconds 800
}

# -DSERIAL_RX_BUFFER_SIZE=768: the core default of 64 bytes is only ~5.5 ms of
# stream at 115200. A graph repaint now runs every second and takes ~45 ms, so a
# frame arriving during one would be truncated and dropped; 768 bytes covers it.
# The cache and build paths carry the buffer size. The core is compiled with this
# flag too, so a cache built at a different size leaves HardwareSerial.h and the
# cached object disagreeing about an array bound -- an error that looks nothing
# like a stale cache. Keying the path on the size makes that impossible.
$rxBuffer  = 768
$bufFlag   = "build.extra_flags=-DSERIAL_RX_BUFFER_SIZE=$rxBuffer"
$cachePath = Join-Path $env:TEMP "sysmon-core-cache-$rxBuffer"
$buildPath = Join-Path $env:TEMP "sysmon-build-$rxBuffer"

Write-Host "Building $Fqbn" -ForegroundColor Cyan
& $cli compile --fqbn $Fqbn --build-property $bufFlag `
    --build-cache-path $cachePath --build-path $buildPath $sketch
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Uploading to $Port" -ForegroundColor Cyan
# Same build path, so the upload flashes exactly what was just compiled with the
# larger receive buffer rather than a stale default-buffer build.
& $cli upload -p $Port --fqbn $Fqbn --input-dir $buildPath $sketch
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Flashed" -ForegroundColor Green

if ($agent -and -not $NoRestart) {
    $exe = Join-Path (Split-Path -Parent $root) "DESKTOP\build\sysmon.exe"
    if (Test-Path $exe) {
        Write-Host "Restarting the desktop agent" -ForegroundColor Cyan
        Start-Process -FilePath $exe
    }
}
