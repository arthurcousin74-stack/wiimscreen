#pragma once

#include <M5Unified.h>

#include <memory>
#include <vector>

#include "art.h"
#include "core.h"
#include "settings.h"
#include "upnp.h"

struct UiState {
    wr::PlayerStatus player;  // titre/artiste/album déjà complétés par getMetaInfo
    wr::TrackMeta meta;
    bool online = false;
    String message;    // notification temporaire (erreur, info), affichée en haut
    int battery = -1;  // % ou -1 si inconnu / pas de batterie
    int pending = -1;  // bouton de lecture dont la commande attend la confirmation du WiiM (anneau tournant)
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
    bool hitHome(int x, int y) const;      // Accueil : barre de navigation, ou bouton de la vue ambiance
    bool hitSettings(int x, int y) const;  // Réglages : barre de navigation
    // Bouton ♥ de la vue commandes : 0 caché, 1 vide (ajouter), 2 plein (retirer), 3 en cours
    void setFavorite(int state) { favState_ = state; }
    bool hitFav(int x, int y) const;
    bool hitVoiceCancel(int x, int y) const;
    // Avance de la progression seule (zone ~3 % de l'écran) dans la vue affichée ;
    // false si elle ne s'y prête pas (autre écran) : faire un rendu complet
    bool updateProgress(const UiState& st);
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
    enum MenuChoice { MenuNone = -1, MenuConfig = 0, MenuUpdate, MenuTouch, MenuLog, MenuClose };
    void renderMenu(const char* version);
    MenuChoice hitMenu(int x, int y) const;

    // Choix du WiiM trouvé sur le réseau : (nom, adresse) ; liste vide = aucun trouvé
    enum { PickNone = -1, PickRetry = -2, PickConfig = -3 };
    void renderPicker(const std::vector<std::pair<std::string, std::string>>& devices);
    int hitPicker(int x, int y) const;  // indice du WiiM, ou PickRetry / PickConfig / PickNone

    // Test de l'écran tactile : un rond sous le doigt et ses coordonnées (x < 0 : aucun appui)
    void renderTouchTest(int x, int y, int count);
    bool hitTouchTestClose(int x, int y) const { return touchClose_.contains(x, y); }

    // Journal : derniers évènements (appuis, commandes, erreurs) pour le diagnostic
    void renderLog(const std::vector<String>& lines, const char* version);
    bool hitLogClose(int x, int y) const { return logClose_.contains(x, y); }
    bool hitLogQueue(int x, int y) const { return logQueue_.contains(x, y); }
    bool hitLogTest(int x, int y) const { return logTest_.contains(x, y); }

    // Recherche Qobuz (par le WiiM) : clavier, puis liste des morceaux trouvés
    bool hitSearch(int x, int y) const;  // bouton Recherche de la barre
    // source 0 Qobuz, 1 Deezer ; sources : afficher le choix Qobuz | Deezer
    void renderSearch(const std::string& query, const String& status, bool busy, int pressedKey = -1, int source = 0,
                      bool sources = false);
    int hitSource(int x, int y) const;   // 0 Qobuz, 1 Deezer, -1 ailleurs
    int hitKey(int x, int y) const;      // indice dans keys(), ou -1
    const std::vector<wr::Key>& keys() const { return keys_; }
    void renderResults(const std::string& query, const std::vector<wr::QueueTrack>& items, int page,
                       const String& status, int pressed = -1);
    int resultsPerPage() const { return resultsPerPage_; }
    int hitResult(int x, int y) const;   // indice global du morceau, ou -1
    int hitResultsPage(int x, int y) const;  // -1 / +1 : page précédente / suivante, 0 sinon
    bool hitResultsEdit(int x, int y) const { return resultsEdit_.contains(x, y); }

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
    void presentRect(const wr::Rect& r);  // n'envoie à l'écran que cette zone
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
    void homeIcon(int cx, int cy, int s, uint32_t color, uint32_t bg);
    void gearIcon(int cx, int cy, int r, uint32_t color, uint32_t bg);
    void navRail(int active);  // 0 Accueil, 1 Réglages, 2 Recherche
    void searchIcon(int cx, int cy, int r, uint32_t color, uint32_t bg);
    void emptyStrip();         // bande des presets sans preset
    void heartIcon(int cx, int cy, int s, uint32_t color);

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
    wr::Rect menuRects_[5];
    wr::Rect logClose_;
    wr::Rect logQueue_;  // Journal : bouton « File du WiiM »
    wr::Rect logTest_;   // Journal : bouton « Essai recherche »
    std::vector<wr::Rect> pickRects_;
    wr::Rect touchClose_;
    wr::Rect pickRetry_, pickConfig_;
    // Visualiseur
    bool visEnabled_ = false;
    bool ambientShown_ = false;
    enum class View { Other, Controls, Ambient };
    View view_ = View::Other;  // dernier écran affiché (pour les mises à jour partielles)
    wr::Rect ambProg_;          // ligne de progression de la vue ambiance
    wr::Rect homeRect_;         // bouton Accueil de la vue ambiance
    bool railShown_ = false;    // barre de navigation à l'écran
    int favState_ = 0;
    std::vector<wr::Key> keys_;           // clavier affiché
    std::vector<wr::Rect> resultRects_;   // lignes de résultats affichées
    int resultsFirst_ = 0, resultsPerPage_ = 1;
    wr::Rect resultsPrev_, resultsNext_, resultsEdit_;
    wr::Rect srcRects_[2];  // choix Qobuz | Deezer du clavier
    wr::Rect visRect_;
    std::vector<float> visLevels_, visPeaks_;
};
