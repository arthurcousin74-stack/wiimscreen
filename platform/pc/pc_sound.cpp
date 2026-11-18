// PC (simulator and desktop): no sound yet (taps and beeps are silent).
#include "device/sound.h"

namespace sound {
void begin(int) {}
void tap() {}
void error() {}
void confirm() {}
void suspend() {}
void resume() {}
}  // namespace sound
