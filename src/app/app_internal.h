// État partagé de l'application : src/main.cpp et src/app/app_*.cpp seulement (même idée que ui_internal.h et
// wiim_worker_internal.h ; ne pas l'inclure ailleurs). Constantes, écran affiché, état regroupé par sujet, et
// fonctions d'un de ces fichiers appelées par un autre.
#pragma once

#include <Arduino.h>

#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "net/art.h"
#include "core.h"
#include "decisions.h"
#include "dsp.h"
#include "storage/settings.h"
#include "ui/ui.h"
#include "net/wiim_worker.h"

// Espace de noms : ces objets sont visibles de plusieurs fichiers (et le simulateur a déjà son propre g_state)
namespace app {

constexpr const char* kQobuzFavsId = "qobuz-favoris";    // tuile « Favoris Qobuz » : liste des morceaux favoris
constexpr const char* kDeezerFavsId = "deezer-favoris";  // tuile « Favoris Deezer » (profil Deezer configuré)

// Onglet du clavier de recherche (indice du bouton dans Ui::renderSearch)
enum : int { kTabQobuz = 0, kTabDeezer = 1, kTabAlbums = 2 };

// Visualiseur : blocs de 512 échantillons à 16 kHz (32 ms), deux tampons en alternance
constexpr int kMicRate = 16000, kMicBlock = 512, kBands = 24;
constexpr size_t kVoiceMax = kMicRate * 8;  // dictée : 8 s au plus

// ------------------------------------------------------------------ état de l'application
//
// Tout l'état de la boucle, regroupé par sujet. Les objets sont définis dans main.cpp.

extern Settings g_settings;
extern Ui g_ui;
extern WiimWorker g_worker;
extern UiState g_state;  // état affiché : relevé du WiiM, message, batterie

// Images : téléchargeur, pochette en cours, images des tuiles et des lignes de résultats
struct ImageCache {
    ArtFetcher fetcher;
    ArtImage art;                                  // pochette affichée
    std::string wanted;                            // adresse de la pochette attendue ("" = aucune)
    std::map<std::string, ArtImage> presets;       // images des presets, par adresse
    std::map<std::string, ArtImage> results;       // pochettes de la liste de résultats (jusqu'à la suivante)
    std::set<std::string> coverRequested;          // pochettes de résultats demandées au téléchargement
};
extern ImageCache g_img;

// Tuiles : presets du WiiM, favoris enregistrés, albums favoris des comptes ; file qui joue (♥)
struct TileData {
    std::vector<wr::Preset> list;                  // presets affichés
    std::vector<wr::Preset> wiimPresets;           // presets du WiiM (ou saisis), sans les favoris
    bool favOk = false;                            // mémoire des favoris disponible
    std::vector<wr::Favorite> favs;
    std::vector<wr::QobuzAlbum> deezerAlbums;      // albums favoris du compte Deezer
    std::vector<wr::QobuzAlbum> qobuzAlbums;       // albums favoris du compte Qobuz (lus par l'accès du WiiM)
    bool fromWiim = false;                         // dernière liste de presets lue sur le WiiM
    uint32_t lastFetch = 0;
    std::string queueKey;                          // clé de la file qui joue (relevé du WiiM), vide si inconnue
};
extern TileData g_tiles;

// Preset en cours : marqué jusqu'à ce que la source change ou que la lecture s'arrête
struct ActiveTile {
    int index = -1;
    uint32_t since = 0;
    int mode = -1;
};
extern ActiveTile g_active;

// Travaux confiés à la tâche WiiM, en attente de leur évènement (abandonnés après 120 s par la boucle)
struct WorkerJobs {
    bool search = false;                           // recherche (ou liste de favoris) en cours
    bool favorite = false;                         // enregistrement d'un favori en cours
    bool albumFavorite = false;                    // album en cours d'ajout aux favoris
    uint32_t since = 0;                            // début de la recherche / de l'enregistrement en cours
};
extern WorkerJobs g_jobs;

// Recherche Qobuz (par le WiiM) : clavier, puis résultats
struct SearchScreen {
    std::string resultsTitle;                      // en-tête de la liste : texte cherché, ou « Favoris Qobuz »
    SearchKind resultsKind = SearchKind::QobuzTracks;  // liste affichée (recherche ou favoris)
    std::string query;
    String status;
    std::vector<wr::QueueTrack> results;
    int page = 0;
    int source = kTabQobuz;                        // onglet du clavier
    std::vector<wr::QobuzAlbum> albums;            // résultats de la recherche d'albums (SearchKind::QobuzAlbums)
    bool deezerOk = false;                         // une file Deezer a été vue sur le WiiM
    // Touche enfoncée : dessinée tout de suite, relâchée ~90 ms plus tard (mises à jour partielles de l'écran)
    int keyDown = -1;
    uint32_t keyDownAt = 0;
};
extern SearchScreen g_search;

// Écran affiché. Un seul à la fois : chaque passage d'un écran à l'autre se fait depuis l'écran quitté.
enum class Screen {
    Controls,   // commandes (ou vue ambiance, voir OpenScreens::ambient)
    Menu,       // menu Réglages (barre, ou appui long sur l'en-tête)
    TouchTest,  // test tactile (menu Réglages)
    Log,        // Journal (menu Réglages)
    Search,     // recherche : clavier
    Results,    // recherche : liste de résultats (ou favoris Qobuz / Deezer)
};
constexpr int kScreenCount = 6;  // nombre de valeurs de Screen (focus retenu par écran)

// Écran affiché, vue ambiance, et compteurs des écrans Journal et test tactile
struct OpenScreens {
    Screen screen = Screen::Controls;
    // Vue ambiance : façon de dessiner les commandes (micro du visualiseur ouvert). Drapeau à part, pas une valeur
    // de Screen : elle commence seulement sur les commandes, mais une tuile « Favoris » relâchée dans le tour
    // de boucle où elle commence ouvre la liste par-dessus (Results), et Accueil y ramène ensuite ; exitAmbient()
    // ferme alors le micro. Une seule valeur perdrait la vue ambiance et laisserait le micro ouvert.
    bool ambient = false;
    uint32_t logFrame = 0;
    int touchCount = 0;
    uint32_t touchFrame = 0;
};
extern OpenScreens g_view;

// Focus de la télécommande (touches : clavier, télécommande, manette ; rien sur le Tab5). Retenu par écran comme
// (act, arg), pas comme un indice : il survit aux rendus (la liste Ui::targets() est refaite à chaque image) ;
// élément disparu (autre page de tuiles) : celui par défaut (wr::focusedTarget). L'anneau n'est montré qu'après
// une touche et caché au premier appui sur l'écran (« focus-visible ») : le Tab5 reste tel quel. Pas d'anneau sur
// le clavier ni les résultats (étape suivante), ni sur la vue ambiance.
struct FocusState {
    wr::FocusSpot spot[kScreenCount];  // indice : (int)Screen
    wr::FocusSpot picker;              // choix du WiiM (chooseWiim, pendant setup())
    bool picking = false;              // chooseWiim() en cours : l'anneau suit `picker`
    bool visible = false;
};
extern FocusState g_focus;

// Transcription en tâche de fond : l'appel à OpenAI (jusqu'à ~30 s si le réseau est lent) ne doit pas figer
// l'écran (règle « jamais d'attente réseau dans la boucle »). La boucle lit le résultat quand il est prêt.
struct SttJob {
    std::atomic<bool> busy{false}, done{false};  // atomiques : `done` publie text/err/ok écrits avant lui
    bool ok = false;
    std::string text;
    String err;
};

// Dictée de la recherche : enregistrement, niveau affiché, volume baissé, transcription
struct VoiceInput {
    bool listening = false;                        // micro en cours d'écoute
    bool dictation = false;                        // écoute pour la recherche (arrêt par un appui, pas par la fin de phrase)
    int16_t* buf = nullptr;
    size_t len = 0;
    float level = 0;
    uint32_t frame = 0;
    int duckedFrom = -1;                           // volume d'avant l'écoute, à rétablir
    SttJob stt;
};
extern VoiceInput g_voice;

// Micro (visualiseur de la vue ambiance et dictée) : deux tampons en alternance
struct MicInput {
    wr::Visualizer vis{kBands};
    bool on = false;                               // micro ouvert pour le visualiseur
    int16_t buf[2][kMicBlock];
    int cur = 0;
    bool pending = false;
    uint32_t lastVisFrame = 0;
};
extern MicInput g_mic;

// Bande des presets : appui = tuile enfoncée, relâcher = lancer, glisser = changer de page
struct StripTouch {
    bool touch = false;
    int preset = -1;
    int startX = 0;
    bool dragged = false;
};
extern StripTouch g_strip;

// Bouton montré enfoncé un court instant après un appui
struct PressFlash {
    int preset = -1, transport = -1;
    uint32_t until = 0;
};
extern PressFlash g_flash;

// Réveil par mouvement : accéléromètre lu à 50 Hz
struct MotionWake {
    wr::MotionDetector detector;
    uint32_t lastImu = 0;
};
extern MotionWake g_motion;

// Dessin : image complète à refaire (show()), dernière image, progression à redessiner
struct Redraw {
    bool dirty = false;
    uint32_t lastTick = 0;                         // dernière image (complète, progression ou anneau)
    bool progressDue = false;                      // saut de position (avance, retour) : ligne de progression redessinée sans attendre
};
extern Redraw g_draw;

// Écran et veille : dernier appui, dernière lecture vue, rétroéclairage
struct IdleClock {
    uint32_t lastTouch = 0;
    uint32_t lastPlayingAt = 0;                    // dernier tour de boucle où le WiiM (joignable) jouait : veille, écran éteint
    int brightness = -1;
};
extern IdleClock g_idle;

// Relevé affiché : instant du relevé, fin du message, tic de la batterie
struct StatusClock {
    uint32_t at = 0;                               // instant du dernier relevé (pour faire avancer la progression)
    uint32_t lastTick = 0;
    uint32_t messageUntil = 0;
};
extern StatusClock g_status;

// Appui long sur l'en-tête (pochette, titre) : menu Réglages
struct HeaderPress {
    bool pressed = false;
    uint32_t since = 0;
};
extern HeaderPress g_header;

// Commande en attente de confirmation : anneau tournant sur le bouton (sinon l'appui semble ignoré
// pendant que le WiiM réagit). Levée quand le relevé montre le changement, sur erreur, ou après 10 s.
using Await = wr::Awaited;  // ce que l'on attend (lib/core/src/decisions.h)
struct PendingCommand {
    int button = -1;                               // bouton concerné (wr::kPrev, kNext, kPlayPause)
    Await await = Await::None;                     // ce que l'on attend du WiiM
    uint32_t since = 0, seq = 0;
    std::string title;                             // morceau affiché au moment de l'appui
    bool playing = false;
    uint32_t pos = 0;                              // position au moment de l'appui (⏮ peut reprendre le même morceau)
    // Await::Music : la commande est arrivée au WiiM (appui, ou évènement FavPlayed) ; « chargement » vu depuis
    uint32_t readyAt = 0;
    bool ready = false, sawLoading = false;
};
extern PendingCommand g_pending;

// ------------------------------------------------------------------ fonctions partagées

// main.cpp
void show();  // écran à redessiner : renderNow() le dessine une fois par tour de boucle
void setMessage(const String& m, uint32_t durationMs = 8000);

// app_tiles.cpp : presets, tuiles des favoris, images (pochette, tuiles, lignes de résultats)
void refreshTiles();
std::string currentFavorite();
int favoriteState();
void onPresets(bool ok, const std::vector<wr::Preset>& fromWiim);
void requestPresets();
void checkImages();
void retryImages();
const ArtImage* knownImage(const std::string& url);

// app_search.cpp : recherche (clavier, liste de résultats) et listes « Favoris Qobuz / Deezer »
void renderSearch(int pressedKey = -1);
void runSearchQuery();
void openFavoriteList(bool deezer);
void openSearch();
void updateSearchField();
void releaseKey(bool force);
void onSearchKey(int k);
void renderResultsList(int pressed = -1, const String& status = String());
void toggleAlbumFavorite(int i);
void onSearchDone(const WiimEvent& ev);

// app_voice.cpp : dictée de la recherche (micro, transcription OpenAI en tâche de fond)
void stopListening();
void startVoice();
void finishDictation(bool send);
void serviceTranscription();
void serviceVoice();

}  // namespace app
