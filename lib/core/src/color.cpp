#include "color.h"

#include <algorithm>
#include <cmath>

namespace wr {

namespace {

void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    l = (mx + mn) / 2;
    if (mx == mn) {
        h = s = 0;
        return;
    }
    float d = mx - mn;
    s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h /= 6;
}

float hueToRgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6) return p + (q - p) * 6 * t;
    if (t < 1.0f / 2) return q;
    if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

uint32_t hslToRgb(float h, float s, float l) {
    float r, g, b;
    if (s == 0) {
        r = g = b = l;
    } else {
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
        r = hueToRgb(p, q, h + 1.0f / 3);
        g = hueToRgb(p, q, h);
        b = hueToRgb(p, q, h - 1.0f / 3);
    }
    auto c = [](float v) { return static_cast<uint32_t>(std::lround(std::max(0.f, std::min(1.f, v)) * 255)); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}

}  // namespace

uint32_t nameColor(const std::string& name) {
    uint32_t h = 2166136261u;  // FNV-1a
    for (unsigned char c : name) h = (h ^ c) * 16777619u;
    // Teintes saturées mais sombres : texte blanc lisible
    float hue = (h % 360) / 360.0f;
    return hslToRgb(hue, 0.50f, 0.40f);
}

uint32_t accentColor(const uint8_t* rgb, size_t n, uint32_t fallback) {
    double sr = 0, sg = 0, sb = 0, sw = 0;
    for (size_t i = 0; i < n; ++i) {
        int r = rgb[3 * i], g = rgb[3 * i + 1], b = rgb[3 * i + 2];
        int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
        if (mx < 40) continue;  // presque noir : ne dit rien de la teinte
        double sat = double(mx - mn) / mx, val = mx / 255.0;
        double w = sat * sat * val;  // les couleurs vives comptent le plus
        sr += w * r;
        sg += w * g;
        sb += w * b;
        sw += w;
    }
    if (n == 0 || sw < 0.02 * n) return fallback;
    float h, s, l;
    rgbToHsl(float(sr / sw / 255), float(sg / sw / 255), float(sb / sw / 255), h, s, l);
    s = std::max(s, 0.45f);
    l = std::max(0.55f, std::min(0.68f, l));
    return hslToRgb(h, s, l);
}

}  // namespace wr
