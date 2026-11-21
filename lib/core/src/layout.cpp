#include "layout.h"

#include "i18n.h"

#include <algorithm>

namespace wr {

Layout computeLayout(int width, int height, int presetCount) {
    Layout L;
    const int ref = std::min(width, height);  // 720 sur le Tab5 (référence de mise en page : 540)
    auto sc = [ref](int v) { return v * ref / 540; };
    const bool landscape = width >= height;
    const int m = sc(18), gap = sc(12);
    const int volH = sc(48), playD = sc(76), sideD = sc(60), progH = sc(30);

    // --- Barre de navigation : à gauche (paysage) ou en haut (portrait)
    const int railW = sc(76);
    if (landscape) {
        L.rail = Rect{0, 0, railW, height};
        L.navHome = Rect{(railW - sideD) / 2, m, sideD, sideD};
        L.navSettings = Rect{(railW - sideD) / 2, height - m - sideD, sideD, sideD};
        L.navSearch = Rect{(railW - sideD) / 2, (height - sideD) / 2, sideD, sideD};
        L.navLibrary = Rect{(railW - sideD) / 2, (L.navHome.y + L.navSearch.y) / 2, sideD, sideD};
        L.content = Rect{railW, 0, width - railW, height};
    } else {
        L.rail = Rect{0, 0, width, railW};
        L.navHome = Rect{m, (railW - sideD) / 2, sideD, sideD};
        L.navSettings = Rect{width - m - sideD, (railW - sideD) / 2, sideD, sideD};
        L.navSearch = Rect{(width - sideD) / 2, (railW - sideD) / 2, sideD, sideD};
        L.navLibrary = Rect{(L.navHome.x + L.navSearch.x) / 2, (railW - sideD) / 2, sideD, sideD};
        L.content = Rect{0, railW, width, height - railW};
    }
    const int X0 = landscape ? L.content.x - sc(4) : 0;  // la barre a déjà sa marge
    const int Y0 = L.content.y;
    const int W = width - X0;

    // --- Bande des presets : tuiles (image + nom), une ou deux rangées par page.
    // Toujours réservée, même sans preset (l'écran y explique comment en ajouter) : sinon
    // l'écran principal ressemble à la vue ambiance.
    const int tileH = sc(136), dotsH = sc(16);
    const int rows = landscape || presetCount <= 0 ? 1 : 2;
    const int minTileW = sc(130);
    const int cols = std::max(2, std::min(6, (W - 2 * m + gap) / (minTileW + gap)));
    L.perPage = presetCount > 0 ? cols * rows : 0;
    const int stripH = rows * tileH + (rows - 1) * gap;
    const int dotsY = height - m - dotsH;
    L.strip = Rect{X0 + m, dotsY - stripH, W - 2 * m, stripH};
    if (presetCount > 0) {
        L.dots = Rect{X0 + m, dotsY, W - 2 * m, dotsH};
        const int tileW = (W - 2 * m - (cols - 1) * gap) / cols;
        for (int i = 0; i < std::min(presetCount, L.perPage); ++i) {
            int r = i / cols, c = i % cols;
            L.presets.push_back(Rect{X0 + m + c * (tileW + gap), L.strip.y + r * (tileH + gap), tileW, tileH});
        }
    }
    const int topY = Y0 + m;
    const int topBottom = L.strip.y - gap;

    // --- Pile de droite (paysage) ou du bas (portrait) : hauteurs fixes
    const int infoMinH = sc(150);
    const int stackH = infoMinH + sc(6) + progH + sc(12) + playD + sc(12) + volH;

    int colX, colW, colY;
    if (landscape) {
        int side = std::min(topBottom - topY, W * 40 / 100);
        L.art = Rect{X0 + m, topY + (topBottom - topY - side) / 2, side, side};
        colX = L.art.right() + sc(28);
        colW = width - m - colX;
        colY = topY;
    } else {
        int side = std::min(W - 2 * m, topBottom - topY - gap - stackH);
        L.art = Rect{X0 + (W - side) / 2, topY, side, side};
        colX = X0 + m;
        colW = W - 2 * m;
        colY = L.art.bottom() + gap;
    }
    // Du bas vers le haut : volume, commandes, progression ; le reste pour les textes
    L.volume = Rect{colX, topBottom - volH, colW, volH};
    const int playCy = L.volume.y - sc(12) - playD / 2;
    const int cx = colX + colW / 2;
    L.transport[kPlayPause] = Rect{cx - playD / 2, playCy - playD / 2, playD, playD};
    L.transport[kPrev] = Rect{cx - playD / 2 - sc(30) - sideD, playCy - sideD / 2, sideD, sideD};
    L.transport[kNext] = Rect{cx + playD / 2 + sc(30), playCy - sideD / 2, sideD, sideD};
    L.fav = Rect{colX, playCy - sideD / 2, sideD, sideD};
    L.progress = Rect{colX, L.transport[kPlayPause].y - sc(12) - progH, colW, progH};
    L.info = Rect{colX, colY, colW, L.progress.y - sc(6) - colY};

    // Pilule de volume : [ − ] 🔈 ━━━━━●──── 42 [ + ]
    const int b = volH - sc(8);
    L.transport[kVolDown] = Rect{L.volume.x + sc(4), L.volume.y + sc(4), b, b};
    L.transport[kVolUp] = Rect{L.volume.right() - sc(4) - b, L.volume.y + sc(4), b, b};
    L.mute = Rect{L.transport[kVolDown].right() + sc(6), L.volume.y, sc(40), volH};
    const int valueW = sc(62);  // « Muet » doit tenir
    L.volBar = Rect{L.mute.right() + sc(10), L.volume.y,
                    L.transport[kVolUp].x - sc(10) - valueW - (L.mute.right() + sc(10)), volH};

    return L;
}

std::vector<Key> keyboardLayout(const Rect& area) {
    std::vector<Key> keys;
    const int g = std::max(4, area.w / 120);
    const int unit = (area.w - 9 * g) / 10, rowH = (area.h - 4 * g) / 5;
    auto add = [&](int row, int col, int span, const std::string& label, const std::string& text, Key::Kind kind) {
        Key k;
        k.r = Rect{area.x + col * (unit + g), area.y + row * (rowH + g), span * unit + (span - 1) * g, rowH};
        k.label = label;
        k.text = text;
        k.kind = kind;
        keys.push_back(k);
    };
    // QWERTY in English, AZERTY in French (same 10 + 10 + 10 + 8 keys)
    const bool fr = lang() == Lang::Fr;
    const char* rows[] = {"1234567890", fr ? "azertyuiop" : "qwertyuiop", fr ? "qsdfghjklm" : "asdfghjkl'"};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; rows[r][c]; ++c) add(r, c, 1, std::string(1, rows[r][c]), std::string(1, rows[r][c]), Key::Char);
    const char* last = fr ? "wxcvbn'-" : "zxcvbnm-";
    for (int c = 0; last[c]; ++c) add(3, c, 1, std::string(1, last[c]), std::string(1, last[c]), Key::Char);
    add(3, 8, 2, "\xE2\x8C\xAB", "", Key::Back);  // ⌫
    add(4, 0, 2, L("Clear", "Effacer"), "", Key::Clear);
    add(4, 2, 5, L("space", "espace"), " ", Key::Space);
    add(4, 7, 3, L("Search", "Chercher"), "", Key::Enter);
    return keys;
}

}  // namespace wr
