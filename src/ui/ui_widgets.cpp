// Petits éléments partagés par plusieurs écrans : icônes (cœur, micro, maison, engrenage, loupe) et barre de
// navigation, avec ses zones tactiles.
#include "ui/ui_internal.h"

// Cœur : deux disques et une pointe
void Ui::heartIcon(int cx, int cy, int s, uint32_t c) {
    auto& C = canvas_;
    const int r = s / 2 + 1;
    C.fillCircle(cx - r + 1, cy - s / 4, r, c);
    C.fillCircle(cx + r - 1, cy - s / 4, r, c);
    C.fillTriangle(cx - 2 * r + 1, cy - s / 8, cx + 2 * r - 1, cy - s / 8, cx, cy + s, c);
}

// Micro : capsule, arceau et pied (s : demi-hauteur de l'icône)
void Ui::micIcon(int cx, int cy, int s, uint32_t c, uint32_t bg) {
    auto& C = canvas_;
    const int cw = s * 2 / 3, ch = s * 5 / 4, t = std::max(2, s / 6);
    C.fillRoundRect(cx - cw / 2, cy - s, cw, ch, cw / 2, c);
    // Arceau : demi-anneau sous la capsule
    C.fillArc(cx, cy - s + ch - cw / 2, cw / 2 + t * 2, cw / 2 + t, 0, 180, c);
    C.fillRect(cx - t / 2, cy + s / 2 + t, t, s / 3, c);
    C.fillRoundRect(cx - cw / 2, cy + s / 2 + t + s / 3 - 1, cw, t, t / 2, c);
    (void)bg;
}

// Bouton de la barre : toute la largeur de la barre compte, et un peu autour
static wr::Rect navHit(const wr::Rect& rail, const wr::Rect& b, int pad) {
    if (rail.h > rail.w) return wr::Rect{rail.x, b.y - pad, rail.w, b.h + 2 * pad};
    return wr::Rect{b.x - pad, rail.y, b.w + 2 * pad, rail.h};
}

// Barre de navigation : Accueil en haut, Réglages en bas ; l'écran affiché est en couleur
void Ui::navRail(int active) {
    auto& C = canvas_;
    const wr::Rect& r = layout_.rail;
    C.fillRect(r.x, r.y, r.w, r.h, 0x0A0B0Eu);
    if (r.h > r.w) C.drawFastVLine(r.right() - 1, r.y, r.h, kCard);
    else C.drawFastHLine(r.x, r.bottom() - 1, r.w, kCard);
    const wr::Rect* items[] = {&layout_.navHome, &layout_.navSettings, &layout_.navSearch};
    for (int i = 0; i < 3; ++i) {
        const wr::Rect& b = *items[i];
        const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
        const bool on = i == active;
        const uint32_t bg = on ? accent_ : kCard, fg = on ? kBg : kText2;
        C.fillCircle(cx, cy, b.w / 2, bg);
        if (i == 0) homeIcon(cx, cy, b.w / 4, fg, bg);
        else if (i == 1) gearIcon(cx, cy, b.w / 4, fg, bg);
        else searchIcon(cx, cy, b.w / 4, fg, bg);
    }
    // Zones tactiles : toute la largeur (la hauteur en portrait) de la barre, et sc(16) autour du bouton. Elles ne
    // touchent aucun autre élément (la barre est à part : wr::computeLayout), leur place dans la liste est donc libre.
    targets_.add(wr::Act::Home, 0, layout_.navHome, navHit(layout_.rail, layout_.navHome, sc(16)));
    targets_.add(wr::Act::Search, 0, layout_.navSearch, navHit(layout_.rail, layout_.navSearch, sc(16)));
    targets_.add(wr::Act::Settings, 0, layout_.navSettings, navHit(layout_.rail, layout_.navSettings, sc(16)));
}

// Loupe : anneau et manche
void Ui::searchIcon(int cx, int cy, int r, uint32_t c, uint32_t bg) {
    auto& C = canvas_;
    const int rr = r * 3 / 4, ox = cx - r / 4, oy = cy - r / 4, t = std::max(3, r / 4);
    C.fillCircle(ox, oy, rr, c);
    C.fillCircle(ox, oy, rr - t, bg);
    for (int k = -t / 2; k <= t / 2; ++k)
        C.drawLine(ox + rr * 7 / 10 + k, oy + rr * 7 / 10 - k, cx + r, cy + r, c);
    C.fillCircle(cx + r, cy + r, t / 2, c);
}

bool Ui::hitSearch(int x, int y) const { return targets_.hit(wr::Act::Search, x, y) != nullptr; }

// Engrenage : dents en couronne, disque, trou central
void Ui::gearIcon(int cx, int cy, int r, uint32_t c, uint32_t bg) {
    auto& C = canvas_;
    const int teeth = 8, tr = std::max(2, r / 4);
    for (int k = 0; k < teeth; ++k) {
        float a = k * 2 * 3.14159265f / teeth;
        C.fillCircle(cx + (int)std::lround(std::cos(a) * r), cy + (int)std::lround(std::sin(a) * r), tr, c);
    }
    C.fillCircle(cx, cy, r * 85 / 100, c);
    C.fillCircle(cx, cy, r * 38 / 100, bg);
}

// Maison : toit, corps, porte
void Ui::homeIcon(int cx, int cy, int s, uint32_t c, uint32_t bg) {
    auto& C = canvas_;
    C.fillTriangle(cx - s - s / 4, cy - s / 8, cx + s + s / 4, cy - s / 8, cx, cy - s - s / 4, c);
    C.fillRect(cx - s * 3 / 4, cy - s / 8, s * 3 / 2, s, c);
    C.fillRect(cx - s / 5, cy + s / 3, s * 2 / 5, s * 2 / 3 - s / 8, bg);
}

// Accueil : bouton de la barre, ou celui de la vue ambiance ; Réglages, Recherche : barre. Inscrits seulement
// pendant que leur écran est affiché (beginScreen() vide la liste).
bool Ui::hitHome(int x, int y) const { return targets_.hit(wr::Act::Home, x, y) != nullptr; }

bool Ui::hitSettings(int x, int y) const { return targets_.hit(wr::Act::Settings, x, y) != nullptr; }
