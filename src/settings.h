#pragma once

#include <Arduino.h>

#include <vector>

#include "core.h"

// Réglages persistants (mémoire NVS de l'ESP32, conservés lors des mises à jour du firmware)
struct Settings {
    String ssid;
    String pass;
    String host;         // IP du WiiM, éventuellement avec ":port"
    bool https = true;
    String fingerprint;  // empreinte SHA-256 du certificat du WiiM, vide = à relever
    String presetsText;  // une ligne par preset : "1=Radio Swiss Jazz" ; vide = presets du WiiM
    int columns = 3;
    int rotation = 1;    // 0 portrait, 1 paysage, 2 portrait inversé, 3 paysage inversé
    int volStep = 5;
    int beep = 2;        // bips : 0 désactivés, 1 discrets, 2 normaux, 3 forts
    bool visualizer = true;  // vue ambiance : spectre du son de la pièce (micro, traité sur place)
    bool motionWake = true;  // soulever / déplacer le Tab5 rallume l'écran

    std::vector<wr::Preset> presets;  // déduit de presetsText (vide : repris du WiiM)

    bool autoPresets() const;

    bool configured() const { return ssid.length() > 0 && host.length() > 0; }
};

// Texte de presets vide (ou seulement des espaces / commentaires) : presets repris du WiiM
bool presetsTextIsAuto(const String& text);

// Charge les réglages ; renvoie false si la liste de presets est invalide.
bool loadSettings(Settings& s);
void saveSettings(const Settings& s);
void saveFingerprint(const String& hex);
