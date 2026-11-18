// Aperçu PC : M5.Display devient un canevas M5GFX en mémoire (1280×720 par défaut, PREVIEW_SIZE sinon).
#pragma once

#include <M5GFX.h>

#include "Arduino.h"

// Pas de « using namespace lgfx » : son millis() (horloge SDL) rendrait les captures dépendantes du temps.
// millis() vient de Arduino.h (valeur fixe, définie dans preview.cpp), comme dans platform/pc/stubs.
using lgfx::textdatum_t;

class PreviewDisplay : public M5Canvas {
public:
    void setRotation(int) {}  // le canevas est créé directement dans la bonne orientation
    void setAutoDisplay(bool) {}
    void waitDisplay() {}
    void display() { ++refreshes; }
    int refreshes = 0;
};

struct PreviewM5 {
    PreviewDisplay Display;
};
extern PreviewM5 M5;
