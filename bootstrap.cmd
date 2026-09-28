@echo off
rem ===========================================================================
rem  Multi Screen System Monitor -- toolchain bootstrap (Windows)
rem
rem  Downloads everything needed to build both halves into .toolchain\, so
rem  nothing is installed system-wide and nothing outside this folder is touched.
rem  Already have a toolchain? build.cmd finds g++ and arduino-cli on PATH and
rem  skips all of this.
rem
rem    bootstrap.cmd        ask before each download
rem    bootstrap.cmd -y     take everything without asking (for CI)
rem
rem  Uses only curl.exe and tar.exe, both shipped with Windows 10 1803 and later.
rem ===========================================================================
rem NOTE: deliberately NO "setlocal enabledelayedexpansion". With it enabled cmd
rem treats ! as a variable reference, so any path containing one -- and this
rem project lives under !ARDUINO -- silently expands to nothing. A :confirm
rem subroutine is used instead of reading !REPLY! inside a block.
setlocal

set "ROOT=%~dp0"
set "TC=%ROOT%.toolchain"
set "ASSUME_YES="
if /i "%~1"=="-y" set "ASSUME_YES=1"

rem Pinned on purpose. A build script that silently tracks "latest" stops being
rem reproducible the day upstream changes a flag. See README to bump these.
set "W64_VER=2.10.0"
set "W64_URL=https://github.com/skeeto/w64devkit/releases/download/v%W64_VER%/w64devkit-x64-%W64_VER%.7z.exe"
set "ACLI_URL=https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip"

where curl.exe >nul 2>&1 || (
    echo ERROR: curl.exe not found. Windows 10 1803 or later is required.
    exit /b 1
)
where tar.exe >nul 2>&1 || (
    echo ERROR: tar.exe not found. Windows 10 1803 or later is required.
    exit /b 1
)

if not exist "%TC%" mkdir "%TC%"

rem ---------------------------------------------------------------------------
rem  arduino-cli -- needed for the firmware
rem ---------------------------------------------------------------------------
if exist "%TC%\arduino-cli\arduino-cli.exe" (
    echo [ok] arduino-cli already present
) else (
    echo.
    echo About to download arduino-cli:
    echo   %ACLI_URL%
    call :confirm "Proceed?"
    if errorlevel 1 (
        echo Skipped.
        goto :after_acli
    )
    mkdir "%TC%\arduino-cli" 2>nul
    curl -L --fail --progress-bar -o "%TC%\arduino-cli.zip" "%ACLI_URL%" || (
        echo ERROR: download failed.
        exit /b 1
    )
    tar -xf "%TC%\arduino-cli.zip" -C "%TC%\arduino-cli" || (
        echo ERROR: could not unpack arduino-cli.
        exit /b 1
    )
    del "%TC%\arduino-cli.zip" >nul 2>&1
    echo [ok] arduino-cli installed
)
:after_acli

rem The AVR core is what actually compiles the sketch. Keeping its data inside
rem .toolchain too means this never disturbs an existing Arduino IDE setup.
if exist "%TC%\arduino-cli\arduino-cli.exe" (
    echo Installing the arduino:avr core ^(this is the compiler for the board^)...
    set "ARDUINO_DIRECTORIES_DATA=%TC%\arduino-data"
    set "ARDUINO_DIRECTORIES_DOWNLOADS=%TC%\arduino-data\staging"
    "%TC%\arduino-cli\arduino-cli.exe" core update-index >nul
    "%TC%\arduino-cli\arduino-cli.exe" core install arduino:avr
    echo [ok] arduino:avr core ready
)

rem ---------------------------------------------------------------------------
rem  w64devkit -- the C++ compiler for the desktop agent
rem ---------------------------------------------------------------------------
if exist "%TC%\w64devkit\bin\g++.exe" (
    echo [ok] w64devkit already present
    goto :done
)

where g++.exe >nul 2>&1 && (
    echo [ok] a g++ is already on PATH; skipping w64devkit
    goto :done
)

echo.
echo w64devkit is distributed ONLY as a self-extracting archive -- upstream
echo stopped publishing a plain .zip, so unpacking it means running the
echo downloaded file. It is extracted with 7-Zip SFX switches into
echo   %TC%\w64devkit
echo and nothing is installed or written outside that folder.
echo.
echo   %W64_URL%
echo.
echo If you would rather not run a downloaded executable, install any
echo MinGW-w64 toolchain yourself and put g++ on PATH; build.cmd will use it.
echo.
call :confirm "Download and extract w64devkit?"
if errorlevel 1 (
    echo Skipped. The firmware will still build; the desktop agent will not.
    goto :done
)

curl -L --fail --progress-bar -o "%TC%\w64devkit.exe" "%W64_URL%" || (
    echo ERROR: download failed.
    exit /b 1
)
echo Extracting...
rem -y accept, -o target. The SFX writes w64devkit\ underneath.
"%TC%\w64devkit.exe" -y -o"%TC%" >nul || (
    echo ERROR: extraction failed.
    exit /b 1
)
del "%TC%\w64devkit.exe" >nul 2>&1

if exist "%TC%\w64devkit\bin\g++.exe" (
    echo [ok] w64devkit installed
) else (
    echo ERROR: extraction finished but g++ is missing.
    exit /b 1
)

:done
echo.
echo Bootstrap complete. Now run:  build.cmd
exit /b 0

rem ---------------------------------------------------------------------------
rem  Asks a yes/no question. Returns 0 for yes, 1 for no. A subroutine rather
rem  than an inline set /p because reading a variable inside an if-block needs
rem  delayed expansion, which cannot be enabled here (see the note at the top).
rem ---------------------------------------------------------------------------
:confirm
if defined ASSUME_YES exit /b 0
set "REPLY="
set /p "REPLY=%~1 [y/N] "
if /i "%REPLY%"=="y" exit /b 0
if /i "%REPLY%"=="yes" exit /b 0
exit /b 1
