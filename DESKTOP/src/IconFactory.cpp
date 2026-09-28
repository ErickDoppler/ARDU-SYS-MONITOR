#include "IconFactory.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

namespace icons {
namespace {

struct Pixel {  // memory order for a Windows 32bpp DIB
    uint8_t b, g, r, a;
};

// --- palette --------------------------------------------------------------
constexpr Pixel kBodyMid{0x3A, 0x34, 0x30, 0xFF};   // dark epoxy
constexpr Pixel kBodyLit{0x6E, 0x66, 0x60, 0xFF};   // top-left bevel
constexpr Pixel kBodyDim{0x1C, 0x18, 0x16, 0xFF};   // bottom-right bevel
constexpr Pixel kPinMid{0x9A, 0xA4, 0xA8, 0xFF};    // tinned leg
constexpr Pixel kPinLit{0xE2, 0xE8, 0xEA, 0xFF};
constexpr Pixel kPinDim{0x50, 0x58, 0x5C, 0xFF};
constexpr Pixel kDie{0x22, 0x1E, 0x1C, 0xFF};       // recessed centre
constexpr Pixel kOn{0xF9, 0x7F, 0x2D, 0xFF};        // BGR: blue  #2D7FF9
constexpr Pixel kOff{0x00, 0x00, 0x8B, 0xFF};       // BGR: dark red #8B0000

Pixel mix(Pixel a, Pixel b, float t) {
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    const auto lerp = [t](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>(x + (y - x) * t + 0.5f);
    };
    return Pixel{lerp(a.b, b.b), lerp(a.g, b.g), lerp(a.r, b.r), lerp(a.a, b.a)};
}

class Canvas {
public:
    Canvas(int w, int h) : m_w(w), m_h(h), m_px(static_cast<size_t>(w) * h, Pixel{0, 0, 0, 0}) {}

    void set(int x, int y, Pixel p) {
        if (x < 0 || y < 0 || x >= m_w || y >= m_h) return;
        m_px[static_cast<size_t>(y) * m_w + x] = p;
    }
    Pixel get(int x, int y) const {
        if (x < 0 || y < 0 || x >= m_w || y >= m_h) return Pixel{0, 0, 0, 0};
        return m_px[static_cast<size_t>(y) * m_w + x];
    }
    // Alpha-composites, so antialiased edges land on what is underneath.
    void blend(int x, int y, Pixel p) {
        if (x < 0 || y < 0 || x >= m_w || y >= m_h || p.a == 0) return;
        Pixel &d = m_px[static_cast<size_t>(y) * m_w + x];
        const float sa = p.a / 255.f;
        const float da = d.a / 255.f * (1.f - sa);
        const float oa = sa + da;
        if (oa <= 0.f) { d = Pixel{0, 0, 0, 0}; return; }
        d.b = static_cast<uint8_t>((p.b * sa + d.b * da) / oa + 0.5f);
        d.g = static_cast<uint8_t>((p.g * sa + d.g * da) / oa + 0.5f);
        d.r = static_cast<uint8_t>((p.r * sa + d.r * da) / oa + 0.5f);
        d.a = static_cast<uint8_t>(oa * 255.f + 0.5f);
    }

