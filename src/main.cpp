// Télécommande WiiM pour M5Stack Tab5.
//
// Démarrage :
//  - pas encore configuré → portail de configuration (Wi-Fi + QR code à l'écran)
//  - sinon → connexion Wi-Fi, recherche du WiiM si besoin, puis interface : pochette
//    et titre en cours, progression, presets du WiiM avec leurs images,
//    lecture/pause, piste précédente/suivante, volume
// Appui long (3 s) sur la pochette ou le titre → menu Réglages.
// Après 60 s sans appui pendant la lecture : vue ambiance (grande pochette) ; tout contact
// avec l'écran la quitte. Visualiseur (micro) en option, expérimental.
// Après 10 min sans appui, l'écran s'atténue ; il s'éteint après 20 min si rien ne joue.
// Soulever ou déplacer le Tab5 rallume l'écran (capteur de mouvement).
//
// Réseau : tout échange avec le WiiM passe par WiimWorker (tâche de fond). Cette
// boucle ne bloque jamais sur le WiiM : un appui s'affiche tout de suite (état
// optimiste), la commande part en arrière-plan, et seules les erreurs reviennent.

#include <M5Unified.h>
#include <WiFi.h>

#include <esp_heap_caps.h>

#include <atomic>
#include <cmath>
#include <map>
#include <set>

#include "appstate.h"
#include "art.h"
#include "core.h"
#include "decisions.h"
#include "i18n.h"
#include "discovery.h"
#include "dsp.h"
#include "favorites.h"
#include "log.h"
#include "ota.h"
#include "portal.h"
#include "settings.h"
#include "sound.h"
#include "ui.h"
#include "version.h"
#include "voice.h"
#include "voice_service.h"
#include "wiim_worker.h"

static Settings g_settings;
static Ui g_ui;
static WiimWorker g_worker;
static UiState g_state;
static ArtFetcher g_artFetcher;
static ArtImage g_art;           // pochette affichée
static std::string g_artWanted;  // adresse de la pochette attendue ("" = aucune)

static std::vector<wr::Preset> g_presets;              // presets affichés
static std::vector<wr::Preset> g_wiimPresets;          // presets du WiiM (ou saisis), sans les favoris
static bool g_favOk = false;                            // mémoire des favoris disponible
static bool g_favBusy = false;                          // enregistrement d'un favori en cours
static uint32_t g_busySince = 0;                         // début de la recherche / de l'enregistrement en cours
static std::vector<wr::Favorite> g_favs;
static std::vector<wr::QobuzAlbum> g_deezerAlbums;  // albums favoris du compte Deezer
static std::vector<wr::QobuzAlbum> g_qobuzAlbums;  // albums favoris du compte Qobuz (lus par l'accès du WiiM)
static const char* kQobuzFavsId = "qobuz-favoris";    // tuile « Favoris Qobuz » : liste des morceaux favoris
static const char* kDeezerFavsId = "deezer-favoris";  // tuile « Favoris Deezer » (profil Deezer configuré)
static std::map<std::string, ArtImage> g_presetImages;  // images des presets, par adresse
static std::map<std::string, ArtImage> g_resultImages;  // pochettes de la liste de résultats (jusqu'à la suivante)
static std::set<std::string> g_coverRequested;          // pochettes de résultats demandées au téléchargement
static bool g_presetsFromWiim = false;
static uint32_t g_lastPresetFetch = 0;

static uint32_t g_statusAt = 0;  // instant du dernier relevé (pour faire avancer la progression)
static uint32_t g_lastTick = 0;
static uint32_t g_lastStatusTick = 0;
static uint32_t g_messageUntil = 0;
static uint32_t g_headerPressStart = 0;
static bool g_headerPressed = false;
static uint32_t g_lastTouch = 0;
static uint32_t g_lastPlayingAt = 0;  // dernier tour de boucle où le WiiM (joignable) jouait : veille, écran éteint
static bool g_progressDue = false;     // saut de position (avance, retour) : ligne de progression redessinée sans attendre
static bool g_ambient = false;
static int g_brightness = -1;
static bool g_listening = false;  // micro en cours d'écoute
static bool g_dictation = false;  // écoute pour la recherche (arrêt par un appui, pas par la fin de phrase)
static bool g_menuOpen = false;   // menu Réglages affiché (appui long)
static bool g_touchTest = false;  // écran de test tactile (menu Réglages)
static bool g_logOpen = false;    // écran Journal (menu Réglages)
// Recherche Qobuz (par le WiiM) : clavier, puis résultats
static bool g_searchOpen = false, g_resultsOpen = false, g_searchBusy = false;
static std::string g_resultsTitle;  // en-tête de la liste : texte cherché, ou « Favoris Qobuz »
static SearchKind g_resultsKind = SearchKind::QobuzTracks;  // liste affichée (recherche ou favoris)
static std::string g_searchQuery;
static String g_searchStatus;
static std::vector<wr::QueueTrack> g_searchResults;
static int g_resultsPage = 0;
// Onglet du clavier de recherche (indice du bouton dans Ui::renderSearch)
enum : int { kTabQobuz = 0, kTabDeezer = 1, kTabAlbums = 2 };
static int g_searchSource = kTabQobuz;
static SearchKind kindOfTab(int tab) {
    return tab == kTabDeezer ? SearchKind::Deezer : tab == kTabAlbums ? SearchKind::QobuzAlbums : SearchKind::QobuzTracks;
}
// Le WiiM charge l'album du morceau choisi (Deezer, albums) : plus long qu'une piste de la file
static bool loadsAlbum(SearchKind k) {
    return k == SearchKind::Deezer || k == SearchKind::DeezerFavorites || k == SearchKind::QobuzAlbums;
}
static std::vector<wr::QobuzAlbum> g_albumResults;  // résultats de la recherche d'albums (SearchKind::QobuzAlbums)
static bool g_albumFavBusy = false;                  // album en cours d'ajout aux favoris
static bool g_deezerOk = false;      // une file Deezer a été vue sur le WiiM
static uint32_t g_logFrame = 0;
static int g_touchCount = 0;
static uint32_t g_touchFrame = 0;

// Bouton montré enfoncé un court instant après un appui
static int g_flashPreset = -1, g_flashTransport = -1;
static uint32_t g_flashUntil = 0;

// Bande des presets : appui = tuile enfoncée, relâcher = lancer, glisser = changer de page
static bool g_stripTouch = false;
static int g_stripPreset = -1;
static int g_stripStartX = 0;
static bool g_stripDragged = false;
static constexpr int kSwipePx = 50;

// Preset en cours : marqué jusqu'à ce que la source change ou que la lecture s'arrête
static int g_activePreset = -1;
static uint32_t g_activeSince = 0;
static int g_activeMode = -1;

// Visualiseur : blocs de 512 échantillons à 16 kHz (32 ms), deux tampons en alternance
static constexpr int kMicRate = 16000, kMicBlock = 512, kBands = 24;
static wr::Visualizer g_vis(kBands);
static bool g_micOn = false;
static int16_t g_micBuf[2][kMicBlock];
static int g_micCur = 0;
static bool g_micPending = false;
static uint32_t g_lastVisFrame = 0;

// Réveil par mouvement : accéléromètre lu à 50 Hz
static wr::MotionDetector g_motion;
static uint32_t g_lastImu = 0;

static constexpr uint32_t kLongPressMs = 3000;
static constexpr uint32_t kWifiTimeoutMs = 20000;
static constexpr uint32_t kPresetRefreshMs = 10UL * 60 * 1000;
static constexpr uint32_t kPresetRetryMs = 30UL * 1000;
static constexpr uint32_t kFlashMs = 150;

// Vue ambiance après 60 s ; rétroéclairage atténué après 10 min, éteint après 20 min si rien ne joue
static constexpr uint32_t kAmbientMs = 60UL * 1000;
static constexpr uint8_t kBrightOn = 200, kBrightDim = 50;
static constexpr uint32_t kDimMs = 10UL * 60 * 1000;
static constexpr uint32_t kOffMs = 20UL * 60 * 1000;

// Liaison SDIO entre l'ESP32-P4 et l'ESP32-C6 (Wi-Fi) du Tab5, d'après la documentation M5Stack
static constexpr int kSdioClk = 12, kSdioCmd = 13, kSdioD0 = 11, kSdioD1 = 10, kSdioD2 = 9,
                     kSdioD3 = 8, kSdioRst = 15;

// Batterie NP-F550 optionnelle : sans elle, pas d'indicateur
static int batteryLevel() {
    int b = M5.Power.getBatteryLevel();
    if (b <= 0 || b > 100) return -1;
    return (b + 2) / 5 * 5;  // arrondi à 5 % : évite de redessiner pour 1 %
}

static void setBrightness(int b) {
    if (b == g_brightness) return;
    g_brightness = b;
    M5.Display.setBrightness(b);
}

// État affiché : la position avance entre deux relevés pendant la lecture
// Commande en attente de confirmation : anneau tournant sur le bouton (sinon l'appui semble ignoré
// pendant que le WiiM réagit). Levée quand le relevé montre le changement, sur erreur, ou après 10 s.
using Await = wr::Awaited;  // ce que l'on attend (lib/core/src/decisions.h)
static int g_pendingT = -1;          // bouton concerné (wr::kPrev, kNext, kPlayPause)
static Await g_await = Await::None;  // ce que l'on attend du WiiM
static uint32_t g_pendingSince = 0, g_pendingSeq = 0;
static std::string g_pendingTitle;   // morceau affiché au moment de l'appui
static bool g_pendingPlaying = false;
static uint32_t g_pendingPos = 0;    // position au moment de l'appui (⏮ peut reprendre le même morceau)
// Await::Music : la commande est arrivée au WiiM (appui, ou évènement FavPlayed) ; « chargement » vu depuis
static uint32_t g_pendingReadyAt = 0;
static bool g_pendingReady = false, g_pendingSawLoading = false;

static void markPending(int button, Await what, bool viaEvent = false);
static void show();

// Lecture en cours, d'après le dernier relevé ; WiiM injoignable : son dernier état ne compte plus
static bool playingNow() { return g_state.online && g_state.player.playing; }

