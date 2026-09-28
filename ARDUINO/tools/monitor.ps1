# Watch the firmware's serial log.
#
# Only useful with the desktop agent stopped: the two cannot share the port, and
# with the agent running this will simply fail to open it.

param([string]$Port = "COM20", [int]$Baud = 115200)

$agent = Get-Process sysmon -ErrorAction SilentlyContinue
if ($agent) {
    Write-Host "The desktop agent is running and holds $Port." -ForegroundColor Yellow
    Write-Host "Stop it first, or this will not be able to open the port." -ForegroundColor Yellow
}

$cli = Join-Path $env:LOCALAPPDATA "Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
& $cli monitor -p $Port --config "baudrate=$Baud"
