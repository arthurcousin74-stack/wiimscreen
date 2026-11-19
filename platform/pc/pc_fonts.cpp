// Computer builds: the interface fonts at any pixel size, made at run time from DejaVu Sans (the TTF that the
// Tab5's VLW fonts come from, tools/make_vlw.py). With a canvas larger than 1280x720 (a big window, a full HD
// screen), src/ui/ui_core.cpp asks for the fonts scaled like the layout, so the text keeps its proportions and stays
// sharp instead of the picture being stretched. Same VLW format and characters as make_vlw.py; the glyphs are
// rasterised by stb_truetype (platform/pc/third_party, public domain / MIT), not FreeType, so they can differ from
// the built-in fonts by a pixel here and there. At 1280x720 and smaller, the built-in fonts are used (screens and
// golden images unchanged).
//
// The TTF is read from: the program's folder (Windows zip: DejaVuSans.ttf beside wiimscreen.exe), the repository
// (fonts/DejaVuSans.ttf, for .pio/build/<env>/program), the current folder, then the system (Debian, Raspberry Pi
// OS: fonts-dejavu-core). Not found: pcScaledFont() returns nullptr and the built-in fonts stay.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"  // -isystem platform/pc/third_party (its warnings are not the project's)

#include "pc.h"

namespace {

std::mutex g_mutex;
bool g_tried = false;
std::vector<unsigned char> g_ttf;
stbtt_fontinfo g_info;
std::map<int, std::vector<uint8_t>> g_fonts;  // pixel size -> VLW data (kept: M5GFX reads the glyphs from it)

bool loadTtf() {
    if (g_tried) return !g_ttf.empty();
    g_tried = true;
    const std::string dir = pc::programDir();
    const std::string paths[] = {dir + "DejaVuSans.ttf", dir + "../../../fonts/DejaVuSans.ttf", "fonts/DejaVuSans.ttf",
                                 "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
    for (const std::string& p : paths) {
        std::ifstream f(p, std::ios::binary);
        if (!f) continue;
        std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (data.size() < 1024) continue;
        const int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
        if (offset < 0) continue;
        g_ttf = std::move(data);  // stbtt_fontinfo points into it: set up after the move
        if (stbtt_InitFont(&g_info, g_ttf.data(), offset)) {
            if (!pc::quiet()) printf("polices : %s\n", p.c_str());
            return true;
        }
        g_ttf.clear();
    }
    fprintf(stderr, "polices : DejaVuSans.ttf introuvable, polices de 1280x720\n");
    return false;
}

void put32(std::vector<uint8_t>& out, int32_t v) {
    const uint32_t u = (uint32_t)v;
    out.push_back((uint8_t)(u >> 24));
    out.push_back((uint8_t)(u >> 16));
    out.push_back((uint8_t)(u >> 8));
    out.push_back((uint8_t)u);
}

// The characters of tools/make_vlw.py (CHARS), in code point order
std::vector<int> charset() {
    std::vector<int> cps;
    for (int c = 0x20; c < 0x7F; ++c) cps.push_back(c);
    for (int c = 0xA0; c < 0x180; ++c) cps.push_back(c);
    for (int c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E, 0x2022, 0x2026, 0x20AC, 0x2122, 0x2212})
        cps.push_back(c);
    return cps;
}

// VLW (big-endian 32-bit integers): count, version 11, size, 0, ascent, descent; per glyph: code point, height,
// width, advance, dY (top above the baseline), dX (left offset), 0; then the 8-bit alpha bitmaps in glyph order.
std::vector<uint8_t> build(int px) {
    const float scale = stbtt_ScaleForMappingEmToPixels(&g_info, (float)px);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&g_info, &asc, &desc, &gap);
    const std::vector<int> cps = charset();
    std::vector<uint8_t> head, bitmaps;
    put32(head, (int32_t)cps.size());
    put32(head, 11);
    put32(head, px);
    put32(head, 0);
    put32(head, (int32_t)std::ceil(asc * scale));    // as FreeType (and so PIL) rounds them
    put32(head, (int32_t)std::ceil(-desc * scale));
    for (int cp : cps) {
        int advance = 0, lsb = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointHMetrics(&g_info, cp, &advance, &lsb);
        stbtt_GetCodepointBitmapBox(&g_info, cp, scale, scale, &x0, &y0, &x1, &y1);
        const int adv = (int)std::lround(advance * scale), w = x1 - x0, h = y1 - y0;
        if (cp == ' ' || w <= 0 || h <= 0) {
            for (int32_t v : {cp, 0, 0, adv, 0, 0, 0}) put32(head, v);
            continue;
        }
        const size_t at = bitmaps.size();
        bitmaps.resize(at + (size_t)w * h);
        stbtt_MakeCodepointBitmap(&g_info, bitmaps.data() + at, w, h, w, scale, scale, cp);
        for (int32_t v : {cp, h, w, adv, -y0, x0, 0}) put32(head, v);
    }
    head.insert(head.end(), bitmaps.begin(), bitmaps.end());
    return head;
}

}  // namespace

const uint8_t* pcScaledFont(int px) {
    if (px < 6 || px > 400) return nullptr;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!loadTtf()) return nullptr;
    std::vector<uint8_t>& f = g_fonts[px];
    if (f.empty()) f = build(px);
    return f.data();
}
