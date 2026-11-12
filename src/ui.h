#pragma once

#include <M5Unified.h>

#include <memory>
#include <vector>

#include "art.h"
#include "core.h"
#include "settings.h"

struct UiState {
    wr::PlayerStatus player;  // titre/artiste/album déjà complétés par getMetaInfo
    wr::TrackMeta meta;
    bool online = false;
    String message;    // notification temporaire (erreur, info), affichée en haut
    int battery = -1;  // % ou -1 si inconnu / pas de batterie
};

// Affichage sur l'écran couleur du Tab5 (1280×720).
//
// Chaque image est entièrement dessinée hors écran (canevas en PSRAM), puis
// affichée d'un coup : pas de scintillement, et un appui s'affiche en quelques
// millisecondes. La couleur d'accent (lueur de fond, bouton lecture, barres,
// preset actif) est tirée de la pochette en cours.
//
// Deux vues :
//  - commandes : grande pochette ; à côté, textes, progression, lecture, volume ;
//    en bas, les presets en tuiles (image + nom), par pages de 6
//  - ambiance (après un moment sans appui) : grande pochette, grands textes,
//    visualiseur ; un appui revient aux commandes
class Ui {
public:
    void begin(const Settings& s);

    // Liste des presets (revient à la première page)
    void setPresets(const std::vector<wr::Preset>& presets);
    // Image du preset i (nullptr : pas d'image). L'image doit rester valide.
    void setPresetArt(int index, const ArtImage* img);
    // Preset en cours de lecture (-1 : aucun), marqué d'un contour et d'une icône
    void setActivePreset(int index) { active_ = index; }
    // Nouvelle pochette (nullptr : illustration par défaut) ; recalcule l'accent.
    // L'image doit rester valide (elle est redécodée si la mise en page change).
    void setArt(const ArtImage* img);

    // Pages de presets
    int page() const { return page_; }
    int perPage() const { return layout_.perPage; }
    int pageCount() const;
    void setPage(int p);

    // Vue commandes ; pressedPreset (indice global) / pressedTransport : bouton enfoncé.
    void render(const UiState& st, int pressedPreset = -1, int pressedTransport = -1);
    // Vue ambiance
    void renderAmbient(const UiState& st);
    // Commande vocale : bouton micro (si une clé est configurée) et écran d'écoute
    enum class VoicePhase { Listening, Thinking };
    void setVoiceEnabled(bool on) { voiceOn_ = on; }
    void renderVoice(VoicePhase phase, float level);  // level : 0..1
    bool hitMic(int x, int y) const;
    bool hitVoiceCancel(int x, int y) const;
    // Visualiseur de la vue ambiance : réserve sa place si activé
    void setVisualizer(bool enabled) { visEnabled_ = enabled; }
    // Nouveaux niveaux (0..1) : seule la zone du visualiseur est redessinée
    void showLevels(const std::vector<float>& levels, const std::vector<float>& peaks);

    int hitPreset(int x, int y) const;  // indice global du preset, ou -1
    int hitTransport(int x, int y) const;
    bool hitHeader(int x, int y) const;                    // pochette ou textes
    bool hitVolume(int x, int y, int& volume) const;       // curseur de volume : niveau visé
    bool hitMute(int x, int y) const;                      // icône du haut-parleur
    bool hitProgress(int x, int y, uint32_t& posMs) const;  // barre de progression
    bool hitStrip(int x, int y) const;                     // bande des presets (glisser)
    int hitDots(int x, int y) const;                       // -1 / +1 : page précédente / suivante

    // Menu de l'appui long : configuration, mise à jour, fermer
    enum MenuChoice { MenuNone = -1, MenuConfig = 0, MenuUpdate, MenuTouch, MenuClose };
    void renderMenu(const char* version);
    MenuChoice hitMenu(int x, int y) const;

    // Choix du WiiM trouvé sur le réseau : (nom, adresse) ; liste vide = aucun trouvé
    enum { PickNone = -1, PickRetry = -2, PickConfig = -3 };
    void renderPicker(const std::vector<std::pair<std::string, std::string>>& devices);
    int hitPicker(int x, int y) const;  // indice du WiiM, ou PickRetry / PickConfig / PickNone

    // Test de l'écran tactile : un rond sous le doigt et ses coordonnées (x < 0 : aucun appui)
    void renderTouchTest(int x, int y, int count);
    bool hitTouchTestClose(int x, int y) const { return touchClose_.contains(x, y); }

    // Mise à jour par le réseau : adresse à ouvrir, code à saisir, progression (-1 : en attente)
    void showUpdate(const String& url, const String& code, const char* version, int percent, const String& status);
    bool hitUpdateCancel(int x, int y) const { return cancelRect_.contains(x, y); }

    // Écrans plein page
    void showInfo(const char* title, const String& body, const char* footer = nullptr);
    void showPortal(const String& apName, const String& apPass, const String& url, const String& reason);

private:
    int sc(int v) const { return v * ref_ / 540; }
    uint32_t bgAt(int y) const;
    void buildBackground();
    void background();
    void present();
    void relayout();
    int thumbSide() const;

    bool decode(M5Canvas& dst, const ArtImage* img, uint32_t bg);
    void roundedPush(M5Canvas& src, int x, int y, int radius, bool bgIsGradient, uint32_t bg);
    void placeholderArt(M5Canvas& dst);
    void info(const UiState& st);
    void chip(int& x, int y, const std::string& text, uint32_t dot, uint32_t textColor);
    void progressRow(const UiState& st, const wr::Rect& r, wr::Rect* hit);
    void transport(const UiState& st, int pressed);
    void volumePill(const UiState& st, int pressed);
    void tile(int slot, int index, bool pressed);
    void pageDots();
    void toast(const String& msg);
    void drawBars();
    void micIcon(int cx, int cy, int s, uint32_t color, uint32_t bg);

    M5Canvas canvas_;  // image complète, en PSRAM
    M5Canvas bg_;      // fond (lueur de l'accent), recalculé quand l'accent change
    uint32_t bgAccent_ = 0xFFFFFFFFu;
    M5Canvas art_;     // pochette à la taille de la vue commandes
    M5Canvas artBig_;  // pochette à la taille de la vue ambiance (décodée à la demande)
    const ArtImage* artSrc_ = nullptr;
    bool artBigValid_ = false;
    uint32_t accent_ = 0;
    int ref_ = 540;
    wr::Layout layout_;
    std::vector<wr::Preset> presets_;
    std::vector<const ArtImage*> presetArtSrc_;
    std::vector<std::unique_ptr<M5Canvas>> thumbs_;  // vignettes décodées (nullptr : aucune)
    int page_ = 0;
    int active_ = -1;
    // Zone tactile de la progression, mise à jour à chaque rendu de la vue commandes
    wr::Rect progHit_;
    uint32_t progDuration_ = 0;
    // Commande vocale
    bool voiceOn_ = false;
    wr::Rect cancelRect_;
    wr::Rect menuRects_[4];
    std::vector<wr::Rect> pickRects_;
    wr::Rect touchClose_;
    wr::Rect pickRetry_, pickConfig_;
    // Visualiseur
    bool visEnabled_ = false;
    bool ambientShown_ = false;
    wr::Rect visRect_;
    std::vector<float> visLevels_, visPeaks_;
};
