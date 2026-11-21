#pragma once

#include <M5Unified.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "net/art.h"
#include "core.h"
#include "storage/settings.h"
#include "upnp.h"

struct UiState {
    wr::PlayerStatus player;  // titre/artiste/album déjà complétés par getMetaInfo
    wr::TrackMeta meta;
    bool online = false;
    String message;    // notification temporaire (erreur, info), affichée en haut
    int battery = -1;  // % ou -1 si inconnu / pas de batterie
    int pending = -1;  // bouton de lecture dont la commande attend la confirmation du WiiM (anneau tournant)
};

// Affichage sur l'écran couleur (Tab5 : 1280×720 en paysage, 720×1280 en portrait ; les autres tailles
// sont mises à l'échelle par sc(), mise en page : wr::computeLayout).
//
// Chaque image est entièrement dessinée hors écran (canevas en PSRAM), puis
// affichée d'un coup : pas de scintillement, et un appui s'affiche en quelques
// millisecondes. La couleur d'accent (lueur de fond, bouton lecture, barres,
// preset actif) est tirée de la pochette en cours.
//
// Deux vues :
//  - commandes : grande pochette ; à côté (dessous en portrait), textes, progression, lecture, volume ;
//    en bas, les presets en tuiles (image + nom), par pages : une rangée en paysage, deux en portrait,
//    de 2 à 6 colonnes selon la largeur (6 par page sur le Tab5)
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
    // Dictée de la recherche : bouton micro (si une clé OpenAI est configurée)
    void setVoiceEnabled(bool on) { voiceOn_ = on; }
    void setAlbumSearch(bool on) { albumsOn_ = on; }  // choix « Albums » (recherche Qobuz directe seulement)
    bool hitMic(int x, int y) const;
    bool hitHome(int x, int y) const;      // Accueil : barre de navigation, ou bouton de la vue ambiance
    bool hitSettings(int x, int y) const;  // Réglages : barre de navigation
    // Bouton ♥ de la vue commandes : 0 caché, 1 vide (ajouter), 2 plein (retirer), 3 en cours
    void setFavorite(int state) { favState_ = state; }
    // Avance de la progression seule (zone ~3 % de l'écran) dans la vue affichée ;
    // false si elle ne s'y prête pas (autre écran) : faire un rendu complet
    bool updateProgress(const UiState& st);
    // Rangée des boutons de lecture seule (animation d'attente) ; false si la vue commandes n'est pas affichée
    bool updateTransport(const UiState& st, int pressed = -1);
    // Pilule du volume seule (−, haut-parleur, curseur, valeur, +) : appui, glissé, son coupé ; false hors vue commandes
    bool updateVolume(const UiState& st, int pressed = -1);
    // Une tuile seule (enfoncée ou relâchée) ; false si elle n'est pas sur la page affichée ou hors vue commandes
    bool updateTile(int index, bool pressed);
    // Mesure de la réactivité : nombre d'envois à l'écran (complets ou partiels) et millis() du dernier
    uint32_t presentCount() const { return presents_; }
    uint32_t lastPresentMs() const { return lastPresentMs_; }
    // Visualiseur de la vue ambiance : réserve sa place si activé
    void setVisualizer(bool enabled) { visEnabled_ = enabled; }
    // Nouveaux niveaux (0..1) : seule la zone du visualiseur est redessinée
    void showLevels(const std::vector<float>& levels, const std::vector<float>& peaks);

    // Position visée dans la vue commandes (le point d'un appui sur l'élément Volume / Progress de targets())
    bool hitVolume(int x, int y, int& volume) const;       // curseur de volume : niveau visé
    int volumeAt(int x) const;                             // glissé du curseur : niveau visé (borné, y ignoré)
    bool hitProgress(int x, int y, uint32_t& posMs) const;  // barre de progression (zone suivie par updateProgress)
    // targets() est celle de la vue commandes (dernier écran complet dessiné : render())
    bool controlsShown() const { return view_ == View::Controls; }

    // Menu de l'appui long : configuration, mise à jour, fermer
    enum MenuChoice { MenuNone = -1, MenuConfig = 0, MenuUpdate, MenuTouch, MenuLog, MenuSleep, MenuClose };
    void renderMenu(const char* version);  // éléments MenuChoice (arg = MenuChoice)

    // Menu d'une tuile (appui long) : son image et son nom, Épingler / Désépingler, Retirer des favoris (removable),
    // Fermer. Éléments TileChoice (arg = TileChoice)
    enum TileChoice { TilePin = 0, TileRemove, TileClose };
    void renderTileMenu(int index, bool pinned, bool removable);

    // Bibliothèque : toutes les tuiles en grille. shown : indices des tuiles à montrer (filtre appliqué, dans l'ordre
    // de la bande) ; filters : filtres proposés (wr::TileKind, -1 = toutes) ; filter : le choisi ; pressed : tuile
    // enfoncée (-1). Éléments LibFilter, LibTile (arg = indice global), LibPage. Rend la page montrée (bornée).
    int renderLibrary(const std::vector<int>& shown, const std::vector<int>& filters, int filter, int page, int pressed = -1);

    // Choix du WiiM trouvé sur le réseau : (nom, adresse) ; liste vide = aucun trouvé. Éléments PickDevice (arg =
    // indice), PickRetry, PickSetup
    void renderPicker(const std::vector<std::pair<std::string, std::string>>& devices);

    // Test de l'écran tactile : un rond sous le doigt et ses coordonnées (x < 0 : aucun appui)
    void renderTouchTest(int x, int y, int count);

    // Journal : derniers évènements (appuis, commandes, erreurs) pour le diagnostic
    void renderLog(const std::vector<String>& lines, const char* version);

    // Recherche Qobuz (par le WiiM) : clavier, puis liste des morceaux trouvés
    bool hitSearch(int x, int y) const;  // bouton Recherche de la barre
    // source 0 Qobuz (morceaux), 1 Deezer, 2 albums Qobuz ; sources : Deezer proposé (choix Qobuz | Albums | Deezer)
    // mic : 0 micro au repos, 1 écoute (level 0..1), 2 transcription en cours (si la voix est configurée)
    void renderSearch(const std::string& query, const String& status, bool busy, int pressedKey = -1, int source = 0,
                      bool sources = false, int mic = 0, float level = 0);
    // Mises à jour partielles (rapides) de l'écran de recherche : une touche enfoncée / relâchée, le champ
    void pressKey(int index, bool down);
    void updateSearchField(const std::string& query, const String& status, bool busy, int source, bool sources,
                           int mic = 0, float level = 0);
    const std::vector<wr::Key>& keys() const { return keys_; }
    // hearts : vide, ou un cœur par ligne (0 vide, 1 plein) : albums que l'on peut mettre en tuile.
    // covers : vide, ou une image par ligne (nullptr : pas encore là) ; une ligne dont `art` est vide garde son numéro
    void renderResults(const std::string& query, const std::vector<wr::QueueTrack>& items, int page,
                       const String& status, int pressed = -1, const std::vector<int>& hearts = {},
                       const std::vector<const ArtImage*>& covers = {});
    // Pochette arrivée après l'affichage de la liste : seule sa case est redessinée (si la ligne est visible)
    void updateResultCover(int index, const ArtImage* img);
    int resultsFirst() const { return resultsFirst_; }
    int resultsPerPage() const { return resultsPerPage_; }

    // Mise à jour par le réseau : adresse à ouvrir, code à saisir, progression (-1 : en attente)
    void showUpdate(const String& url, const String& code, const char* version, int percent, const String& status);
    bool hitUpdateCancel(int x, int y) const { return targets_.hit(wr::Act::UpdateCancel, x, y) != nullptr; }

    // Écrans plein page
    void showInfo(const char* title, const String& body, const char* footer = nullptr);
    void showPortal(const String& apName, const String& apPass, const String& url, const String& reason);

    // Éléments touchables de l'écran affiché (wr::TargetList), inscrits pendant son dessin complet ; les mises à
    // jour partielles n'y touchent pas. hit() rend le premier qui contient le point (l'ordre d'inscription est la
    // priorité) : main.cpp résout chaque appui ainsi, puis l'active. hitHome/hitSettings/hitSearch en sont des
    // recherches ; hitMic() garde son calcul (voir ui_search.cpp), hitVolume/hitProgress lisent la position.
    const wr::TargetList& targets() const { return targets_; }

    // Focus de la télécommande (D-pad) : anneau autour de l'élément focalisé, dessiné en dernier par chaque écran
    // complet (present) et par les mises à jour partielles dont la zone le touche (presentRect). `source` dit s'il
    // est montré et sur quel élément (wr::focusedTarget : l'élément retenu, sinon celui par défaut) ; nullptr ou
    // false : aucun anneau (le Tab5, sans touches, n'en montre jamais). Jamais sur la vue ambiance.
    using FocusSource = bool (*)(wr::FocusSpot& spot);
    void setFocusSource(FocusSource source) { focusSource_ = source; }

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
    void tileAt(const wr::Rect& r, int index, bool pressed);  // tuile `index` dans le rectangle r (bande, Bibliothèque)
    void tileArt(int index, int x, int y, uint32_t bg);  // image de la tuile (ou monogramme, grand cœur), côté thumbSide()
    void pageDots();
    void toast(const String& msg);
    void drawBars();
    void micIcon(int cx, int cy, int s, uint32_t color, uint32_t bg);
    void homeIcon(int cx, int cy, int s, uint32_t color, uint32_t bg);
    void gearIcon(int cx, int cy, int r, uint32_t color, uint32_t bg);
    void navRail(int active);  // 0 Accueil, 1 Réglages, 2 Recherche, 3 Bibliothèque
    void libraryIcon(int cx, int cy, int r, uint32_t color);
    void searchIcon(int cx, int cy, int r, uint32_t color, uint32_t bg);
    void searchField(const std::string& query, const String& status, bool busy, int source, bool sources, int mic,
                     float level);
    void drawKey(int index, bool down);
    void emptyStrip();         // bande des presets sans preset
    void heartIcon(int cx, int cy, int s, uint32_t color);
    void favButton();
    void controlsTargets();  // inscrit les éléments touchables de la vue commandes (fin de render())
    void focusRing();        // anneau du focus sur canvas_ (ui_widgets.cpp), si la source le demande
    FocusSource focusSource_ = nullptr;
    uint32_t presents_ = 0, lastPresentMs_ = 0;

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
    // Dictée de la recherche (micro dans le champ) ; onglet Albums (accès Qobuz direct)
    bool voiceOn_ = false;
    bool albumsOn_ = false;
    int chipRight_ = 0;  // info() : bord droit des puces (avant la batterie), 0 = largeur du bloc
    // Visualiseur
    bool visEnabled_ = false;
    enum class View { Other, Controls, Ambient };
    View view_ = View::Other;  // dernier écran affiché (pour les mises à jour partielles)
    // Début de chaque écran complet : view_ = view, liste des éléments touchables vidée (barre de navigation
    // comprise : navRail() l'inscrit)
    void beginScreen(View view);
    wr::Rect ambProg_;          // ligne de progression de la vue ambiance
    wr::Rect homeRect_;         // bouton Accueil de la vue ambiance
    int favState_ = 0;
    std::vector<wr::Key> keys_;           // clavier affiché
    std::vector<wr::Rect> coverRects_;    // pochettes des lignes de résultats (rectangle vide : numéro à la place)
    std::map<std::string, std::unique_ptr<M5Canvas>> coverThumbs_;  // pochettes décodées, par adresse
    void drawResultCover(const wr::Rect& r, const ArtImage* img);
    int resultsFirst_ = 0, resultsPerPage_ = 1;
    wr::Rect srcRects_[3];  // choix Qobuz | Deezer | Albums du clavier (indice = source)
    wr::Rect micRect_;      // micro (dictée) du champ de recherche
    wr::Rect searchArea_;   // champ + choix de la source + ligne d'état (redessinés seuls)
    bool keysBusy_ = false;
    wr::TargetList targets_;  // vidée par beginScreen(), remplie par le rendu complet de chaque écran
    wr::Rect visRect_;
    std::vector<float> visLevels_, visPeaks_;
};