    int width() const { return m_w; }
    int height() const { return m_h; }
    const std::vector<Pixel> &pixels() const { return m_px; }
    std::vector<Pixel> &pixels() { return m_px; }

private:
    int m_w, m_h;
    std::vector<Pixel> m_px;
};

// Signed distance to a rounded rectangle: negative inside. Used for both the
// shape and, via its gradient, the bevel lighting.
float roundRectSdf(float px, float py, float cx, float cy, float hw, float hh, float r) {
    const float dx = std::fabs(px - cx) - (hw - r);
    const float dy = std::fabs(py - cy) - (hh - r);
    const float ax = dx > 0.f ? dx : 0.f;
    const float ay = dy > 0.f ? dy : 0.f;
    const float outside = std::sqrt(ax * ax + ay * ay);
    const float inside = std::fmin(std::fmax(dx, dy), 0.f);
    return outside + inside - r;
}

// Shades one shape: `lit` toward the light, `dim` away from it, so the edge reads
// as a chamfer instead of a flat outline. The light sits top-left.
Pixel bevelShade(float sdf, float bevel, float nx, float ny, Pixel mid, Pixel lit,
                 Pixel dim) {
    // How far into the edge band this pixel is: 1 at the rim, 0 once fully inside.
    const float depth = (bevel > 0.f) ? (1.f + sdf / bevel) : 0.f;
    const float edge = (depth < 0.f) ? 0.f : (depth > 1.f ? 1.f : depth);

    // Light direction, normalised.
    constexpr float lx = -0.55f, ly = -0.83f;
    const float facing = nx * lx + ny * ly;  // +1 lit rim, -1 shadowed rim

    if (facing >= 0.f) return mix(mid, lit, edge * facing);
    return mix(mid, dim, edge * -facing);
}

void drawRoundRect(Canvas &c, float cx, float cy, float hw, float hh, float r,
                   float bevel, Pixel mid, Pixel lit, Pixel dim) {
    const int x0 = static_cast<int>(std::floor(cx - hw - 1));
    const int x1 = static_cast<int>(std::ceil(cx + hw + 1));
    const int y0 = static_cast<int>(std::floor(cy - hh - 1));
    const int y1 = static_cast<int>(std::ceil(cy + hh + 1));

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float d = roundRectSdf(px, py, cx, cy, hw, hh, r);
            if (d > 0.7f) continue;

            // Central difference on the SDF gives the outward normal, which is
            // what makes the bevel follow the rounded corners correctly.
            const float e = 0.6f;
            float nx = roundRectSdf(px + e, py, cx, cy, hw, hh, r) -
                       roundRectSdf(px - e, py, cx, cy, hw, hh, r);
            float ny = roundRectSdf(px, py + e, cx, cy, hw, hh, r) -
                       roundRectSdf(px, py - e, cx, cy, hw, hh, r);
            const float len = std::sqrt(nx * nx + ny * ny);
            if (len > 0.0001f) { nx /= len; ny /= len; }

            Pixel p = bevelShade(d, bevel, nx, ny, mid, lit, dim);
            // Antialias the outer 0.7 px.
            const float cov = (d < -0.3f) ? 1.f : (0.7f - d) / 1.0f;
            p.a = static_cast<uint8_t>(255.f * (cov < 0.f ? 0.f : (cov > 1.f ? 1.f : cov)));
            c.blend(x, y, p);
        }
    }
}

// Renders text centred in a box using GDI, then composites it as a coverage mask
// so the result keeps proper alpha. GDI cannot draw into an alpha channel, hence
// the detour through a black-and-white scratch bitmap.
void drawCenteredText(Canvas &c, const wchar_t *text, float cx, float cy, int pxHeight,
                      Pixel colour, float maxWidth) {
    if (pxHeight < 4) return;

    const int w = c.width(), h = c.height();
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!dc) return;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(dc); return; }

    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    std::memset(bits, 0, static_cast<size_t>(w) * h * 4);

    const int len = static_cast<int>(wcslen(text));

    // Fit the label to the space it has to live in. "OFF" is half again as wide
    // as "ON" at the same point size, so a single fixed size either overflows on
    // three glyphs or wastes the die on two. Measure, then shrink to fit.
    const auto makeFont = [](int px) {
        return CreateFontW(-px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                           FF_SWISS, L"Segoe UI");
    };

    HFONT font = makeFont(pxHeight);
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));

    SIZE ext{};
    GetTextExtentPoint32W(dc, text, len, &ext);

    if (maxWidth > 0.f && ext.cx > maxWidth) {
        int fitted = static_cast<int>(pxHeight * (maxWidth / static_cast<float>(ext.cx)));
        if (fitted < 4) fitted = 4;
        SelectObject(dc, oldFont);
        DeleteObject(font);
        font = makeFont(fitted);
        oldFont = SelectObject(dc, font);
        GetTextExtentPoint32W(dc, text, len, &ext);
    }

    TextOutW(dc, static_cast<int>(cx - ext.cx / 2.f), static_cast<int>(cy - ext.cy / 2.f),
             text, len);

    // Composite: the green channel is the coverage GDI produced.
    const auto *src = static_cast<const uint8_t *>(bits);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t cov = src[(static_cast<size_t>(y) * w + x) * 4 + 1];
            if (cov == 0) continue;
            Pixel p = colour;
            p.a = cov;
            c.blend(x, y, p);
        }
    }

    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

enum class State { Plain, On, Off };

