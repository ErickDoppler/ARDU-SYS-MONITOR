// ---------------------------------------------------------------------------
//  mkicon -- bootstrap tool that writes resources/appicon.ico.
//
//  Exists to break a chicken-and-egg problem: app.rc embeds appicon.ico, so the
//  main build cannot be the thing that produces it. This links only IconFactory,
//  needs no resource, and emits the same artwork the running app draws.
//
//  Built and run by tools/build.ps1 when the .ico is missing or older than
//  IconFactory.cpp.
// ---------------------------------------------------------------------------
#include <windows.h>
#include <stdio.h>

#include "../src/IconFactory.h"

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: mkicon <out.ico>\n");
        return 2;
    }
    if (!icons::writeIcoFile(argv[1])) {
        fprintf(stderr, "failed to write the icon\n");
        return 1;
    }
    printf("wrote the icon\n");
    return 0;
}