static UiState displayState() {
    UiState s = g_state;
    s.pending = g_pendingT;
    auto& p = s.player;
    if (s.online && p.playing && p.durationMs > 0)
        p.positionMs = std::min(p.durationMs, p.positionMs + (millis() - g_statusAt));
    return s;
}

static void markPending(int button, Await what, bool viaEvent) {
    g_pendingT = button;
    g_await = what;
    g_pendingSince = millis();
    // Commande passée par la file : confirmée par un relevé commencé après elle. Favori ou résultat de
    // recherche (autre chemin) : seulement après l'évènement de fin (FavPlayed).
    g_pendingSeq = viaEvent ? UINT32_MAX : g_worker.queued();
    g_pendingReady = !viaEvent;
    g_pendingReadyAt = g_pendingSince;
    g_pendingSawLoading = false;
    g_pendingTitle = g_state.player.title;
    g_pendingPlaying = g_state.player.playing;  // déjà basculé pour lecture/pause
    g_pendingPos = displayState().player.positionMs;
    show();
}

// Affichage différé : show() marque l'écran à redessiner, renderNow() le dessine une
// seule fois par tour de boucle, même si plusieurs changements sont arrivés.
static bool g_dirty = false;

static void show() { g_dirty = true; }

static bool modalOpen() { return g_listening || g_menuOpen || g_touchTest || g_logOpen || g_searchOpen || g_resultsOpen; }
static void openSearch();  // recherche Qobuz, plus bas
static void onSearchKey(int k);
static void onSearchDone(const WiimEvent& ev);
static void renderResultsList(int pressed = -1, const String& status = String());

static int favoriteState();  // plus bas, avec les favoris

// Court effet « bouton enfoncé » encore actif (différence signée : juste après 49 jours aussi)
static bool flashActive(uint32_t now) { return g_flashUntil && (int32_t)(g_flashUntil - now) > 0; }

static void renderNow() {
    if (!g_dirty || modalOpen()) return;  // écrans modaux : rien par-dessus (rendu à leur fermeture)
    g_dirty = false;
    const uint32_t now = millis();
    // Bouton enfoncé : tuile sous le doigt, ou court effet après un appui
    int pressedPreset = (g_stripTouch && !g_stripDragged) ? g_stripPreset : -1;
    int pressedTransport = -1;
    if (flashActive(now)) {
        if (pressedPreset < 0) pressedPreset = g_flashPreset;
        pressedTransport = g_flashTransport;
    }
    g_lastTick = now;
    g_progressDue = false;  // la progression fait partie de l'image complète
    g_ui.setFavorite(favoriteState());
    if (g_ambient) g_ui.renderAmbient(displayState());
    else g_ui.render(displayState(), pressedPreset, pressedTransport);
}

// Bouton enfoncé pendant un court instant, puis relâché par la boucle
static void flash(int preset, int transport) {
    g_flashPreset = preset;
    g_flashTransport = transport;
    g_flashUntil = millis() + kFlashMs;
    show();
}

static void setMessage(const String& m, uint32_t durationMs = 8000) {
    g_state.message = m;
    g_messageUntil = m.length() ? millis() + durationMs : 0;
}

static String errorText(WiimError e, const String& fingerprint = String()) {
    switch (e) {
        case WiimError::None: return "";
        case WiimError::Network:
            return wr::L("WiiM unreachable: check that it is on", "WiiM injoignable : v\xC3\xA9rifie qu'il est allum\xC3\xA9");
        case WiimError::PinMismatch:
            return String(wr::L("Unexpected WiiM certificate (", "Certificat du WiiM inattendu (")) +
                   fingerprint.substring(0, 12) +
                   wr::L("\xE2\x80\xA6). Long press here to reconfigure", "\xE2\x80\xA6). Appui long ici pour reconfigurer");
        case WiimError::Http: return wr::L("Unexpected response from the WiiM", "R\xC3\xA9ponse inattendue du WiiM");
        case WiimError::Refused: return wr::L("The WiiM refused the command", "Le WiiM a refus\xC3\xA9 la commande");
    }
    return "";
}

// --------------------------------------------------------------- vue ambiance

// Le bus audio passe du haut-parleur au micro (partagés sur le Tab5)
static void enterAmbient() {
    g_ambient = true;
    logEvent("Vue ambiance");
    if (g_settings.visualizer) {
        sound::suspend();
        g_micOn = M5.Mic.begin();
        logEvent("Micro (visualiseur) : %s", g_micOn ? "OK" : "\xC3\xA9" "chec");
        g_micPending = false;
        g_vis.reset();
        g_ui.showLevels(g_vis.levels(), g_vis.peaks());
    }
    show();
}

static void exitAmbient() {
    logEvent("Sortie de la vue ambiance");
    if (g_micOn) {
        M5.Mic.end();
        g_micOn = false;
        g_micPending = false;
    }
    if (g_settings.visualizer) sound::resume();
    g_ambient = false;
    show();
}

// Micro : traite le bloc terminé pendant que le suivant s'enregistre
static void serviceMic() {
    if (!g_micOn) return;
    if (!g_micPending) {
        g_micPending = M5.Mic.record(g_micBuf[g_micCur], kMicBlock, kMicRate);
        return;
    }
    if (M5.Mic.isRecording()) return;
    const int done = g_micCur;
    g_micCur ^= 1;
    g_micPending = M5.Mic.record(g_micBuf[g_micCur], kMicBlock, kMicRate);
    float db[kBands];
    wr::spectrumBands(g_micBuf[done], kMicBlock, kMicRate, kBands, db);
    g_vis.update(db);
    if (millis() - g_lastVisFrame >= 33) {  // ~30 images/s, zone du visualiseur seulement
        g_lastVisFrame = millis();
        g_ui.showLevels(g_vis.levels(), g_vis.peaks());
    }
}

// Mouvement détecté : écran rallumé et retour aux commandes
static void serviceMotion(uint32_t now) {
    if (!g_settings.motionWake || now - g_lastImu < 20) return;
    g_lastImu = now;
    float ax, ay, az;
    M5.Imu.update();
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
    if (!g_motion.feed(ax, ay, az)) return;
    g_lastTouch = now;
    if (g_brightness != kBrightOn) setBrightness(kBrightOn);
    if (g_ambient) exitAmbient();
}

// Mode configuration : par un redémarrage, jamais à chaud (le Wi-Fi du Tab5 passe par l'ESP32-C6 ;
// couper la connexion et chercher les réseaux pendant que les tâches réseau tournent figeait l'appareil).
static void restartIntoPortal(const String& reason) {
    logEvent("Configuration : red\xC3\xA9marrage en mode configuration");
    requestPortal(reason);
    g_ui.showInfo(wr::L("Setup", "Configuration"),
                  wr::L("The Tab5 is restarting in setup mode\xE2\x80\xA6", "Le Tab5 red\xC3\xA9marre en mode configuration\xE2\x80\xA6"));
    delay(600);
    ESP.restart();
}

// Mise en veille. Le toucher du Tab5 ne peut pas réveiller l'ESP32-P4 d'un sommeil profond (son interruption
// arrive sur GPIO23, et seules GPIO0-15 réveillent le P4) : on éteint donc le Tab5. Sur batterie, l'impulsion
// PWROFF coupe l'alimentation et le bouton d'alimentation le rallume ; sur USB, M5Unified passe en sommeil
// profond sans source de réveil (redémarrage par le bouton ou en débranchant).
static void stopListening();  // plus bas, avec la voix

static void goToSleep(const char* why) {
    logEvent("Mise en veille (%s)", why);
    if (g_listening) stopListening();
    sound::confirm();
    g_ui.showInfo(wr::L("Sleep", "Mise en veille"),
                  wr::L("The Tab5 is turning off. Press its power button to turn it back on.",
                        "Le Tab5 s'\xC3\xA9teint. Appuie sur son bouton d'alimentation pour le rallumer."));
    setBrightness(kBrightOn);
    delay(2500);
    setBrightness(0);
    M5.Power.powerOff();  // ne revient pas (coupure, ou sommeil profond)
}

// ------------------------------------------------------------------- presets

static bool samePresets(const std::vector<wr::Preset>& a, const std::vector<wr::Preset>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].label != b[i].label || a[i].picUrl != b[i].picUrl || a[i].favId != b[i].favId)
            return false;
    return true;
}

static void applyPresets(const std::vector<wr::Preset>& list) {
    // Doigt posé sur une tuile : son indice ne vaut plus rien dans la nouvelle liste (mauvaise tuile lancée, ou
    // lecture hors de la liste si elle a raccourci)
    if (g_stripTouch) {
        g_stripTouch = false;
        g_stripPreset = -1;
    }
    g_presets = list;
    // Images des tuiles disparues : libérées (PSRAM) quand le cache grandit
    if (g_presetImages.size() > 48)
        for (auto it = g_presetImages.begin(); it != g_presetImages.end();) {
            bool used = it->first == g_art.url;
            for (auto& p : g_presets) used |= p.picUrl == it->first;
            it = used ? std::next(it) : g_presetImages.erase(it);
        }
    g_ui.setPresets(g_presets);
    g_activePreset = -1;
    for (size_t i = 0; i < g_presets.size(); ++i) {
        const std::string& url = g_presets[i].picUrl;
        if (url.empty()) continue;
        auto it = g_presetImages.find(url);
        if (it != g_presetImages.end()) g_ui.setPresetArt((int)i, &it->second);
        else g_artFetcher.request(url);
    }
}

