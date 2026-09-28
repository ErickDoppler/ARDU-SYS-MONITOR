# ---------------------------------------------------------------------------
#  Builds the desktop agent. A convenience wrapper for people who live in
#  PowerShell -- ..\..\build.cmd and ..\..\build.sh do the same thing.
#
#    .\tools\build.ps1            build
#    .\tools\build.ps1 -Clean     wipe build\ first
#    .\tools\build.ps1 -Run       build, then launch (Windows prompts for UAC)
#    .\tools\build.ps1 -SelfTest  build the console sensor harness instead
#
#  Drives the Makefile rather than CMake. w64devkit already ships make, so this
#  needs no other tool -- an earlier version pinned a specific CMake and Ninja
#  install path, which worked on exactly one machine.
#
#  The compiler is looked for on PATH first, then in the repo's .toolchain\
#  (created by bootstrap.cmd), then in C:\workenv\ -- so an existing setup is
#  always preferred over anything this project downloaded.
# ---------------------------------------------------------------------------

param(
    [switch]$Clean,
    [switch]$Run,
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"

$desktop = Split-Path -Parent $PSScriptRoot
$repo    = Split-Path -Parent $desktop

# --- locate a compiler -----------------------------------------------------
$candidates = @(
    (Join-Path $repo ".toolchain\w64devkit\bin"),
    "C:\workenv\w64devkit\bin"
)

$haveGxx = $null -ne (Get-Command g++ -ErrorAction SilentlyContinue)
if (-not $haveGxx) {
    foreach ($dir in $candidates) {
        if (Test-Path (Join-Path $dir "g++.exe")) {
            $env:PATH = "$dir;$env:PATH"
            $haveGxx = $true
            break
        }
    }
}

if (-not $haveGxx) {
    Write-Host "No C++ compiler found." -ForegroundColor Red
    Write-Host "Run bootstrap.cmd in the repository root, or put g++ on PATH."
    exit 1
}

$make = Get-Command make -ErrorAction SilentlyContinue
if (-not $make) {
    Write-Host "make not found. It ships with w64devkit; check your PATH." -ForegroundColor Red
    exit 1
}

# --- build -----------------------------------------------------------------
Push-Location $desktop
try {
    if ($Clean) {
        Write-Host "Cleaning" -ForegroundColor Yellow
        & make clean
    }

    $target = if ($SelfTest) { "selftest" } else { "all" }
    Write-Host "Building ($target)" -ForegroundColor Cyan
    & make $target
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Pop-Location
}

$exe = Join-Path $desktop "build\sysmon.exe"
Write-Host "Built $exe" -ForegroundColor Green

if ($Run) {
    Write-Host "Launching (expect a UAC prompt)" -ForegroundColor Cyan
    Start-Process -FilePath $exe -Verb RunAs
}
