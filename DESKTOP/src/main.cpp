// ---------------------------------------------------------------------------
//  main -- entry point, elevation check, icon export.
//
//  The manifest already asks for requireAdministrator, so on a normal launch
//  Windows shows the UAC prompt and the check below passes. It still runs, because
//  the manifest can be bypassed: a debugger, a scheduled task registered without
//  HIGHEST, or a rebuild whose resource did not get embedded. The explicit check
//  is what the requirement asks for; the manifest just makes the usual path
//  pleasant instead of showing an error the user cannot act on.
//
//  wWinMain rather than wmain: this is a GUI-subsystem binary built with
//  -municode, so MinGW's startup looks for wWinMainCRTStartup -> wWinMain. Using
//  wmain here links but leaves an unresolved entry point.
// ---------------------------------------------------------------------------
#include <windows.h>
#include <shellapi.h>

#include <cwchar>

#include "App.h"
#include "IconFactory.h"

namespace {

// Writes the multi-size .ico used as the executable's resource. Build-time only:
// keeps the embedded icon identical to the one drawn at runtime.
int exportIcon(int argc, wchar_t **argv) {
    if (argc < 3) {
        MessageBoxW(nullptr, L"Usage: sysmon.exe --export-icon <path.ico>",
                    L"System Monitor", MB_ICONERROR | MB_OK);
        return 2;
    }
    const bool ok = icons::writeIcoFile(argv[2]);
    if (!ok) {
        MessageBoxW(nullptr, L"Could not write the icon file.", L"System Monitor",
                    MB_ICONERROR | MB_OK);
    }
    return ok ? 0 : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    // wcscmp rather than constructing a std::wstring: no allocation for a single
    // comparison, and it sidesteps a gcc -Wfree-nonheap-object false positive on
    // the temporary.
    if (argv && argc >= 2 && wcscmp(argv[1], L"--export-icon") == 0) {
        const int rc = exportIcon(argc, argv);
        LocalFree(argv);
        return rc;
    }
    if (argv) LocalFree(argv);

    if (!isRunningElevated()) {
        MessageBoxW(nullptr,
                    L"System Monitor must be run as administrator.\n\n"
                    L"It reads per-process CPU and memory figures for every process "
                    L"on the machine, which Windows only permits to an elevated "
                    L"process. Without it, services and processes owned by other "
                    L"users would be missing from the list.\n\n"
                    L"Right-click the executable and choose \"Run as administrator\", "
                    L"or enable Autostart in Settings to have it start elevated at "
                    L"logon.",
                    L"System Monitor - administrator required", MB_ICONERROR | MB_OK);
        return 1;
    }

    App app;
    if (!app.init(instance)) return 1;
    return app.run();
}