// Tuiles : presets du WiiM, puis favoris enregistrés sur la télécommande
static void refreshTiles() {
    g_favs = g_favOk ? fav::list() : std::vector<wr::Favorite>();
    std::vector<wr::Preset> list = g_wiimPresets;
    for (size_t i = 0; i < g_favs.size(); ++i) {
        wr::Preset p;
        p.slot = 100 + (int)i;
        p.label = g_favs[i].title;
        p.picUrl = g_favs[i].artUrl;
        p.favId = g_favs[i].id;
        list.push_back(p);
    }
    // Favoris Qobuz du compte (ceux de l'app) : le WiiM les charge avec sa propre connexion Qobuz
    if (g_favOk && fav::hasQobuzModel()) {
        wr::Preset p;
        p.slot = 99;
        p.label = wr::L("Qobuz favorites", "Favoris Qobuz");
        p.favId = kQobuzFavsId;
        list.push_back(p);
    }
    // Favoris Deezer (profil public indiqué dans la configuration) : il faut un modèle de file Deezer du WiiM
    if (g_favOk && g_settings.deezerUser.length() && fav::hasDeezerModel()) {
        wr::Preset p;
        p.slot = 98;
        p.label = wr::L("Deezer favorites", "Favoris Deezer");

        p.favId = kDeezerFavsId;
        list.push_back(p);
    }
    // Albums favoris Qobuz, avec leur pochette
    for (size_t i = 0; i < g_qobuzAlbums.size() && i < 36; ++i) {
        wr::Preset p;
        p.slot = 200 + (int)i;
        p.label = g_qobuzAlbums[i].title;
        p.picUrl = g_qobuzAlbums[i].image;
        p.favId = "qobuz-album:" + g_qobuzAlbums[i].id;
        list.push_back(p);
    }
    // Albums favoris Deezer
    for (size_t i = 0; i < g_deezerAlbums.size() && i < 36; ++i) {
        wr::Preset p;
        p.slot = 300 + (int)i;
        p.label = g_deezerAlbums[i].title;
        p.picUrl = g_deezerAlbums[i].image;
        p.favId = "deezer-album:" + g_deezerAlbums[i].id;
        list.push_back(p);
    }
    if (samePresets(list, g_presets)) return;
    applyPresets(list);
    show();
}

static std::string g_queueKey;  // clé de la file qui joue (relevé du WiiM), vide si inconnue

// Favori correspondant à ce qui joue : même file (clé), sinon même album et même artiste
static std::string currentFavorite() {
    if (!g_queueKey.empty())
        for (auto& f : g_favs)
            if (f.key == g_queueKey) return f.id;
    const UiState& st = g_state;
    const std::string album = st.player.album.empty() ? st.meta.album : st.player.album;
    if (album.empty()) return "";
    const std::string a = wr::normalizeText(album), artist = wr::normalizeText(st.player.artist);
    for (auto& f : g_favs)
        if (wr::normalizeText(f.title) == a && (f.artist.empty() || artist.empty() || wr::normalizeText(f.artist) == artist))
            return f.id;
    return "";
}

static int favoriteState() {
    if (!g_favOk || !g_state.online || g_state.player.title.empty()) return 0;
    if (g_favBusy) return 3;
    return currentFavorite().empty() ? 1 : 2;
}

// Réponse de getPresetInfo : presets du WiiM (noms et images), ou liste saisie
// complétée par les images du WiiM. Sans preset sur le WiiM : pas de tuiles, et
// l'écran explique comment en ajouter (plutôt que des presets factices).
static void onPresets(bool ok, const std::vector<wr::Preset>& fromWiim) {
    g_presetsFromWiim = ok;
    std::vector<wr::Preset> list;
    if (g_settings.autoPresets()) {
        if (!ok) return;  // on garde la liste actuelle ; nouvel essai plus tard
        list = fromWiim;
    } else {
        list = g_settings.presets;
        if (ok)
            for (auto& p : list)
                for (auto& w : fromWiim)
                    if (w.slot == p.slot) p.picUrl = w.picUrl;
    }
    g_wiimPresets = list;
    refreshTiles();
}

static void requestPresets() {
    g_lastPresetFetch = millis();
    g_worker.refreshPresets();
    g_worker.refreshQobuzFavorites();  // albums favoris Qobuz (tuiles), même rythme que les presets
}