// The artwork. Laid out in fractions of the icon so every size is consistent.
Canvas renderChip(int size, State state) {
    Canvas c(size, size);

    const float s = static_cast<float>(size);
    const float cx = s * 0.5f, cy = s * 0.5f;

    // Body occupies the middle; pins stick out either side.
    const float bodyHw = s * 0.30f;
    const float bodyHh = s * 0.32f;
    const float radius = std::fmax(1.0f, s * 0.05f);
    const float bevel = std::fmax(1.0f, s * 0.09f);

    // --- pins, behind the body so the body edge overlaps them ---
    const int pinCount = (size >= 32) ? 5 : 3;
    const float pinLen = s * 0.115f;
    const float pinHh = std::fmax(0.6f, s * 0.035f);
    const float pinSpan = bodyHh * 1.42f;
    for (int i = 0; i < pinCount; i++) {
        const float t = (pinCount == 1) ? 0.5f : static_cast<float>(i) / (pinCount - 1);
        const float py = cy - pinSpan * 0.5f + pinSpan * t;
        // Slight overlap into the body keeps a gap from opening up at 16 px.
        drawRoundRect(c, cx - bodyHw - pinLen * 0.45f, py, pinLen, pinHh,
                      std::fmin(pinHh, 1.0f), std::fmax(0.6f, s * 0.03f), kPinMid,
                      kPinLit, kPinDim);
        drawRoundRect(c, cx + bodyHw + pinLen * 0.45f, py, pinLen, pinHh,
                      std::fmin(pinHh, 1.0f), std::fmax(0.6f, s * 0.03f), kPinMid,
                      kPinLit, kPinDim);
    }

    // --- body ---
    drawRoundRect(c, cx, cy, bodyHw, bodyHh, radius, bevel, kBodyMid, kBodyLit, kBodyDim);

    // --- recessed die: same bevel with the light inverted, so it reads as a
    //     depression rather than another raised block ---
    const float dieHw = bodyHw * 0.62f;
    const float dieHh = bodyHh * 0.58f;
    if (size >= 20) {
        drawRoundRect(c, cx, cy, dieHw, dieHh, std::fmax(0.5f, s * 0.02f),
                      std::fmax(0.8f, s * 0.05f), kDie, kBodyDim, kBodyLit);
    }

    // --- pin-1 orientation dot, the detail that makes it read as a chip ---
    if (size >= 24) {
        const float dotR = std::fmax(0.8f, s * 0.035f);
        drawRoundRect(c, cx - bodyHw * 0.62f, cy - bodyHh * 0.62f, dotR, dotR, dotR,
                      dotR, kPinLit, kPinLit, kPinMid);
    }

    // --- link state ---
    if (state != State::Plain) {
        const Pixel colour = (state == State::On) ? kOn : kOff;
        const wchar_t *label = (state == State::On) ? L"ON" : L"OFF";

        if (size >= 20) {
            // A wider panel than the plain die, so three glyphs have somewhere to
            // sit without crowding the body's bevel.
            const float padHw = bodyHw * 0.86f;
            const float padHh = bodyHh * 0.46f;
            drawRoundRect(c, cx, cy, padHw, padHh, std::fmax(0.5f, s * 0.02f),
                          std::fmax(0.8f, s * 0.05f), mix(kDie, colour, 0.62f), colour,
                          mix(colour, kDie, 0.55f));

            const int fontPx = static_cast<int>(s * 0.34f);
            drawCenteredText(c, label, cx, cy, fontPx, Pixel{255, 255, 255, 255},
                             padHw * 1.82f);
        } else {
            // At 16 px there is no room for three legible glyphs, so the whole die
            // becomes the indicator. Colour still carries the meaning.
            drawRoundRect(c, cx, cy, dieHw * 1.05f, dieHh * 1.05f,
                          std::fmax(0.5f, s * 0.02f), std::fmax(0.8f, s * 0.06f),
                          colour, mix(colour, Pixel{255, 255, 255, 255}, 0.45f),
                          mix(colour, kDie, 0.5f));
        }
    }

    return c;
}

