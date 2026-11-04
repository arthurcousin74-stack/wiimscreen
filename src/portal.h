#pragma once

#include <Arduino.h>

#include "settings.h"
#include "ui.h"

// Portail de configuration : le Tab5 crée un Wi-Fi protégé par un mot de
// passe aléatoire affiché à l'écran (avec QR code), et sert un formulaire
// sur http://192.168.4.1. À l'enregistrement, il redémarre. Ne revient jamais.
[[noreturn]] void runSetupPortal(Settings& s, Ui& ui, const String& reason);
