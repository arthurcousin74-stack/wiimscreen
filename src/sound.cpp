#include "sound.h"

#include <M5Unified.h>

namespace sound {

static bool g_on = false;

void begin(int level) {
    // Haut-parleur 1 W du Tab5 : bien plus sonore que le buzzer d'origine
    static const uint8_t kVolume[] = {0, 30, 70, 140};
    level = constrain(level, 0, 3);
    g_on = level > 0 && M5.Speaker.begin();
    if (g_on) M5.Speaker.setVolume(kVolume[level]);
}

void tap() {
    if (g_on) M5.Speaker.tone(2600, 10);
}

void error() {
    if (!g_on) return;
    M5.Speaker.tone(1100, 80, 0, true);
    M5.Speaker.tone(700, 160, 0, false);  // à la suite de la première note
}

void confirm() {
    if (!g_on) return;
    M5.Speaker.tone(1500, 60, 0, true);
    M5.Speaker.tone(2300, 100, 0, false);
}

}  // namespace sound