// Images téléchargées en tâche de fond : pochette ou image de preset
static void checkImages() {
    ArtImage img;
    bool changed = false;
    while (g_artFetcher.poll(img)) {
        if (!img.ok()) {
            // Échec (réseau, format) : rien n'est gardé, l'image sera redemandée (retryImages, nouvelle page)
            g_coverRequested.erase(img.url);
            // Pochette du nouveau morceau introuvable : celle du précédent (et sa couleur d'accent) ne reste pas
            // à côté du nouveau titre ; image par défaut jusqu'au prochain essai
            if (!g_artWanted.empty() && img.url == g_artWanted && g_art.url != g_artWanted &&
                (g_art.ok() || !g_art.url.empty())) {
                g_ui.setArt(nullptr);
                g_art.reset();
                g_art.url.clear();
                changed = true;
            }
            continue;
        }
        if (!g_artWanted.empty() && img.url == g_artWanted) {
            // Même image pour une tuile (album en cours parmi les favoris) : une copie pour la tuile, sinon elle
            // ne recevait jamais son image (vu dans le simulateur)
            bool tile = false;
            for (auto& p : g_presets) tile |= p.picUrl == img.url;
            if (tile && img.ok() && !g_presetImages[img.url].ok()) {
                ArtImage& copy = g_presetImages[img.url];
                copy.url = img.url;
                copy.data = static_cast<uint8_t*>(heap_caps_malloc(img.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                if (copy.data) {
                    memcpy(copy.data, img.data, img.len);
                    copy.len = img.len;
                    for (size_t i = 0; i < g_presets.size(); ++i)
                        if (g_presets[i].picUrl == img.url) g_ui.setPresetArt((int)i, &copy);
                }
            }
            g_art = std::move(img);
            g_ui.setArt(&g_art);
            changed = true;
            continue;
        }
        bool used = false;
        for (auto& p : g_presets)
            if (p.picUrl == img.url) used = true;
        if (!used && g_coverRequested.count(img.url)) {  // pochette d'une ligne de résultats
            const std::string url = img.url;
            ArtImage& stored = g_resultImages[url];
            stored = std::move(img);
            if (g_resultsOpen && !g_searchOpen)
                for (size_t i = 0; i < g_searchResults.size(); ++i)
                    if (g_searchResults[i].art == url) g_ui.updateResultCover((int)i, &stored);
            continue;
        }
        if (!used) continue;  // piste ou liste déjà changée : image périmée
        std::string url = img.url;
        ArtImage& stored = g_presetImages[url];
        stored = std::move(img);
        for (size_t i = 0; i < g_presets.size(); ++i)
            if (g_presets[i].picUrl == url) g_ui.setPresetArt((int)i, &stored);
        if (g_resultsOpen && !g_searchOpen && g_coverRequested.count(url))  // même image dans la liste ouverte
            for (size_t i = 0; i < g_searchResults.size(); ++i)
                if (g_searchResults[i].art == url) g_ui.updateResultCover((int)i, &stored);
        changed = true;
    }
    if (changed) show();
}

// Images manquantes (échec, ou résultat perdu pendant un écran sans checkImages) : redemandées toutes les minutes
static void retryImages() {
    static uint32_t last = 0;
    if (millis() - last < 60000) return;
    last = millis();
    if (!g_artWanted.empty() && g_art.url != g_artWanted) g_artFetcher.request(g_artWanted, true);
    for (auto& p : g_presets) {
        if (p.picUrl.empty()) continue;
        auto it = g_presetImages.find(p.picUrl);
        if (it == g_presetImages.end() || !it->second.ok()) g_artFetcher.request(p.picUrl);
    }
}

// ------------------------------------------------------- état venant du WiiM

static void applyReport(const WiimReport& r) {
    UiState next = g_state;
    next.online = r.online;
    bool artCleared = false;
    const uint32_t shownPos = displayState().player.positionMs;  // position affichée juste avant ce relevé
    if (r.online) {
        wr::PlayerStatus st = r.player;
        if (r.metaOk) {
            next.meta = r.meta;
        } else if (st.title.empty()) {  // getMetaInfo sauté ce coup-ci : on garde la piste connue
            st.title = g_state.player.title;
            st.artist = g_state.player.artist;
            st.album = g_state.player.album;
        }
        next.player = st;
        g_statusAt = r.at;
        if (r.queueKey != g_queueKey) {
            g_queueKey = r.queueKey;
            show();  // le ♥ peut changer
        }

        if (next.meta.artUrl != g_artWanted) {
            g_artWanted = next.meta.artUrl;
            if (g_artWanted.empty()) {
                g_ui.setArt(nullptr);
                g_art.reset();
                artCleared = true;
            } else {
                g_artFetcher.request(g_artWanted, true);
            }
        }
    }

    // Preset actif : on retient la source quelques secondes après l'appui (le temps que
    // le WiiM bascule), puis on l'oublie si la source change ou si la lecture s'arrête. Rien n'est jugé pendant ce
    // délai : sur un WiiM à l'arrêt, le premier relevé (rien ne joue, pas de titre) effaçait la tuile choisie.
    if (g_activePreset >= 0 && next.online && !next.player.loading && wr::expired(millis(), g_activeSince, 6000)) {
        bool stopped = !next.player.playing && next.player.title.empty();
        if (g_activeMode < 0) g_activeMode = next.player.mode;
        else if (next.player.mode != g_activeMode) stopped = true;
        if (stopped) {
            g_activePreset = -1;
            g_ui.setActivePreset(-1);
        }
    }

    if (next.online) {
        bool due = millis() - g_lastPresetFetch > (g_presetsFromWiim ? kPresetRefreshMs : kPresetRetryMs);
        if (due || !g_state.online) requestPresets();
    }

    // La position avance à chaque relevé pendant la lecture : la ligne de progression (1 fois/s) la dessine, sans
    // image complète (~1,8 Mo). Un saut (avance, retour, morceau repris) est dessiné tout de suite ; à l'arrêt,
    // toute différence est un vrai changement.
    wr::PlayerStatus samePos = next.player;
    samePos.positionMs = g_state.player.positionMs;
    bool changed = artCleared || samePos != g_state.player || next.meta != g_state.meta ||
                   next.online != g_state.online;
    if (!changed && next.player.positionMs != g_state.player.positionMs) {
        if (!next.online || !next.player.playing) {
            changed = true;
        } else if (next.player.durationMs > 0) {
            const uint32_t newPos = std::min(next.player.durationMs, next.player.positionMs + (millis() - r.at));
            const uint32_t gap = newPos > shownPos ? newPos - shownPos : shownPos - newPos;
            if (gap > 2500) g_progressDue = true;
        }
    }
    g_state = next;
    if (changed) show();
}

// Copie de secours des réglages (LittleFS) à jour avec la NVS : la tâche WiiM y écrit l'empreinte, l'adresse et
// l'identité du WiiM ; sans cela un flash USB complet les perdait jusqu'au redémarrage suivant
static void backupSettings() {
    Settings s;
    loadSettings(s);
    if (g_favOk && s.configured()) fav::saveSettingsBackup(settingsToBackup(s));
}

static void serviceWiim() {
    WiimReport r;
    // Relevé commencé avant la dernière commande : il annulerait l'affichage optimiste
    if (g_worker.takeReport(r) && r.cmdSeq >= g_worker.queued()) {
        applyReport(r);
        // Confirmation de la commande en attente
        if (g_pendingT >= 0 && r.online && r.cmdSeq >= g_pendingSeq) {
            // Music : sans nouveau titre (tuile de ce qui joue déjà, radio sans titre), le WiiM qui joue après
            // avoir chargé, ou quelques secondes après la commande, confirme aussi (sinon l'anneau tournait 20 s)
            bool settled = false;
            if (g_await == Await::Music && g_pendingReady) {
                if (r.player.loading) g_pendingSawLoading = true;
                settled = wr::musicSettled(r.player, g_pendingSawLoading, millis() - g_pendingReadyAt);
            }
            if (settled || wr::pendingConfirmed(g_await, r.player, g_pendingTitle, g_pendingPos, g_pendingPlaying)) {
                g_pendingT = -1;
                show();
            }
        }
    }

    WiimEvent ev;
    while (g_worker.takeEvent(ev)) {
        switch (ev.kind) {
            case WiimEvent::CommandDone:
                if (ev.error != WiimError::None) g_pendingT = -1;
                if (ev.error != WiimError::None && ev.error != WiimError::PinMismatch) {
                    sound::error();
                    setMessage(errorText(ev.error));
                    if (ev.error == WiimError::Network) g_state.online = false;
                    g_worker.pollSoon();  // l'état réel remplace l'affichage optimiste
                    show();
                }
                break;
            case WiimEvent::Presets: onPresets(ev.ok, ev.presets); break;
            case WiimEvent::FavSaved:
                if (!ev.automatic) g_favBusy = g_albumFavBusy = false;
                if (ev.ok) {
                    sound::confirm();
                    refreshTiles();
                    if (g_resultsOpen && g_resultsKind == SearchKind::QobuzAlbums) renderResultsList();  // cœur plein
                    setMessage(String(wr::L("\xE2\x80\x9C", "\xC2\xAB ")) + ev.text +
                                   wr::L("\xE2\x80\x9D added to favorites", " \xC2\xBB ajout\xC3\xA9 aux favoris"),
                               5000);
                } else {
                    sound::error();
                    setMessage(String(wr::L("Favorite not saved: ", "Favori non enregistr\xC3\xA9 : ")) + ev.text);
                }
                show();
                break;
            case WiimEvent::SearchDone: onSearchDone(ev); break;
            case WiimEvent::DeezerFavorites:
                if (ev.ok) {
                    g_deezerAlbums = ev.albums;
                    refreshTiles();
                }
                break;
            case WiimEvent::QobuzFavorites:
                if (ev.ok) {
                    g_qobuzAlbums = ev.albums;
                    refreshTiles();
                }
                break;
            case WiimEvent::FavPlayed:
                if (g_pendingT >= 0) {
                    g_pendingSeq = g_worker.queued();  // les relevés suivants peuvent confirmer
                    g_pendingReady = true;             // la file est lancée : délai de musicSettled compté d'ici
                    g_pendingReadyAt = millis();
                }
                if (!ev.ok) {
                    g_pendingT = -1;
                    sound::error();
                    g_activePreset = -1;
                    g_ui.setActivePreset(-1);
                    setMessage(String(wr::L("Playback: ", "Lecture : ")) + ev.text);
                }
                g_worker.pollSoon();
                show();
                break;
            case WiimEvent::Pinned:
                backupSettings();  // empreinte écrite par la tâche WiiM : aussi dans la copie de secours
                setMessage(wr::L("WiiM certificate saved", "Certificat du WiiM enregistr\xC3\xA9"));
                show();
                break;
            case WiimEvent::PinMismatch:
                sound::error();
                setMessage(errorText(WiimError::PinMismatch, ev.text), 600000);
                show();
                break;
            case WiimEvent::Moved:
                g_settings.host = ev.text;
                backupSettings();
                setMessage(String(wr::L("WiiM found at its new address (", "WiiM retrouv\xC3\xA9 \xC3\xA0 sa nouvelle adresse (")) +
                           ev.text + ")");
                show();
                break;
            case WiimEvent::Identity:
                g_settings.wiimUuid = ev.uuid;
                g_settings.wiimName = ev.name;
                backupSettings();
                break;
        }
    }
}

// Batterie et fin des messages, une fois par seconde
static void tickStatus(uint32_t now) {
    if (now - g_lastStatusTick < 1000) return;
    g_lastStatusTick = now;
    bool changed = false;
    int b = batteryLevel();
    if (b != g_state.battery) g_state.battery = b, changed = true;
    if (g_messageUntil && (int32_t)(now - g_messageUntil) > 0) {
        g_state.message = "";
        g_messageUntil = 0;
        changed = true;
    }
    if (changed) show();
}

// --------------------------------------------------------- WiiM sur le réseau

static void useWiim(const WiimDevice& d) {
    const bool same = g_settings.wiimUuid.length() && d.uuid == g_settings.wiimUuid.c_str();
    g_settings.host = d.ip.c_str();
    g_settings.wiimUuid = d.uuid.c_str();
    g_settings.wiimName = d.name.c_str();
    saveWiim(g_settings.host, g_settings.wiimUuid, g_settings.wiimName);
    if (!same) {  // autre lecteur : son certificat sera enregistré au premier échange
        g_settings.fingerprint = "";
        saveFingerprint("");
    }
}

// Sans adresse enregistrée : recherche sur le Wi-Fi, choix sur l'écran s'il y en a plusieurs
static void chooseWiim() {
    for (;;) {
        g_ui.showInfo(wr::L("Looking for the WiiM\xE2\x80\xA6", "Recherche du WiiM\xE2\x80\xA6"),
                      wr::L("The Tab5 is looking for WiiM players on your Wi-Fi.",
                            "Le Tab5 cherche les lecteurs WiiM sur ton Wi-Fi."));
        std::vector<WiimDevice> found = discoverWiims();
        if (found.size() == 1) {
            useWiim(found[0]);
            return;
        }
        std::vector<std::pair<std::string, std::string>> list;
        for (auto& d : found) list.push_back({d.name, d.ip});
        g_ui.renderPicker(list);
        for (bool retry = false; !retry;) {
            M5.update();
            auto t = M5.Touch.getDetail();
            if (t.wasPressed()) {
                int c = g_ui.hitPicker(t.x, t.y);
                if (c >= 0 && c < (int)found.size()) {
                    sound::tap();
                    useWiim(found[c]);
                    return;
                }
                if (c == Ui::PickRetry) sound::tap(), retry = true;
                if (c == Ui::PickConfig) restartIntoPortal(wr::L("Enter the IP address of the WiiM.", "Saisis l'adresse IP du WiiM."));

            }
            delay(20);
        }
    }
}

// --------------------------------------------------------------------- appuis

// Avant le premier relevé, volume (0), son coupé et lecture sont des valeurs par défaut : un appui sur « + »
// enverrait vol 5, lecture/pause l'inverse de l'état réel. Ces appuis attendent l'état du WiiM.
static bool wiimStateKnown(const char* what) {
    if (g_state.player.ok) return true;
    logEvent("Appui %s ignor\xC3\xA9 : \xC3\xA9tat du WiiM pas encore re\xC3\xA7u", what);
    setMessage(wr::L("Waiting for the WiiM status\xE2\x80\xA6", "En attente de l'\xC3\xA9tat du WiiM\xE2\x80\xA6"), 3000);
    show();
    return false;
}

// Barres (volume, coupure du son, progression) : affichage immédiat, commande en fond
static bool handleBars(int x, int y) {
    int vol;
    uint32_t pos;
    auto& p = g_state.player;
    if (g_ui.hitVolume(x, y, vol)) {
        logEvent("Appui volume %d (%d,%d)", vol, x, y);
        sound::tap();
        p.volume = vol;
        p.muted = false;
        g_worker.command("setPlayerCmd:vol:" + String(vol));
        show();
        return true;
    }
    if (g_ui.hitMute(x, y)) {
        if (!wiimStateKnown("son coup\xC3\xA9/r\xC3\xA9tabli")) return true;
        logEvent("Appui son coup\xC3\xA9/r\xC3\xA9tabli (%d,%d)", x, y);
        sound::tap();
        p.muted = !p.muted;
        g_worker.command(String("setPlayerCmd:mute:") + (p.muted ? "1" : "0"));
        show();
        return true;
    }
    if (g_ui.hitProgress(x, y, pos)) {
        logEvent("Appui progression %lu s (%d,%d)", (unsigned long)(pos / 1000), x, y);
        sound::tap();
        p.positionMs = pos;
        g_statusAt = millis();
        g_worker.command("setPlayerCmd:seek:" + String(pos / 1000));  // secondes
        show();
        return true;
    }
    return false;
}

static void openFavoriteList(bool deezer);

static void playPreset(int p) {
    if (p < 0 || p >= (int)g_presets.size()) return;
    logEvent("Preset %d : %s", g_presets[p].slot, g_presets[p].label.c_str());
    sound::tap();
    if (g_presets[p].favId == kQobuzFavsId || g_presets[p].favId == kDeezerFavsId) {
        openFavoriteList(g_presets[p].favId == kDeezerFavsId);  // liste : on choisit, rien ne joue avant
        return;
    }
    g_activePreset = p;
    g_activeSince = millis();
    g_activeMode = -1;
    g_ui.setActivePreset(p);
    if (!g_presets[p].favId.empty()) {
        g_worker.playFavorite(g_presets[p].favId.c_str());  // file enregistrée
        markPending(wr::kPlayPause, Await::Music, true);
    } else {
        g_worker.command("MCUKeyShortClick:" + String(g_presets[p].slot));
        markPending(wr::kPlayPause, Await::Music);
    }
    flash(p, -1);
}

// ------------------------------------------------------------ dictée de la recherche
//
// Dictée de la recherche : bouton micro → écoute (la musique est baissée le temps d'écouter) → appui sur
// le bouton rouge → transcription OpenAI en tâche de fond → texte cherché.

static int16_t* g_voiceBuf = nullptr;
static size_t g_voiceLen = 0;
static constexpr size_t kVoiceMax = kMicRate * 8;  // 8 s au plus
static float g_voiceLevel = 0;
static uint32_t g_voiceFrame = 0;
static int g_duckedFrom = -1;  // volume d'avant l'écoute, à rétablir

static void stopListening() {
    M5.Mic.end();
    g_micPending = false;
    g_listening = false;
    sound::resume();
    if (g_duckedFrom >= 0) {
        g_worker.command("setPlayerCmd:vol:" + String(g_duckedFrom));
        g_duckedFrom = -1;
    }
}

static void renderSearch(int pressedKey = -1);

static void startVoice() {
    if (!g_settings.voiceEnabled()) return;
    g_dictation = true;
    if (!g_voiceBuf)
        g_voiceBuf = static_cast<int16_t*>(heap_caps_malloc(kVoiceMax * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_voiceBuf) {
        g_searchStatus = wr::L("Not enough memory to listen", "M\xC3\xA9moire insuffisante pour \xC3\xA9" "couter");
        renderSearch();
        return;
    }
    const auto& p = g_state.player;
    if (g_state.online && p.playing && !p.muted && p.volume > 15) {
        g_duckedFrom = p.volume;
        g_worker.command("setPlayerCmd:vol:12");
    }
    sound::confirm();
    delay(200);  // le bip finit avant que le bus audio passe au micro
    sound::suspend();
    if (!M5.Mic.begin()) {
        stopListening();
        g_dictation = false;
        g_searchStatus = wr::L("Microphone unavailable", "Micro indisponible");
        renderSearch();
        return;
    }
    g_listening = true;
    g_voiceLen = 0;
    g_voiceLevel = 0;
    g_micPending = false;
    g_micCur = 0;
    renderSearch();
}

static void runSearchQuery();

// Dictée de la recherche : la phrase entendue devient le texte cherché, puis la recherche part
// Transcription en tâche de fond : l'appel à OpenAI (jusqu'à ~30 s si le réseau est lent) ne doit pas figer
// l'écran (règle « jamais d'attente réseau dans la boucle »). La boucle lit le résultat quand il est prêt.
struct SttJob {
    std::atomic<bool> busy{false}, done{false};  // atomiques : `done` publie text/err/ok écrits avant lui
    bool ok = false;
    std::string text;
    String err;
};
static SttJob g_stt;

static void sttTask(void*) {
    // Indication pour la transcription, dans la langue de l'interface (choisie à l'exécution, pas à l'initialisation)
    const std::string hint = wr::L("Music search: artist names, track and album titles, in English or French.",
                                   "Recherche de musique : noms d'artistes, titres de morceaux et d'albums, en "
                                   "fran\xC3\xA7" "ais ou en anglais.");
    std::string text;
    String err;
    const bool ok = transcribe(g_voiceBuf, g_voiceLen, kMicRate, g_settings.openaiKey, g_settings.sttModel, hint, text, err);
    g_stt.text = text;
    g_stt.err = err;
    g_stt.ok = ok;
    g_stt.done = true;  // écrit en dernier : la boucle lit les champs après
    vTaskDelete(nullptr);
}

static void finishDictation(bool send) {
    stopListening();
    g_dictation = false;
    if (!send || g_voiceLen < kMicRate / 3) {
        g_searchStatus = send ? String(wr::L("I didn't hear anything", "Je n'ai rien entendu")) : String();
        renderSearch();
        return;
    }
    if (g_stt.busy) return;
    g_ui.updateSearchField(g_searchQuery, "", false, g_searchSource, g_deezerOk, 2);
    g_stt.busy = true;
    g_stt.done = false;
    // Pile en RAM interne (TLS) ; cœur 0, comme la tâche WiiM : le cœur 1 reste à l'écran et au toucher
    if (xTaskCreatePinnedToCore(sttTask, "stt", 16 * 1024, nullptr, 1, nullptr, 0) != pdPASS) {
        g_stt.busy = false;
        g_searchStatus = wr::L("Not enough memory for the transcription", "M\xC3\xA9moire insuffisante pour la transcription");
        renderSearch();
    }
}

// Résultat de la transcription (appelé par la boucle de l'écran de recherche)
static void serviceTranscription() {
    if (!g_stt.busy || !g_stt.done) return;
    g_stt.busy = false;
    g_stt.done = false;
    if (!g_searchOpen) return;  // l'écran a été quitté entre-temps
    if (!g_stt.ok) {
        logEvent("Transcription : %s", g_stt.err.c_str());
        sound::error();
        g_searchStatus = g_stt.err;
        renderSearch();
        return;
    }
    const std::string text = wr::cleanTranscript(g_stt.text, 60);  // guillemets et ponctuation ajoutés retirés
    logEvent("Dict\xC3\xA9" "e : \xC2\xAB %s \xC2\xBB", text.c_str());
    g_searchQuery = text;
    g_searchStatus = "";
    if (text.empty()) {
        g_searchStatus = wr::L("I didn't understand anything", "Je n'ai rien compris");
        renderSearch();
        return;
    }
    runSearchQuery();
}

// Micro pendant l'écoute : enregistre, suit le niveau, détecte la fin de la phrase
static void serviceVoice() {
    if (!g_listening) return;
    if (!g_micPending) {
        g_micPending = M5.Mic.record(g_micBuf[g_micCur], kMicBlock, kMicRate);
        return;
    }
    if (M5.Mic.isRecording()) return;
    const int done = g_micCur;
    g_micCur ^= 1;
    g_micPending = M5.Mic.record(g_micBuf[g_micCur], kMicBlock, kMicRate);
    const int16_t* b = g_micBuf[done];
    double sum = 0;
    for (int i = 0; i < kMicBlock; ++i) sum += double(b[i]) * b[i];
    const float rms = std::sqrt(sum / kMicBlock) / 32768.0f;
    const size_t n = std::min<size_t>(kMicBlock, kVoiceMax - g_voiceLen);
    memcpy(g_voiceBuf + g_voiceLen, b, n * 2);
    g_voiceLen += n;
    g_voiceLevel = std::max(std::min(1.0f, rms * 10), g_voiceLevel * 0.8f);
    // On écoute jusqu'à l'appui sur ■ (ou la limite du tampon)
    if (millis() - g_voiceFrame > 100) {
        g_voiceFrame = millis();
        g_ui.updateSearchField(g_searchQuery, "", false, g_searchSource, g_deezerOk, 1, g_voiceLevel);
    }
    if (g_voiceLen >= kVoiceMax) finishDictation(true);
}

// Bande des presets, suivi du doigt : enfoncer, glisser (page), relâcher (lancer)
static void serviceStrip(const m5::touch_detail_t& t) {
    if (!g_stripTouch) return;
    if (t.isPressed()) {
        if (!g_stripDragged && abs(t.x - g_stripStartX) > kSwipePx) {
            g_stripDragged = true;  // c'est un glissement : la tuile n'est plus enfoncée
            show();
        }
        return;
    }
    g_stripTouch = false;
    if (g_stripDragged) {
        int dx = t.x - g_stripStartX;
        int before = g_ui.page();
        g_ui.setPage(before + (dx < 0 ? 1 : -1));
        if (g_ui.page() != before) sound::tap();
        show();
    } else if (g_stripPreset >= 0) {
        playPreset(g_stripPreset);
    } else {
        show();
    }
}

static void handleTap(int x, int y) {
    if (int dir = g_ui.hitDots(x, y)) {
        g_ui.setPage(g_ui.page() + dir);
        sound::tap();
        show();
        return;
    }
    if (g_ui.hitStrip(x, y)) {
        g_stripTouch = true;
        g_stripDragged = false;
        g_stripStartX = x;
        g_stripPreset = g_ui.hitPreset(x, y);
        if (g_stripPreset >= 0) show();  // tuile montrée enfoncée tant que le doigt reste
        return;
    }
    if (g_ui.hitFav(x, y)) {  // ♥ : ajouter ce qui joue aux favoris, ou l'en retirer
        if (g_favBusy) return;
        sound::tap();
        const std::string id = currentFavorite();
        if (!id.empty()) {
            fav::remove(id);
            logEvent("Favori retir\xC3\xA9 (%s)", id.c_str());
            refreshTiles();
            setMessage(wr::L("Removed from favorites", "Retir\xC3\xA9 des favoris"), 4000);
        } else {
            const UiState& st = g_state;
            const std::string album = st.player.album.empty() ? st.meta.album : st.player.album;
            logEvent("Appui Favori : %s", album.c_str());
            g_favBusy = true;
            g_busySince = millis();
            g_worker.saveFavorite(st.player.title.c_str(), st.player.artist.c_str(), album.c_str(), st.meta.artUrl.c_str());
            setMessage(wr::L("Adding to favorites\xE2\x80\xA6", "Ajout aux favoris\xE2\x80\xA6"), 15000);

        }
        show();
        return;
    }
    if (g_ui.hitHome(x, y)) {  // Accueil (barre) : déjà ici, retour à la première page de presets
        logEvent("Appui Accueil (%d,%d)", x, y);
        sound::tap();
        g_ui.setPage(0);
        show();
        return;
    }
    if (g_ui.hitSearch(x, y)) {  // Recherche (barre)
        logEvent("Appui Recherche (%d,%d)", x, y);
        sound::tap();
        openSearch();
        return;
    }
    if (g_ui.hitSettings(x, y)) {  // Réglages (barre)
        logEvent("Appui R\xC3\xA9glages (%d,%d)", x, y);
        sound::tap();
        g_menuOpen = true;
        g_ui.renderMenu(FW_VERSION);
        return;
    }

    int t = g_ui.hitTransport(x, y);
    if (t < 0) {
        logEvent("Appui hors bouton (%d,%d)", x, y);
        return;
    }
    static const char* kNames[] = {"volume -", "pr\xC3\xA9" "c\xC3\xA9" "dent", "lecture/pause", "suivant", "volume +"};
    if ((t == wr::kVolDown || t == wr::kVolUp || t == wr::kPlayPause) && !wiimStateKnown(kNames[t])) return;
    logEvent("Appui %s (%d,%d)", kNames[t], x, y);
    sound::tap();
    auto& p = g_state.player;
    switch (t) {
        case wr::kVolDown:
        case wr::kVolUp: {
            int v = p.volume + (t == wr::kVolUp ? g_settings.volStep : -g_settings.volStep);
            v = constrain(v, 0, 100);
            p.volume = v;
            p.muted = false;
            g_worker.command("setPlayerCmd:vol:" + String(v));
            break;
        }
        case wr::kPrev:
        case wr::kNext:
            g_worker.command(t == wr::kPrev ? "setPlayerCmd:prev" : "setPlayerCmd:next");
            markPending(t, Await::Track);
            break;
        case wr::kPlayPause:
            // Ce que montre l'icône, explicitement : plus fiable qu'une bascule (onepause)
            // quand l'état affiché a un temps de retard sur le WiiM
            g_worker.command(p.playing ? "setPlayerCmd:pause" : "setPlayerCmd:resume");
            p.playing = !p.playing;
            g_statusAt = millis();
            markPending(t, Await::Playing);
            break;
    }
    flash(-1, t);
}

// Connexion Wi-Fi ; en cas d'échec, l'écran le dit et un appui ouvre la configuration.
static void connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_settings.ssid.c_str(), g_settings.pass.c_str());
    g_ui.showInfo(wr::L("Connecting\xE2\x80\xA6", "Connexion\xE2\x80\xA6"),
                  String(wr::L("Wi-Fi network: ", "R\xC3\xA9seau Wi-Fi : ")) + g_settings.ssid);

    uint32_t start = millis();
    bool shownError = false;
    while (WiFi.status() != WL_CONNECTED) {
        M5.update();
        if (!shownError && millis() - start > kWifiTimeoutMs) {
            g_ui.showInfo(wr::L("Wi-Fi unreachable", "Wi-Fi injoignable"),
                          String(wr::L("Cannot join \xE2\x80\x9C", "Impossible de rejoindre \xC2\xAB ")) + g_settings.ssid +
                              wr::L("\xE2\x80\x9D. Trying again. If the network or the password has changed, "
                                    "touch the screen to reconfigure.",
                                    " \xC2\xBB. Nouvel essai en cours. Si le r\xC3\xA9seau ou le mot de passe a chang\xC3\xA9, "
                                    "touche l'\xC3\xA9" "cran pour reconfigurer."),
                          wr::L("Touch the screen to open the setup", "Touche l'\xC3\xA9" "cran pour ouvrir la configuration"));
            shownError = true;
        }
        if (shownError && M5.Touch.getDetail().wasPressed()) {
            sound::tap();
            restartIntoPortal(String(wr::L("The Wi-Fi \xE2\x80\x9C", "Le Wi-Fi \xC2\xAB ")) + g_settings.ssid +
                              wr::L("\xE2\x80\x9D is unreachable.", " \xC2\xBB est injoignable."));
        }
        delay(20);
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    // Avant tout appel Wi-Fi (y compris le portail de configuration)
    WiFi.setPins(kSdioClk, kSdioCmd, kSdioD0, kSdioD1, kSdioD2, kSdioD3, kSdioRst);
    setBrightness(kBrightOn);

    // Réglages : NVS ; copie de secours dans LittleFS (favoris), qu'un flash USB complet n'efface pas
    g_favOk = fav::begin();
    bool presetsOk = loadSettings(g_settings);
    bool restored = false;
    std::string backup;
    if (!g_settings.configured() && g_favOk && fav::settingsBackup(backup) && settingsFromBackup(backup, g_settings)) {
        saveSettings(g_settings);  // NVS effacée (flash USB) : réglages remis depuis la copie
        presetsOk = loadSettings(g_settings);
        restored = true;
    }
    if (g_settings.configured() && g_favOk) fav::saveSettingsBackup(settingsToBackup(g_settings));
    wr::setLang(wr::langFromCode(g_settings.lang.c_str()));  // langue de l'interface (anglais par défaut)
    sound::begin(g_settings.beep);
    g_ui.begin(g_settings);
    g_ui.setVisualizer(g_settings.visualizer);
    g_ui.setVoiceEnabled(g_settings.voiceEnabled());
    g_ui.setAlbumSearch(g_settings.qobuzDirect);  // la recherche d'albums demande l'accès Qobuz direct (non officiel)

    String portalReason;
    if (takePortalRequest(portalReason)) runSetupPortal(g_settings, g_ui, portalReason);  // demandé avant le redémarrage
    if (!g_settings.configured()) runSetupPortal(g_settings, g_ui, wr::L("First setup.", "Premi\xC3\xA8re configuration."));
    if (!presetsOk)
        runSetupPortal(g_settings, g_ui,
                       wr::L("The saved preset list is invalid.", "La liste de presets enregistr\xC3\xA9" "e est invalide."));

    logEvent("D\xC3\xA9marrage, version %s", FW_VERSION);
    if (restored) logEvent("R\xC3\xA9glages remis depuis la copie de secours (Wi-Fi, WiiM, cl\xC3\xA9, options)");
    connectWifi();
    logEvent("Wi-Fi connect\xC3\xA9, adresse du Tab5 %s", WiFi.localIP().toString().c_str());
    if (!g_settings.host.length()) chooseWiim();  // recherche automatique du WiiM
    logEvent("WiiM : %s %s", g_settings.host.c_str(), g_settings.wiimName.c_str());
    g_artFetcher.begin();
    logEvent("Favoris : %s, %u", g_favOk ? "OK" : "m\xC3\xA9moire indisponible", (unsigned)fav::list().size());
    g_worker.begin(g_settings);
    if (!g_settings.autoPresets()) g_wiimPresets = g_settings.presets;  // liste saisie : tout de suite
    refreshTiles();
    requestPresets();
    g_state.online = true;  // jusqu'au premier relevé (une seconde environ)
    g_state.battery = batteryLevel();
    g_lastTouch = g_lastPlayingAt = millis();
    show();
}

// ------------------------------------------------------------ recherche Qobuz

static void renderSearch(int pressedKey) {
    g_ui.renderSearch(g_searchQuery, g_searchStatus, g_searchBusy, pressedKey, g_searchSource, g_deezerOk);
}

// Lance la recherche du texte saisi ou dicté
static void runSearchQuery() {
    std::string q = g_searchQuery;
    while (!q.empty() && q.back() == ' ') q.pop_back();
    if (q.empty() || g_searchBusy) {
        renderSearch();
        return;
    }
    logEvent("Recherche %s : %s", g_searchSource == kTabDeezer ? "Deezer" : g_searchSource == kTabAlbums ? "Albums Qobuz" : "Qobuz",
             q.c_str());
    g_searchBusy = true;
    g_busySince = millis();
    g_resultsTitle = q;
    g_resultsKind = kindOfTab(g_searchSource);
    g_searchStatus = g_searchSource == kTabDeezer ? String(wr::L("Searching Deezer\xE2\x80\xA6", "Recherche sur Deezer\xE2\x80\xA6"))
                     : g_searchSource == kTabAlbums ? String(wr::L("Searching Qobuz albums\xE2\x80\xA6",
                                                          "Recherche d'albums sur Qobuz\xE2\x80\xA6"))
                                           : String(wr::L("Searching Qobuz through your WiiM\xE2\x80\xA6",
                                                          "Recherche sur Qobuz par ton WiiM\xE2\x80\xA6"));
    g_worker.search(q.c_str(), g_resultsKind);
    renderSearch();
}

// Tuiles « Favoris Qobuz » / « Favoris Deezer » : morceaux favoris du compte (tâche WiiM), affichés comme des
// résultats. source 0 : Qobuz, 1 : Deezer.
static void openFavoriteList(bool deezer) {
    if (g_searchBusy) {
        setMessage(wr::L("Search in progress, try again in a moment",
                         "Recherche en cours, r\xC3\xA9" "essaie dans un instant"),
                   3000);
        show();  // message affiché, tuile relâchée
        return;
    }
    g_searchBusy = true;
    g_busySince = millis();
    g_resultsTitle = deezer ? wr::L("Deezer favorites", "Favoris Deezer") : wr::L("Qobuz favorites", "Favoris Qobuz");
    g_resultsKind = deezer ? SearchKind::DeezerFavorites : SearchKind::QobuzFavorites;
    g_searchResults.clear();
    g_resultsPage = 0;
    g_resultsOpen = true;
    g_ui.renderResults(g_resultsTitle, g_searchResults, 0,
                       deezer ? wr::L("Loading your Deezer favorites\xE2\x80\xA6", "Chargement de tes favoris Deezer\xE2\x80\xA6")
                              : wr::L("Loading your Qobuz favorites\xE2\x80\xA6", "Chargement de tes favoris Qobuz\xE2\x80\xA6"));
    g_worker.search(g_resultsTitle.c_str(), g_resultsKind);
}

static void openSearch() {
    g_searchOpen = true;
    g_deezerOk = g_favOk && fav::hasDeezerModel();
    if (!g_deezerOk && g_searchSource == kTabDeezer) g_searchSource = kTabQobuz;
    if (!g_settings.qobuzDirect && g_searchSource == kTabAlbums) g_searchSource = kTabQobuz;
    g_searchStatus = g_searchBusy ? String(wr::L("Searching\xE2\x80\xA6", "Recherche en cours\xE2\x80\xA6")) : String();
    renderSearch();
}

// Touche enfoncée : dessinée tout de suite, relâchée ~90 ms plus tard (mises à jour partielles de l'écran)
static int g_keyDown = -1;
static uint32_t g_keyDownAt = 0;

static void updateSearchField() {
    g_ui.updateSearchField(g_searchQuery, g_searchStatus, g_searchBusy, g_searchSource, g_deezerOk);
}

static void releaseKey(bool force) {
    if (g_keyDown < 0 || (!force && millis() - g_keyDownAt < 90)) return;
    g_ui.pressKey(g_keyDown, false);
    g_keyDown = -1;
}

static void onSearchKey(int k) {
    if (k < 0) return;
    if (g_searchBusy && g_ui.keys()[k].kind == wr::Key::Enter) return;
    const wr::Key key = g_ui.keys()[k];
    releaseKey(true);
    g_ui.pressKey(k, true);  // retour visuel immédiat
    g_keyDown = k;
    g_keyDownAt = millis();
    sound::tap();
    switch (key.kind) {
        case wr::Key::Char:
            if (g_searchQuery.size() < 60) g_searchQuery += key.text;
            break;
        case wr::Key::Space:
            if (!g_searchQuery.empty() && g_searchQuery.back() != ' ') g_searchQuery += ' ';
            break;
        case wr::Key::Back: g_searchQuery = wr::utf8PopBack(g_searchQuery); break;
        case wr::Key::Clear: g_searchQuery.clear(); break;
        case wr::Key::Enter:
            runSearchQuery();
            return;
    }
    if (key.kind != wr::Key::Enter) g_searchStatus = g_searchBusy ? g_searchStatus : String();
    updateSearchField();  // seul le champ change
}

// Liste des résultats ; albums Qobuz : un cœur par ligne, plein si l'album est déjà une tuile
static int albumFavIndex(size_t i) {
    if (i >= g_albumResults.size()) return -1;
    const std::string key = wr::qobuzAlbumKey(g_albumResults[i].id);
    for (size_t k = 0; k < g_favs.size(); ++k)
        if (g_favs[k].key == key) return (int)k;
    return -1;
}

// Image déjà téléchargée pour cette adresse (résultats, presets, pochette en cours), sinon nullptr
static const ArtImage* knownImage(const std::string& url) {
    if (url.empty()) return nullptr;
    auto r = g_resultImages.find(url);
    if (r != g_resultImages.end()) return &r->second;
    auto p = g_presetImages.find(url);
    if (p != g_presetImages.end()) return &p->second;
    return g_art.url == url ? &g_art : nullptr;
}

// Pochettes des lignes de la page affichée : demandées une fois chacune (pas toute la liste d'un coup)
static void requestCovers() {
    const int first = g_ui.resultsFirst(), n = g_ui.resultsPerPage();
    for (int i = first; i < first + n && i < (int)g_searchResults.size(); ++i) {
        const std::string& url = g_searchResults[i].art;
        if (url.empty() || knownImage(url) || !g_coverRequested.insert(url).second) continue;
        g_artFetcher.request(url);
    }
}

static void renderResultsList(int pressed, const String& status) {
    std::vector<int> hearts;
    String sub = status;
    if (g_resultsKind == SearchKind::QobuzAlbums && g_albumResults.size() == g_searchResults.size()) {
        for (size_t i = 0; i < g_albumResults.size(); ++i) hearts.push_back(albumFavIndex(i) >= 0 ? 1 : 0);
        if (!sub.length() && !g_searchResults.empty())
            sub = String((int)g_searchResults.size()) + (g_searchResults.size() > 1 ? " albums" : " album") +
                  wr::L(" \xC2\xB7 tap to play, the heart makes it a tile",
                        " \xC2\xB7 touche pour \xC3\xA9" "couter, le c\xC5\x93ur en fait une tuile");
    }
    std::vector<const ArtImage*> covers;
    for (auto& t : g_searchResults) covers.push_back(knownImage(t.art));
    g_ui.renderResults(g_resultsTitle, g_searchResults, g_resultsPage, sub, pressed, hearts, covers);
    requestCovers();
}

// Cœur d'un album trouvé : ajouté en tuile (tâche WiiM : il faut le modèle de file Qobuz), ou retiré
static void toggleAlbumFavorite(int i) {
    if (i < 0 || i >= (int)g_albumResults.size() || g_albumFavBusy) return;
    sound::tap();
    const wr::QobuzAlbum& a = g_albumResults[i];
    const int f = albumFavIndex(i);
    if (f >= 0) {
        const bool ok = fav::remove(g_favs[f].id);
        logEvent("Favori retir\xC3\xA9 depuis la recherche (%s) : %s", g_favs[f].id.c_str(), ok ? "OK" : "erreur");
        refreshTiles();
        renderResultsList();
        return;
    }
    g_albumFavBusy = true;
    g_busySince = millis();
    g_worker.saveAlbumFavorite(a.id.c_str(), a.title.c_str(), a.artist.c_str(), a.image.c_str());
}

// Résultats arrivés (tâche WiiM)
static void onSearchDone(const WiimEvent& ev) {
    // Réponse d'une recherche abandonnée (attente trop longue) arrivée pendant une autre : pas sous son titre
    if (std::string(ev.name.c_str()) != g_resultsTitle) {
        logEvent("Recherche : r\xC3\xA9sultat ancien ignor\xC3\xA9 (%s)", ev.name.c_str());
        return;
    }
    g_searchBusy = false;
    g_albumResults = ev.albums;
    if (!ev.ok || ev.tracks.empty()) {
        if (!ev.ok) sound::error();
        g_searchStatus = !ev.ok ? String(wr::L("Search failed: ", "Recherche impossible : ")) + ev.text
                                : String(g_resultsKind == SearchKind::QobuzAlbums ? wr::L("No album found for \xE2\x80\x9C",
                                                                      "Aucun album trouv\xC3\xA9 pour \xC2\xAB ")
                                                              : wr::L("No track found for \xE2\x80\x9C",
                                                                      "Aucun morceau trouv\xC3\xA9 pour \xC2\xAB ")) +
                                      ev.name + wr::L("\xE2\x80\x9D", " \xC2\xBB");
        if (g_resultsOpen) {  // liste ouverte (favoris Qobuz) : le message s'y affiche
            g_searchResults.clear();
            g_ui.renderResults(g_resultsTitle, g_searchResults, 0,
                               !ev.ok ? String(wr::L("Failed: ", "Impossible : ")) + ev.text
                                      : String(wr::L("No favorite track on ", "Aucun morceau favori sur ")) +
                                            (g_resultsKind == SearchKind::DeezerFavorites ? "Deezer" : "Qobuz"));
        }
    } else {
        g_searchResults = ev.tracks;
        g_resultsPage = 0;
        g_resultImages.clear();  // pochettes de la liste précédente (PSRAM)
        g_coverRequested.clear();
        g_searchStatus = "";
        if (g_searchOpen || g_resultsOpen) {  // toujours sur le clavier ou la liste : on montre les résultats
            g_searchOpen = false;
            g_resultsOpen = true;
            renderResultsList(-1, String());
        }
        return;
    }
    if (g_searchOpen) renderSearch();
}

// Mémoire dans le Journal (au démarrage + toutes les 10 min) : pour repérer une fuite lors d'un essai de longue durée
static void logMemory() {
    static uint32_t last = 0;
    static bool first = true;
    const uint32_t now = millis();
    if (!first && now - last < 10 * 60 * 1000UL) return;
    first = false;
    last = now;
    logEvent("M\xC3\xA9moire : interne %u ko (min %u), PSRAM %u ko, pile WiiM %u o",
             unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
             unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024), unsigned(g_worker.stackLeft()));
}

void loop() {
    M5.update();
    auto t = M5.Touch.getDetail();
    const uint32_t now = millis();
    logMemory();
    // Images arrivées (tous les écrans : la file de l'ArtFetcher est bornée, un résultat non lu serait perdu)
    checkImages();
    retryImages();
    serviceTranscription();  // tous les écrans : un texte arrivé après la sortie de la recherche est jeté
    // Réponse jamais arrivée (évènement perdu) : on ne bloque pas la recherche ni le cœur jusqu'au redémarrage
    if ((g_searchBusy || g_favBusy || g_albumFavBusy) && wr::expired(now, g_busySince, 120000)) {
        logEvent("Pas de r\xC3\xA9ponse de la t\xC3\xA2" "che WiiM : attente abandonn\xC3\xA9" "e");
        const bool search = g_searchBusy, favorite = g_favBusy || g_albumFavBusy;
        g_searchBusy = g_favBusy = g_albumFavBusy = false;
        // Écran redessiné : plus de « recherche en cours » ni de cœur en attente
        const String noAnswer = wr::L("No answer from the WiiM. Try again.", "Pas de r\xC3\xA9ponse du WiiM. R\xC3\xA9" "essaie.");
        if (favorite) setMessage(noAnswer, 5000);
        if (search) {
            g_searchStatus = noAnswer;
            if (g_searchOpen) renderSearch();
            else if (g_resultsOpen && g_searchResults.empty()) g_ui.renderResults(g_resultsTitle, g_searchResults, 0, noAnswer);
        }
        show();
    }

    // Test tactile : rond sous le doigt tant qu'il touche ; « Fermer » pour sortir
    if (g_touchTest) {
        if (t.wasPressed()) {
            ++g_touchCount;
            if (g_ui.hitTouchTestClose(t.x, t.y)) {
                sound::tap();
                g_touchTest = false;
                g_lastTouch = now;
                show();
                delay(5);
                return;
            }
        }
        if (t.isPressed() && now - g_touchFrame > 40) {
            g_touchFrame = now;
            g_ui.renderTouchTest(t.x, t.y, g_touchCount);
        }
        serviceWiim();
        delay(5);
        return;
    }

    // Journal : rafraîchi chaque seconde ; « Fermer » pour sortir
    if (g_logOpen) {
        if (t.wasPressed() && (g_ui.hitLogClose(t.x, t.y) || g_ui.hitHome(t.x, t.y))) {
            sound::tap();
            g_logOpen = false;
            g_lastTouch = now;
            show();
        } else if (t.wasPressed() && g_ui.hitLogQueue(t.x, t.y)) {
            sound::tap();
            logEvent("Appui File du WiiM");
            g_worker.checkQueue();  // résultat dans le journal (tâche de fond)
            g_logFrame = 0;
        } else if (t.wasPressed() && g_ui.hitSearch(t.x, t.y)) {
            sound::tap();
            g_logOpen = false;
            g_lastTouch = now;
            openSearch();
        } else if (t.wasPressed() && g_ui.hitSettings(t.x, t.y)) {
            sound::tap();
            g_logOpen = false;
            g_menuOpen = true;
            g_lastTouch = now;
            g_ui.renderMenu(FW_VERSION);
        } else if (now - g_logFrame > 1000) {
            g_logFrame = now;
            g_ui.renderLog(logLines(), FW_VERSION);
        }
        serviceWiim();
        delay(5);
        return;
    }

    // Recherche : clavier, puis résultats (la barre de gauche reste active)
    if (g_searchOpen || g_resultsOpen) {
        if (t.wasPressed()) {
            g_lastTouch = now;
            if (g_brightness != kBrightOn) setBrightness(kBrightOn);
            else if (g_listening && g_dictation) {
                // Pendant la dictée : ■ termine ; Accueil ou Réglages annulent ; le reste est ignoré
                if (g_ui.hitMic(t.x, t.y)) {
                    sound::tap();
                    finishDictation(true);
                } else if (g_ui.hitHome(t.x, t.y) || g_ui.hitSettings(t.x, t.y)) {
                    finishDictation(false);
                }
            } else if (g_searchOpen && g_ui.hitMic(t.x, t.y)) {
                if (!g_searchBusy && !g_stt.busy) startVoice();  // bip, puis écoute jusqu'au bouton rouge
            } else if (g_ui.hitHome(t.x, t.y)) {
                sound::tap();
                g_searchOpen = g_resultsOpen = false;
                show();
            } else if (g_ui.hitSettings(t.x, t.y)) {
                sound::tap();
                g_searchOpen = g_resultsOpen = false;
                g_menuOpen = true;
                g_ui.renderMenu(FW_VERSION);
            } else if (g_searchOpen && g_ui.hitSource(t.x, t.y) >= 0) {
                sound::tap();
                g_searchSource = g_ui.hitSource(t.x, t.y);
                g_searchStatus = "";
                updateSearchField();
            } else if (g_searchOpen) {
                onSearchKey(g_ui.hitKey(t.x, t.y));
            } else {
                int r = g_ui.hitResult(t.x, t.y), pg = g_ui.hitResultsPage(t.x, t.y), h = g_ui.hitResultHeart(t.x, t.y);
                if (h >= 0) {
                    toggleAlbumFavorite(h);
                } else if (r >= 0) {
                    sound::tap();
                    renderResultsList(r);  // ligne enfoncée
                    logEvent("Recherche : lecture de %d (%s)", r + 1, g_searchResults[r].title.c_str());
                    g_worker.playSearchResult(r);
                    markPending(wr::kPlayPause, Await::Music, true);
                    setMessage((std::string(wr::L("Playing: ", "Lecture : ")) + g_searchResults[r].title +
                                (loadsAlbum(g_resultsKind) ? wr::L(" (loading the album\xE2\x80\xA6)",
                                                              " (chargement de l'album\xE2\x80\xA6)")
                                                      : ""))
                                   .c_str(),
                               loadsAlbum(g_resultsKind) ? 9000 : 4000);
                    g_resultsOpen = false;
                    show();
                } else if (pg) {
                    sound::tap();
                    g_resultsPage += pg;
                    renderResultsList();
                } else if (g_ui.hitResultsEdit(t.x, t.y) || g_ui.hitSearch(t.x, t.y)) {
                    sound::tap();
                    g_resultsOpen = false;
                    openSearch();
                }
            }
        }
        releaseKey(false);
        serviceVoice();
        serviceWiim();
        delay(5);
        return;
    }

    // Vue ambiance : n'importe quel contact (même un doigt déjà posé) ramène les commandes
    if (g_ambient && t.isPressed() && !t.wasPressed()) {
        g_lastTouch = now;
        setBrightness(kBrightOn);
        exitAmbient();
    }

    if (t.wasPressed()) {
        g_lastTouch = now;
        if (g_brightness != kBrightOn) {
            // Écran atténué ou éteint : ce premier appui ne fait que le rallumer
            logEvent("Appui : r\xC3\xA9veil de l'\xC3\xA9" "cran (%d,%d)", t.x, t.y);
            setBrightness(kBrightOn);
            if (g_ambient) exitAmbient();
        } else if (g_menuOpen && g_ui.hitSearch(t.x, t.y)) {
            sound::tap();
            g_menuOpen = false;
            openSearch();
        } else if (g_menuOpen && g_ui.hitHome(t.x, t.y)) {
            sound::tap();  // Accueil : fermer les réglages
            g_menuOpen = false;
            show();
        } else if (g_menuOpen) {
            Ui::MenuChoice c = g_ui.hitMenu(t.x, t.y);
            if (c != Ui::MenuNone) sound::tap();
            if (c == Ui::MenuConfig) {
                restartIntoPortal(wr::L("Setup requested.", "Reconfiguration demand\xC3\xA9" "e."));  // redémarre

            } else if (c == Ui::MenuUpdate) {
                runUpdateMode(g_ui);  // redémarre si une mise à jour est installée
                g_menuOpen = false;
                show();
            } else if (c == Ui::MenuLog) {
                g_menuOpen = false;
                g_logOpen = true;
                g_logFrame = now;
                g_ui.renderLog(logLines(), FW_VERSION);
            } else if (c == Ui::MenuTouch) {
                g_menuOpen = false;
                g_touchTest = true;
                g_touchCount = 0;
                g_ui.renderTouchTest(-1, -1, 0);
            } else if (c == Ui::MenuSleep) {
                g_menuOpen = false;
                goToSleep("demand\xC3\xA9" "e depuis R\xC3\xA9glages");
            } else if (c == Ui::MenuClose) {
                g_menuOpen = false;
                show();
            }
        } else if (g_ambient) {
            logEvent("Appui en vue ambiance (%d,%d)%s", t.x, t.y, g_ui.hitHome(t.x, t.y) ? " : Accueil" : "");
            exitAmbient();  // vue ambiance : un appui ramène les commandes
            sound::tap();
        } else if (handleBars(t.x, t.y)) {
            // volume, coupure du son ou progression
        } else if (g_ui.hitHeader(t.x, t.y)) {
            g_headerPressed = true;  // appui long : Réglages
            g_headerPressStart = now;
        } else {
            handleTap(t.x, t.y);
        }
    }
    serviceStrip(t);
    if (g_headerPressed) {
        if (!t.isPressed()) g_headerPressed = false;
        else if (now - g_headerPressStart > kLongPressMs) {
            sound::confirm();
            g_headerPressed = false;
            g_menuOpen = true;  // Réglages : configuration ou mise à jour
            g_ui.renderMenu(FW_VERSION);
        }
    }

    serviceWiim();
    tickStatus(now);
    serviceMic();
    serviceVoice();
    serviceMotion(now);

    // Fin de l'effet « bouton enfoncé »
    if (g_flashUntil && (int32_t)(now - g_flashUntil) >= 0) {
        g_flashUntil = 0;
        show();
    }
    // Progression : une image par seconde pendant la lecture (écran allumé ; WiiM joignable)
    const auto& p = g_state.player;
    const bool playing = playingNow();
    if (playing) g_lastPlayingAt = now;
    // Seule la ligne de progression est redessinée si rien d'autre n'a changé (~3 % de l'écran) ; tout de suite
    // après un saut de position
    if (g_brightness > 0 && playing && p.durationMs > 0 && (g_progressDue || now - g_lastTick >= 1000)) {
        g_progressDue = false;
        if (!g_dirty && !modalOpen() && g_ui.updateProgress(displayState())) g_lastTick = now;
        else show();
    }
    // Chargement : anneau qui tourne autour du bouton lecture (~20 images/s)
    // Le WiiM n'a rien confirmé : on arrête d'attendre (un album chargé par le WiiM peut prendre ~15 s).
    // markPending() note millis(), plus tard que `now` : différence signée (wr::expired), sinon elle bouclait à
    // ~4e9 et l'anneau était annulé dans le même tour.
    if (g_pendingT >= 0 && wr::expired(now, g_pendingSince, g_await == Await::Music ? 20000u : 10000u)) {
        g_pendingT = -1;
        show();
    }
    if (g_brightness > 0 && !g_ambient && (p.loading || g_pendingT >= 0) && !g_stripTouch && now - g_lastTick >= 50) {
        // Animation d'attente : seulement la rangée des boutons, sauf si un rendu complet est déjà prévu ; le
        // bouton reste enfoncé pendant son court effet
        g_lastTick = now;
        if (g_dirty || modalOpen() || !g_ui.updateTransport(displayState(), flashActive(now) ? g_flashTransport : -1))
            show();
    }

    // Écran et veille : depuis le dernier appui, et depuis la dernière lecture vue (lib/core/src/appstate.h)
    auto since = [now](uint32_t t) { return (int32_t)(now - t) > 0 ? now - t : 0u; };
    wr::IdleInput idle;
    idle.touchIdleMs = since(g_lastTouch);
    idle.notPlayingMs = since(g_lastPlayingAt);
    idle.online = g_state.online;
    idle.playing = p.playing;
    idle.modal = modalOpen();
    idle.ambient = g_ambient;
    idle.fullBright = g_brightness == kBrightOn;
    idle.dark = g_brightness == 0;
    idle.sleepMin = g_settings.sleepMin;
    wr::IdleTimes times;
    times.ambientMs = kAmbientMs;
    times.dimMs = kDimMs;
    times.offMs = kOffMs;
    switch (wr::idleAction(idle, times)) {
        case wr::IdleAction::None: break;
        case wr::IdleAction::Ambient: enterAmbient(); break;
        case wr::IdleAction::Dim: setBrightness(kBrightDim); break;
        case wr::IdleAction::Off:
            if (g_ambient) exitAmbient();  // micro coupé avec l'écran
            setBrightness(0);
            break;
        case wr::IdleAction::Sleep: goToSleep("automatique"); break;  // sans appui ni lecture depuis sleepMin minutes
    }
    renderNow();
    delay(5);
}
