# ---------------------------------------------------------------------------
#  Multi Screen System Monitor -- desktop agent
#
#  Plain C++20 on Win32. No frameworks, no vcpkg, no package manager: everything
#  it needs is in the MinGW-w64 sysroot that ships with w64devkit, and the two
#  optional data sources (NVML, Afterburner/RTSS shared memory) are reached at
#  runtime rather than linked, so the binary starts on a machine that has neither.
#
#  Build:  tools\build.ps1
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)
project(ardu-sys-monitor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
# Target Windows 10: GetIfTable2 and the InterfaceAndOperStatusFlags bitfield
# need 0x0A00, and PDH's English-counter entry points need a recent SDK level.
# NTDDI_VERSION matters as much as _WIN32_WINNT here: iphlpapi.h gates its
# include of netioapi.h on NTDDI, so without it GetIfTable2 silently vanishes.
add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000000
                        UNICODE _UNICODE)

set(SOURCES
    src/main.cpp
    src/App.cpp
    src/TrayIcon.cpp
    src/IconFactory.cpp
    src/SettingsDialog.cpp
    src/Settings.cpp
    src/Autostart.cpp
    src/SerialPort.cpp
    src/Protocol.cpp
    src/Monitor.cpp
    src/sensors/SensorHub.cpp
    src/sensors/CpuSensor.cpp
    src/sensors/MemorySensor.cpp
    src/sensors/NetworkSensor.cpp
    src/sensors/ProcessSensor.cpp
    src/sensors/NvmlGpu.cpp
    src/sensors/AfterburnerBridge.cpp
    src/sensors/RtssBridge.cpp
)

add_executable(sysmon WIN32 ${SOURCES} resources/app.rc)

target_include_directories(sysmon PRIVATE src)

target_link_libraries(sysmon PRIVATE
    pdh          # CPU counters
    psapi        # memory and per-process figures
    iphlpapi     # GetIfTable2
    setupapi     # COM port enumeration
    comctl32     # the settings dialog
    gdi32        # icon rendering
    shell32      # tray icon
    ole32        # COM, for the Task Scheduler
    oleaut32
    uuid
    taskschd     # autostart that survives the UAC prompt
)

# The app must run elevated, and Windows decides that from the embedded manifest,
# not from how it was launched. requireAdministrator makes Explorer put up the UAC
# prompt on a normal double-click -- without it the app would just refuse to start
# and tell the user to try again as admin, which is a worse experience.
target_link_options(sysmon PRIVATE
    "-Wl,--subsystem,windows"
    "-municode"
)

# Keep the shared wire contract in sync at configure time, so a stale copy cannot
# silently ship.
add_custom_target(sync-wire ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/../SHARED/sysmon_wire.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sysmon_wire.h
    COMMENT "Syncing SHARED/sysmon_wire.h into the desktop build"
)
add_dependencies(sysmon sync-wire)
