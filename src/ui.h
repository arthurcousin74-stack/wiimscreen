#pragma once

#include <M5Unified.h>

#include <vector>

#include "core.h"
#include "settings.h"

struct UiState {
    wr::PlayerStatus player;
    bool online = false;
    String message;    // message temporaire (erreur, info), prioritaire sur l'artiste
    int battery = -1;  // % ou -1 si inconnu
};

// Affichage e-ink. Stratégie de rafraîchissement :
//  - appui : inversion du bouton en mode le plus rapide (~0,1 s)
//  - textes : mode "text" (~0,5 s), uniquement sur les zones modifiées
//  - après 45 mises à jour partielles, dès 10 s sans appui : rafraîchissement
//    complet "quality" (~5 s) pour effacer les images fantômes
class Ui {
public:
    void begin(const Settings& s);

    void drawMain(const UiState& st, bool quality);
    void drawHeader(const UiState& st);
    void showPreset(int index, bool pressed);
    void showTransport(int which, bool pressed, bool playing);

    int hitPreset(int x, int y) const;
    int hitTransport(int x, int y) const;
    bool hitHeader(int x, int y) const;

    bool needsDeghost() const { return partialCount_ >= 45; }

    // Écrans plein page
    void showInfo(const char* title, const String& body, const char* footer = nullptr);
    void showPortal(const String& apName, const String& apPass, const String& url, const String& reason);

private:
    void button(const wr::Rect& r, const std::string& label, bool inverted);
    void transportIcon(int which, const wr::Rect& r, bool inverted, bool playing);
    void push(bool fast);

    wr::Layout layout_;
    std::vector<wr::Preset> presets_;
    int partialCount_ = 0;
};