// Wraps an ARGB canvas as an HICON. Windows wants premultiplied alpha here, and
// the mask bitmap must exist even though the alpha channel does the real work.
HICON toHicon(const Canvas &c) {
    const int w = c.width(), h = c.height();

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = w;
    bi.bV5Height = -h;  // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC dc = GetDC(nullptr);
    void *bits = nullptr;
    HBITMAP colour = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO *>(&bi),
                                      DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!colour || !bits) {
        if (colour) DeleteObject(colour);
        return nullptr;
    }

    auto *dst = static_cast<Pixel *>(bits);
    for (size_t i = 0; i < c.pixels().size(); i++) {
        Pixel p = c.pixels()[i];
        const float a = p.a / 255.f;
        dst[i].b = static_cast<uint8_t>(p.b * a + 0.5f);
        dst[i].g = static_cast<uint8_t>(p.g * a + 0.5f);
        dst[i].r = static_cast<uint8_t>(p.r * a + 0.5f);
        dst[i].a = p.a;
    }

    HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = colour;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(colour);
    DeleteObject(mask);
    return icon;
}

// --- .ico writing ---------------------------------------------------------
void pushU16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void pushU32(std::vector<uint8_t> &v, uint32_t x) {
    for (int i = 0; i < 4; i++) v.push_back(static_cast<uint8_t>((x >> (i * 8)) & 0xFF));
}

// One ICO image: BITMAPINFOHEADER with doubled height, bottom-up BGRA, then the
// 1bpp AND mask. The mask is all-zero because alpha does the shaping, but it must
// still be present and row-padded to 4 bytes or the icon is rejected.
std::vector<uint8_t> encodeIcoImage(const Canvas &c) {
    const int w = c.width(), h = c.height();
    std::vector<uint8_t> out;

    pushU32(out, 40);                             // biSize
    pushU32(out, static_cast<uint32_t>(w));       // biWidth
    pushU32(out, static_cast<uint32_t>(h * 2));   // biHeight, XOR + AND
    pushU16(out, 1);                              // biPlanes
    pushU16(out, 32);                             // biBitCount
    pushU32(out, 0);                              // BI_RGB
    pushU32(out, 0);                              // biSizeImage
    pushU32(out, 0); pushU32(out, 0);             // resolution
    pushU32(out, 0); pushU32(out, 0);             // palette

    for (int y = h - 1; y >= 0; y--) {            // bottom-up
        for (int x = 0; x < w; x++) {
            const Pixel p = c.get(x, y);
            out.push_back(p.b);
            out.push_back(p.g);
            out.push_back(p.r);
            out.push_back(p.a);
        }
    }

    const int maskStride = ((w + 31) / 32) * 4;
    out.insert(out.end(), static_cast<size_t>(maskStride) * h, 0);
    return out;
}

}  // namespace

HICON createAppIcon(int size) { return toHicon(renderChip(size, State::Plain)); }

HICON createTrayIcon(int size, bool connected) {
    return toHicon(renderChip(size, connected ? State::On : State::Off));
}

bool writeIcoFile(const wchar_t *path) {
    // No 256 px entry: it would add a quarter of a megabyte of uncompressed
    // bitmap to the executable for a size a tray utility never shows.
    const int sizes[] = {16, 20, 24, 32, 48, 64, 128};
    const int count = static_cast<int>(sizeof(sizes) / sizeof(sizes[0]));

    std::vector<std::vector<uint8_t>> images;
    images.reserve(count);
    for (int s : sizes) images.push_back(encodeIcoImage(renderChip(s, State::Plain)));

    std::vector<uint8_t> file;
    pushU16(file, 0);                                   // reserved
    pushU16(file, 1);                                   // type: icon
    pushU16(file, static_cast<uint16_t>(count));

    uint32_t offset = 6 + 16u * count;
    for (int i = 0; i < count; i++) {
        const int s = sizes[i];
        file.push_back(static_cast<uint8_t>(s == 256 ? 0 : s));  // 0 means 256
        file.push_back(static_cast<uint8_t>(s == 256 ? 0 : s));
        file.push_back(0);                              // palette entries
        file.push_back(0);                              // reserved
        pushU16(file, 1);                               // planes
        pushU16(file, 32);                              // bit count
        pushU32(file, static_cast<uint32_t>(images[i].size()));
        pushU32(file, offset);
        offset += static_cast<uint32_t>(images[i].size());
    }
    for (auto &img : images) file.insert(file.end(), img.begin(), img.end());

    FILE *f = _wfopen(path, L"wb");
    if (!f) return false;
    const size_t wrote = fwrite(file.data(), 1, file.size(), f);
    fclose(f);
    return wrote == file.size();
}

}  // namespace icons
