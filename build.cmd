@echo off
rem ===========================================================================
rem  Multi Screen System Monitor -- build both halves (Windows)
rem
rem    build.cmd              build desktop agent + firmware
rem    build.cmd desktop      just the agent
rem    build.cmd firmware     just the firmware
rem    build.cmd upload COM20 build the firmware and flash it
rem    build.cmd clean
rem
rem  Toolchains are looked for in this order, so an existing setup is always
rem  preferred over anything this project downloaded:
rem      1. PATH
rem      2. .toolchain\          (created by bootstrap.cmd)
rem      3. C:\workenv\          (a common local layout)
rem      4. the Arduino IDE 2 installation, for arduino-cli
rem
rem  NOTE: deliberately NO "setlocal enabledelayedexpansion". With it enabled cmd
rem  treats ! as a variable reference, so any path containing one -- and this
rem  project lives under !ARDUINO -- silently expands to nothing and every path
rem  in the script breaks. Subroutines are used instead of !var! where a value
rem  has to be read inside a block.
rem ===========================================================================
setlocal

set "ROOT=%~dp0"
set "TC=%ROOT%.toolchain"
set "TARGET=%~1"
set "PORT=%~2"
if "%TARGET%"=="" set "TARGET=all"

set "FQBN=arduino:avr:mega:cpu=atmega1280"

rem The core default receive buffer is 64 bytes, about 5.5 ms of stream at
rem 115200. A graph repaint takes ~45 ms, so a frame arriving during one would be
rem truncated and dropped. The core is compiled with this flag too, hence a cache
rem path keyed to the size -- mixing sizes leaves HardwareSerial disagreeing with
rem its own cached object, and the error says nothing about a stale cache.
set "RXBUF=768"
set "CACHE=%TEMP%\sysmon-core-cache-%RXBUF%"
set "BUILDDIR=%TEMP%\sysmon-build-%RXBUF%"

rem --- locate g++ ------------------------------------------------------------
set "GXX="
where g++.exe >nul 2>&1 && set "GXX=g++.exe"
if not defined GXX if exist "%TC%\w64devkit\bin\g++.exe" (
    set "GXX=%TC%\w64devkit\bin\g++.exe"
    set "PATH=%TC%\w64devkit\bin;%PATH%"
)
if not defined GXX if exist "C:\workenv\w64devkit\bin\g++.exe" (
    set "GXX=C:\workenv\w64devkit\bin\g++.exe"
    set "PATH=C:\workenv\w64devkit\bin;%PATH%"
)

rem --- locate arduino-cli ----------------------------------------------------
set "ACLI="
where arduino-cli.exe >nul 2>&1 && set "ACLI=arduino-cli.exe"
if not defined ACLI if exist "%TC%\arduino-cli\arduino-cli.exe" set "ACLI=%TC%\arduino-cli\arduino-cli.exe"
if not defined ACLI if exist "C:\workenv\arduino-cli\arduino-cli.exe" set "ACLI=C:\workenv\arduino-cli\arduino-cli.exe"
if not defined ACLI if exist "%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" set "ACLI=%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"

rem Keep arduino-cli's data beside the tool when we installed it ourselves, so
rem this never disturbs an existing Arduino IDE configuration.
if exist "%TC%\arduino-data" (
    set "ARDUINO_DIRECTORIES_DATA=%TC%\arduino-data"
    set "ARDUINO_DIRECTORIES_DOWNLOADS=%TC%\arduino-data\staging"
)

if /i "%TARGET%"=="clean"    goto :clean
if /i "%TARGET%"=="desktop"  goto :only_desktop
if /i "%TARGET%"=="firmware" goto :only_firmware
if /i "%TARGET%"=="upload"   goto :do_upload
goto :all

rem ---------------------------------------------------------------------------
:all
call :desktop
if errorlevel 1 exit /b 1
call :firmware
if errorlevel 1 exit /b 1
echo.
echo ===========================================================
echo  Both halves built.
echo    agent     DESKTOP\build\sysmon.exe   ^(run as administrator^)
echo    firmware  flash with:  build.cmd upload COM20
echo ===========================================================
exit /b 0

:only_desktop
call :desktop
exit /b %errorlevel%

:only_firmware
call :firmware
exit /b %errorlevel%

:do_upload
call :firmware
if errorlevel 1 exit /b 1
call :upload
exit /b %errorlevel%

rem ---------------------------------------------------------------------------
:desktop
if not defined GXX (
    echo.
    echo ERROR: no C++ compiler found.
    echo Run bootstrap.cmd, or install MinGW-w64 and put g++ on PATH.
    exit /b 1
)
echo === desktop agent =========================================
echo using %GXX%
pushd "%ROOT%DESKTOP" || exit /b 1
make
set "RC=%errorlevel%"
popd
exit /b %RC%

rem ---------------------------------------------------------------------------
:firmware
if not defined ACLI (
    echo.
    echo ERROR: arduino-cli not found.
    echo Run bootstrap.cmd, or install the Arduino IDE.
    exit /b 1
)
echo.
echo === display firmware ======================================
echo using %ACLI%
"%ACLI%" compile --fqbn %FQBN% --build-property "build.extra_flags=-DSERIAL_RX_BUFFER_SIZE=%RXBUF%" --build-cache-path "%CACHE%" --build-path "%BUILDDIR%" "%ROOT%ARDUINO\SysMonitor"
exit /b %errorlevel%

rem ---------------------------------------------------------------------------
:upload
if "%PORT%"=="" (
    echo.
    echo ERROR: no port given.  Usage:  build.cmd upload COM20
    echo Ports currently present:
    "%ACLI%" board list
    exit /b 1
)
echo.
echo Flashing to %PORT% ...
rem The agent holds the port while it runs, and avrdude cannot open a port that
rem is already in use.
taskkill /IM sysmon.exe /F >nul 2>&1
"%ACLI%" upload -p %PORT% --fqbn %FQBN% --input-dir "%BUILDDIR%" "%ROOT%ARDUINO\SysMonitor"
if errorlevel 1 exit /b 1
echo Flashed.
exit /b 0

rem ---------------------------------------------------------------------------
:clean
echo Cleaning...
if exist "%ROOT%DESKTOP\build" rmdir /s /q "%ROOT%DESKTOP\build"
if exist "%BUILDDIR%" rmdir /s /q "%BUILDDIR%"
if exist "%CACHE%" rmdir /s /q "%CACHE%"
echo Done.
exit /b 0
