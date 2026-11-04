#pragma once

#include <M5Unified.h>

#include <vector>

#include "art.h"
#include "core.h"
#include "settings.h"

struct UiState {
    wr::PlayerStatus player;
    wr::TrackMeta meta;
    bool online = false;
    String message;    // message temporaire (erreur, info), prioritaire sur l'album
    int battery = -1;  // % ou -1 si inconnu
};

// Affichage e-ink.
//
// Le mode de rafraîchissement est choisi AVANT de dessiner : M5GFX convertit les
// pixels au moment du dessin (1 bit tramé en mode rapide, 16 niveaux de gris sinon).
//  - appui : bouton enfoncé en mode le plus rapide (~0,1 s)
//  - textes : mode "text" (~0,5 s, 16 gris), uniquement sur les zones modifiées
//  - pochette : mode "quality" (meilleurs dégradés), seulement quand elle change
//  - après 45 mises à jour partielles, dès 10 s sans appui : rafraîchissement
//    complet "quality" (~5 s) pour effacer les images fantômes
class Ui {
public:
    void begin(const Settings& s);

    void drawMain(const UiState& st, const ArtImage* art, bool quality);
    void drawHeader(const UiState& st);   // infos de lecture (sans la pochette)
    void drawArt(const ArtImage* art);    // pochette, ou illustration par défaut
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
    void beginDraw(epd_mode_t mode);
    void endDraw(bool partial = true);

    void info(const UiState& st);
    void art(const ArtImage* img);
    void placeholderArt(const wr::Rect& r);
    void button(const wr::Rect& r, const std::string& label, bool pressed);
    void transportIcon(int which, const wr::Rect& r, bool pressed, bool playing);

    wr::Layout layout_;
    std::vector<wr::Preset> presets_;
    int partialCount_ = 0;
};
