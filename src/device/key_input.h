#pragma once

// Key input of the device (remote, keyboard, game controller): events built by wr::KeyTracker (lib/core/src/keys.h).
// Each target gives one implementation:
//   Tab5 (touch only)       src/device/key_input.cpp: never an event
//   PC (simulator, desktop) platform/pc/pc_platform_keys.cpp: SDL keyboard/controller and the --script key steps
// Read it from the main loop only.

#include "keys.h"

// Next key event, oldest first, without waiting. False when there is none.
bool takeKey(wr::KeyEvent& out);
