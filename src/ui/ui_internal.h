#pragma once
// Interne aux fichiers ui*.cpp (dessin de l'écran) : couleurs, polices et petites aides partagées.
// Ne l'inclure nulle part ailleurs : l'interface publique est ui.h.

#include <algorithm>
#include <cmath>

#include "i18n.h"
#include "ui/ui.h"

// Couleurs en RGB888 (uint32_t) : M5GFX interpréterait un uint16_t comme du RGB565
static constexpr uint32_t kBg = 0x0E0F12u;      // fond
static constexpr uint32_t kCard = 0x1F2128u;    // boutons
static constexpr uint32_t kCardHi = 0x3A3D47u;  // bouton appuyé
static constexpr uint32_t kTrack = 0x34363Eu;   // rail des barres
static constexpr uint32_t kText = 0xFFFFFFu;
static constexpr uint32_t kText2 = 0xA6A9B2u;   // texte secondaire
static constexpr uint32_t kAccent = 0xE8A33Du;  // accent par défaut (ambre)
static constexpr uint32_t kRed = 0xE5484Du;

// Polices VLW : les tableaux ne sont définis qu'une fois (ui_core.cpp), ici seulement leurs adresses
#ifdef PC_SCALED_FONTS  // ordinateur : choisies selon la taille du canevas (ui_core.cpp)
extern const uint8_t* kTiny;
extern const uint8_t* kSmall;
extern const uint8_t* kMid;
extern const uint8_t* kBig;
#else
extern const uint8_t* const kTiny;  // noms de presets sur deux lignes
extern const uint8_t* const kSmall;
extern const uint8_t* const kMid;
extern const uint8_t* const kBig;
#endif

// Charge la police sur le canevas sauf si c'est déjà la sienne (jamais C.loadFont() directement).
// Un seul état pour tous les fichiers ui*.cpp : défini dans ui_core.cpp.
void useFont(M5Canvas& C, const uint8_t* f);

static inline uint32_t mix(uint32_t a, uint32_t b, float t) {  // t = 0 : a, t = 1 : b
    auto ch = [&](int sh) {
        float v = ((a >> sh) & 0xFF) * (1 - t) + ((b >> sh) & 0xFF) * t;
        return (uint32_t)std::lround(v) << sh;
    };
    return ch(16) | ch(8) | ch(0);
}
