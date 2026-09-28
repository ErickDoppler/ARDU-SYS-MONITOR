# ---------------------------------------------------------------------------
#  Builds the desktop agent with the w64devkit toolchain in C:\workenv.
#
#    .\tools\build.ps1              # configure if needed, then build
#    .\tools\build.ps1 -Clean       # throw the build directory away first
#    .\tools\build.ps1 -Run         # build, then launch (UAC will prompt)
#
#  Two stages, because resources/app.rc embeds appicon.ico and therefore cannot
#  be the thing that generates it: a small bootstrap linking only IconFactory
#  writes the .ico first, then CMake builds the app.
# ---------------------------------------------------------------------------

param(
    [switch]$Clean,
    [switch]$Run,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"

$devkit = "C:\workenv\w64devkit\bin"
$cmake  = "C:\workenv\cmake-4.4.3-windows-x86_64\bin\cmake.exe"
$ninja  = "C:\workenv\ninja\ninja.exe"

foreach ($p in @($devkit, $cmake, $ninja)) {
    if (-not (Test-Path $p)) {
        Write-Host "Missing toolchain component: $p" -ForegroundColor Red
        exit 1
    }
}

# Put the devkit first so CMake finds this gcc rather than anything else on PATH.
$env:PATH = "$devkit;$env:PATH"

if ($Clean -and (Test-Path $build)) {
    Write-Host "Removing $build" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $build
}

# --- stage 1: the icon ------------------------------------------------------
$ico        = Join-Path $root "resources\appicon.ico"
$iconSource = Join-Path $root "src\IconFactory.cpp"
$needIcon   = -not (Test-Path $ico)
if (-not $needIcon) {
    $needIcon = (Get-Item $iconSource).LastWriteTime -gt (Get-Item $ico).LastWriteTime
}

if ($needIcon) {
    Write-Host "Generating appicon.ico" -ForegroundColor Cyan
    $tmp = Join-Path $env:TEMP "sysmon-mkicon.exe"
    & "$devkit\g++.exe" -std=c++20 -O2 -municode `
        (Join-Path $root "tools\mkicon.cpp") `
        (Join-Path $root "src\IconFactory.cpp") `
        -o $tmp -lgdi32 -luser32
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $tmp $ico
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
} else {
    Write-Host "appicon.ico is up to date" -ForegroundColor DarkGray
}

# --- stage 2: the app -------------------------------------------------------
if (-not (Test-Path (Join-Path $build "build.ninja"))) {
    Write-Host "Configuring" -ForegroundColor Cyan
    & $cmake -S $root -B $build -G Ninja `
        "-DCMAKE_MAKE_PROGRAM=$ninja" `
        "-DCMAKE_BUILD_TYPE=$Config" `
        "-DCMAKE_C_COMPILER=$devkit\gcc.exe" `
        "-DCMAKE_CXX_COMPILER=$devkit\g++.exe" `
        "-DCMAKE_RC_COMPILER=$devkit\windres.exe"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "Building" -ForegroundColor Cyan
& $cmake --build $build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $build "sysmon.exe"
Write-Host "Built $exe" -ForegroundColor Green

if ($Run) {
    Write-Host "Launching (expect a UAC prompt)" -ForegroundColor Cyan
    Start-Process -FilePath $exe -Verb RunAs
}
