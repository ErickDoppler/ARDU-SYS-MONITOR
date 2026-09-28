// ---------------------------------------------------------------------------
//  mkpreview -- renders the icon variants into one BMP so the artwork can be
//  reviewed at a glance instead of being judged from code.
//
//  Writes a montage: app / tray-ON / tray-OFF, each at 128, 64, 32, 24 and 16 px,
//  on a mid grey so both the light bevel and the dark bevel are visible.
// ---------------------------------------------------------------------------
#include <windows.h>
#include <stdio.h>
#include <vector>

#include "../src/IconFactory.h"

namespace {

struct BGRA { unsigned char b, g, r, a; };

void blit(std::vector<BGRA> &canvas, int cw, int ch, HICON icon, int size, int ox, int oy) {
    if (!icon) return;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    memset(bits, 0, (size_t)size * size * 4);
    DrawIconEx(dc, 0, 0, icon, size, size, 0, NULL, DI_NORMAL);

    const BGRA *src = (const BGRA *)bits;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            const int dx = ox + x, dy = oy + y;
            if (dx < 0 || dy < 0 || dx >= cw || dy >= ch) continue;
            BGRA s = src[(size_t)y * size + x];
            // DrawIconEx gives premultiplied colour over the zeroed buffer, so
            // the alpha channel is the coverage to composite with.
            const float a = s.a / 255.0f;
            BGRA &d = canvas[(size_t)dy * cw + dx];
            d.b = (unsigned char)(s.b + d.b * (1 - a));
            d.g = (unsigned char)(s.g + d.g * (1 - a));
            d.r = (unsigned char)(s.r + d.r * (1 - a));
            d.a = 255;
        }
    }

    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);
}

bool writeBmp(const wchar_t *path, const std::vector<BGRA> &px, int w, int h) {
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    const DWORD dataSize = (DWORD)((size_t)w * h * 4);

    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + dataSize;

    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = h;   // bottom-up
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;

    FILE *f = _wfopen(path, L"wb");
    if (!f) return false;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    for (int y = h - 1; y >= 0; y--) fwrite(&px[(size_t)y * w], 4, (size_t)w, f);
    fclose(f);
    return true;
}

}  // namespace

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) { fprintf(stderr, "usage: mkpreview <out.bmp>\n"); return 2; }

    const int sizes[] = {128, 64, 32, 24, 16};
    const int nSizes = 5;
    const int pad = 12;

    int rowW = pad;
    for (int i = 0; i < nSizes; i++) rowW += sizes[i] + pad;
    const int w = rowW;
    const int h = pad + (128 + pad) * 3;

    std::vector<BGRA> canvas((size_t)w * h, BGRA{0x50, 0x50, 0x50, 255});

    for (int row = 0; row < 3; row++) {
        int x = pad;
        const int yTop = pad + (128 + pad) * row;
        for (int i = 0; i < nSizes; i++) {
            const int s = sizes[i];
            HICON ic = (row == 0) ? icons::createAppIcon(s)
                                  : icons::createTrayIcon(s, row == 1);
            // Bottom-align each size so the row reads as one baseline.
            blit(canvas, w, h, ic, s, x, yTop + (128 - s));
            if (ic) DestroyIcon(ic);
            x += s + pad;
        }
    }

    if (!writeBmp(argv[1], canvas, w, h)) { fprintf(stderr, "write failed\n"); return 1; }
    printf("wrote preview %dx%d (rows: app, ON, OFF)\n", w, h);
    return 0;
}
