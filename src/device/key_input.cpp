// Tab5: touch screen only, no key input. Compiled for the Tab5 only (the PC builds list their sources in
// platformio.ini and take platform/pc/pc_platform_keys.cpp instead).
#include "device/key_input.h"

bool takeKey(wr::KeyEvent&) { return false; }
