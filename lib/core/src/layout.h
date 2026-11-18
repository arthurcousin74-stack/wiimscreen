// Screen layout without Arduino: rectangles, the main view, the on-screen keyboard.
#pragma once

#include <string>
#include <vector>

namespace wr {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool intersects(const Rect& o) const {
        return w > 0 && h > 0 && o.w > 0 && o.h > 0 && x < o.right() && o.x < right() && y < o.bottom() &&
               o.y < bottom();
    }
};

enum Transport { kVolDown = 0, kPrev, kPlayPause, kNext, kVolUp, kTransportCount };

// Mise en page de la vue commandes.
// Paysage : grande pochette à gauche ; à droite, de haut en bas, les textes, la
// progression, les commandes de lecture et le volume ; les presets en tuiles
// (pochette + nom) sur une bande en bas, par pages.
// Portrait : la même pile, pochette en haut, presets sur deux rangées.
// Barre de navigation (Accueil en haut, Réglages en bas) : à gauche en paysage, en haut en portrait.
struct Layout {
    Rect art;       // pochette, carrée (appui long sur elle ou les textes : configuration)
    Rect info;      // puces (source, qualité), titre, artiste, album
    Rect progress;  // temps écoulé, barre, durée
    Rect transport[kTransportCount];  // cercles : vol−, précédent, lecture, suivant, vol+
    Rect fav;       // bouton ♥ (favori), à l'autre bout de la rangée
    Rect volume;    // pilule de volume (contient vol−, haut-parleur, curseur, valeur, vol+)
    Rect mute;      // icône du haut-parleur (coupe le son)
    Rect volBar;    // curseur de volume
    Rect strip;     // bande des presets (glisser pour changer de page)
    std::vector<Rect> presets;  // emplacements d'une page
    int perPage = 0;
    Rect dots;      // indicateur de pages
    Rect rail;         // barre de navigation
    Rect navHome;      // Accueil (écran principal)
    Rect navSettings;  // Réglages
    Rect navSearch;    // Recherche (au milieu)
    Rect content;      // reste de l'écran, à côté de la barre
};

// Les dimensions suivent la taille de l'écran (référence : 540 px sur le petit côté).
Layout computeLayout(int width, int height, int presetCount);

// Clavier à l'écran (recherche) : AZERTY en français, QWERTY en anglais (wr::lang()), 5 rangées sur une grille
// de 10 colonnes.
struct Key {
    enum Kind { Char, Space, Back, Clear, Enter };
    Rect r;
    std::string label;  // texte affiché
    std::string text;   // texte ajouté (Char)
    Kind kind = Char;
};
std::vector<Key> keyboardLayout(const Rect& area);

}  // namespace wr
