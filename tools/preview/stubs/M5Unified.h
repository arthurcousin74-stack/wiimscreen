// Aperçu PC : M5.Display devient un canevas M5GFX en mémoire (1280×720).
#pragma once

#include <M5GFX.h>

using namespace lgfx;

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
