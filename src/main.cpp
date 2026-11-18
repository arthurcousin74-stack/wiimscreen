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
//
// Fichiers : main.cpp (démarrage, boucle, appuis, relevés du WiiM, vue ambiance, menus), app_search.cpp
// (recherche et résultats), app_voice.cpp (dictée), app_tiles.cpp (presets, favoris, images) ; état partagé et
// écran affiché (enum class Screen) dans app_internal.h.

#include <M5Unified.h>
#include <WiFi.h>

#include <esp_heap_caps.h>

#include <atomic>
#include <cmath>
#include <map>
#include <set>

#include "appstate.h"
#include "net/art.h"
#include "core.h"
#include "decisions.h"
#include "i18n.h"
#include "net/discovery.h"
#include "dsp.h"
#include "storage/favorites.h"
#include "app/log.h"
#include "device/ota.h"
#include "device/portal.h"
#include "storage/settings.h"
#include "device/sound.h"
#include "device/key_input.h"
#include "ui/ui.h"
#include "app/version.h"
#include "voice.h"
#include "net/voice_service.h"
#include "net/wiim_worker.h"
#include "app/app_internal.h"

namespace app {

// État de l'application (types, rôles : app_internal.h). Ordre de construction inchangé.
Settings g_settings;
Ui g_ui;
WiimWorker g_worker;
UiState g_state;
ImageCache g_img;
TileData g_tiles;
ActiveTile g_active;
WorkerJobs g_jobs;
SearchScreen g_search;
OpenScreens g_view;
VoiceInput g_voice;
MicInput g_mic;
StripTouch g_strip;
PressFlash g_flash;
MotionWake g_motion;
Redraw g_draw;
IdleClock g_idle;
StatusClock g_status;
HeaderPress g_header;
PendingCommand g_pending;

// Le WiiM charge l'album du morceau choisi (Deezer, albums) : plus long qu'une piste de la file
static bool loadsAlbum(SearchKind k) {
    return k == SearchKind::Deezer || k == SearchKind::DeezerFavorites || k == SearchKind::QobuzAlbums;
}

static constexpr int kSwipePx = 50;

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
    if (b == g_idle.brightness) return;
    g_idle.brightness = b;
    M5.Display.setBrightness(b);
}

static void markPending(int button, Await what, bool viaEvent = false);

// Lecture en cours, d'après le dernier relevé ; WiiM injoignable : son dernier état ne compte plus
static bool playingNow() { return g_state.online && g_state.player.playing; }

// État affiché : la position avance entre deux relevés pendant la lecture ; anneau du bouton en attente
static UiState displayState() {
    UiState s = g_state;
    s.pending = g_pending.button;
    auto& p = s.player;
    if (s.online && p.playing && p.durationMs > 0)
        p.positionMs = std::min(p.durationMs, p.positionMs + (millis() - g_status.at));
    return s;
}

static void markPending(int button, Await what, bool viaEvent) {
    g_pending.button = button;
    g_pending.await = what;
    g_pending.since = millis();
    // Commande passée par la file : confirmée par un relevé commencé après elle. Favori ou résultat de
    // recherche (autre chemin) : seulement après l'évènement de fin (FavPlayed).
    g_pending.seq = viaEvent ? UINT32_MAX : g_worker.queued();
    g_pending.ready = !viaEvent;
    g_pending.readyAt = g_pending.since;
    g_pending.sawLoading = false;
    g_pending.title = g_state.player.title;
    g_pending.playing = g_state.player.playing;  // déjà basculé pour lecture/pause
    g_pending.pos = displayState().player.positionMs;
    show();
}

// Affichage différé : show() marque l'écran à redessiner, renderNow() le dessine une
// seule fois par tour de boucle, même si plusieurs changements sont arrivés.
void show() { g_draw.dirty = true; }

// Écran autre que les commandes. La dictée (g_voice.listening) n'a lieu que sur Search ou Results : elle
// commence sur le clavier, et seule la fin d'une recherche peut passer de l'un à l'autre pendant l'écoute.
static bool modalOpen() { return g_view.screen != Screen::Controls; }

// Court effet « bouton enfoncé » encore actif (différence signée : juste après 49 jours aussi)
static bool flashActive(uint32_t now) { return g_flash.until && (int32_t)(g_flash.until - now) > 0; }

static void renderNow() {
    if (!g_draw.dirty || modalOpen()) return;  // écrans modaux : rien par-dessus (rendu à leur fermeture)
    g_draw.dirty = false;
    const uint32_t now = millis();
    // Bouton enfoncé : tuile sous le doigt, ou court effet après un appui
    int pressedPreset = (g_strip.touch && !g_strip.dragged) ? g_strip.preset : -1;
    int pressedTransport = -1;
    if (flashActive(now)) {
        if (pressedPreset < 0) pressedPreset = g_flash.preset;
        pressedTransport = g_flash.transport;
    }
    g_draw.lastTick = now;
    g_draw.progressDue = false;  // la progression fait partie de l'image complète
    g_ui.setFavorite(favoriteState());
    if (g_view.ambient) g_ui.renderAmbient(displayState());
    else g_ui.render(displayState(), pressedPreset, pressedTransport);
}

// Bouton enfoncé pendant un court instant, puis relâché par la boucle
static void flash(int preset, int transport) {
    g_flash.preset = preset;
    g_flash.transport = transport;
    g_flash.until = millis() + kFlashMs;
    show();
}

void setMessage(const String& m, uint32_t durationMs) {
    g_state.message = m;
    g_status.messageUntil = m.length() ? millis() + durationMs : 0;
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
    g_view.ambient = true;
    logEvent("Vue ambiance");
    if (g_settings.visualizer) {
        sound::suspend();
        g_mic.on = M5.Mic.begin();
        logEvent("Micro (visualiseur) : %s", g_mic.on ? "OK" : "\xC3\xA9" "chec");
        g_mic.pending = false;
        g_mic.vis.reset();
        g_ui.showLevels(g_mic.vis.levels(), g_mic.vis.peaks());
    }
    show();
}

static void exitAmbient() {
    logEvent("Sortie de la vue ambiance");
    if (g_mic.on) {
        M5.Mic.end();
        g_mic.on = false;
        g_mic.pending = false;
    }
    if (g_settings.visualizer) sound::resume();
    g_view.ambient = false;
    show();
}

// Micro : traite le bloc terminé pendant que le suivant s'enregistre
static void serviceMic() {
    if (!g_mic.on) return;
    if (!g_mic.pending) {
        g_mic.pending = M5.Mic.record(g_mic.buf[g_mic.cur], kMicBlock, kMicRate);
        return;
    }
    if (M5.Mic.isRecording()) return;
    const int done = g_mic.cur;
    g_mic.cur ^= 1;
    g_mic.pending = M5.Mic.record(g_mic.buf[g_mic.cur], kMicBlock, kMicRate);
    float db[kBands];
    wr::spectrumBands(g_mic.buf[done], kMicBlock, kMicRate, kBands, db);
    g_mic.vis.update(db);
    if (millis() - g_mic.lastVisFrame >= 33) {  // ~30 images/s, zone du visualiseur seulement
        g_mic.lastVisFrame = millis();
        g_ui.showLevels(g_mic.vis.levels(), g_mic.vis.peaks());
    }
}

// Mouvement détecté : écran rallumé et retour aux commandes
static void serviceMotion(uint32_t now) {
    if (!g_settings.motionWake || now - g_motion.lastImu < 20) return;
    g_motion.lastImu = now;
    float ax, ay, az;
    M5.Imu.update();
    if (!M5.Imu.getAccel(&ax, &ay, &az)) return;
    if (!g_motion.detector.feed(ax, ay, az)) return;
    g_idle.lastTouch = now;
    if (g_idle.brightness != kBrightOn) setBrightness(kBrightOn);
    if (g_view.ambient) exitAmbient();
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

static void goToSleep(const char* why) {
    logEvent("Mise en veille (%s)", why);
    if (g_voice.listening) stopListening();
    sound::confirm();
    g_ui.showInfo(wr::L("Sleep", "Mise en veille"),
                  wr::L("The Tab5 is turning off. Press its power button to turn it back on.",
                        "Le Tab5 s'\xC3\xA9teint. Appuie sur son bouton d'alimentation pour le rallumer."));
    setBrightness(kBrightOn);
    delay(2500);
    setBrightness(0);
    M5.Power.powerOff();  // ne revient pas (coupure, ou sommeil profond)
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
        g_status.at = r.at;
        if (r.queueKey != g_tiles.queueKey) {
            g_tiles.queueKey = r.queueKey;
            show();  // le ♥ peut changer
        }

        if (next.meta.artUrl != g_img.wanted) {
            g_img.wanted = next.meta.artUrl;
            if (g_img.wanted.empty()) {
                g_ui.setArt(nullptr);
                g_img.art.reset();
                artCleared = true;
            } else {
                g_img.fetcher.request(g_img.wanted, true);
            }
        }
    }

    // Preset actif : on retient la source quelques secondes après l'appui (le temps que
    // le WiiM bascule), puis on l'oublie si la source change ou si la lecture s'arrête. Rien n'est jugé pendant ce
    // délai : sur un WiiM à l'arrêt, le premier relevé (rien ne joue, pas de titre) effaçait la tuile choisie.
    if (g_active.index >= 0 && next.online && !next.player.loading && wr::expired(millis(), g_active.since, 6000)) {
        bool stopped = !next.player.playing && next.player.title.empty();
        if (g_active.mode < 0) g_active.mode = next.player.mode;
        else if (next.player.mode != g_active.mode) stopped = true;
        if (stopped) {
            g_active.index = -1;
            g_ui.setActivePreset(-1);
        }
    }

    if (next.online) {
        bool due = millis() - g_tiles.lastFetch > (g_tiles.fromWiim ? kPresetRefreshMs : kPresetRetryMs);
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
            if (gap > 2500) g_draw.progressDue = true;
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
    if (g_tiles.favOk && s.configured()) fav::saveSettingsBackup(settingsToBackup(s));
}

static void serviceWiim() {
    WiimReport r;
    // Relevé commencé avant la dernière commande : il annulerait l'affichage optimiste
    if (g_worker.takeReport(r) && r.cmdSeq >= g_worker.queued()) {
        applyReport(r);
        // Confirmation de la commande en attente
        if (g_pending.button >= 0 && r.online && r.cmdSeq >= g_pending.seq) {
            // Music : sans nouveau titre (tuile de ce qui joue déjà, radio sans titre), le WiiM qui joue après
            // avoir chargé, ou quelques secondes après la commande, confirme aussi (sinon l'anneau tournait 20 s)
            bool settled = false;
            if (g_pending.await == Await::Music && g_pending.ready) {
                if (r.player.loading) g_pending.sawLoading = true;
                settled = wr::musicSettled(r.player, g_pending.sawLoading, millis() - g_pending.readyAt);
            }
            if (settled || wr::pendingConfirmed(g_pending.await, r.player, g_pending.title, g_pending.pos, g_pending.playing)) {
                g_pending.button = -1;
                show();
            }
        }
    }

    WiimEvent ev;
    while (g_worker.takeEvent(ev)) {
        switch (ev.kind) {
            case WiimEvent::CommandDone:
                if (ev.error != WiimError::None) g_pending.button = -1;
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
                if (!ev.automatic) g_jobs.favorite = g_jobs.albumFavorite = false;
                if (ev.ok) {
                    sound::confirm();
                    refreshTiles();
                    if (g_view.screen == Screen::Results && g_search.resultsKind == SearchKind::QobuzAlbums) renderResultsList();  // cœur plein
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
                    g_tiles.deezerAlbums = ev.albums;
                    refreshTiles();
                }
                break;
            case WiimEvent::QobuzFavorites:
                if (ev.ok) {
                    g_tiles.qobuzAlbums = ev.albums;
                    refreshTiles();
                }
                break;
            case WiimEvent::FavPlayed:
                if (g_pending.button >= 0) {
                    g_pending.seq = g_worker.queued();  // les relevés suivants peuvent confirmer
                    g_pending.ready = true;             // la file est lancée : délai de musicSettled compté d'ici
                    g_pending.readyAt = millis();
                }
                if (!ev.ok) {
                    g_pending.button = -1;
                    sound::error();
                    g_active.index = -1;
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
    if (now - g_status.lastTick < 1000) return;
    g_status.lastTick = now;
    bool changed = false;
    int b = batteryLevel();
    if (b != g_state.battery) g_state.battery = b, changed = true;
    if (g_status.messageUntil && (int32_t)(now - g_status.messageUntil) > 0) {
        g_state.message = "";
        g_status.messageUntil = 0;
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

// Sans adresse enregistrée : recherche sur le Wi-Fi, puis choix sur l'écran. Même pour un seul lecteur trouvé :
// son certificat sera épinglé au premier échange, et n'importe quel appareil du réseau peut répondre à la
// recherche. Le nom et l'adresse sont montrés, un appui confirme.
static void chooseWiim() {
    for (;;) {
        g_ui.showInfo(wr::L("Looking for the WiiM\xE2\x80\xA6", "Recherche du WiiM\xE2\x80\xA6"),
                      wr::L("The Tab5 is looking for WiiM players on your Wi-Fi.",
                            "Le Tab5 cherche les lecteurs WiiM sur ton Wi-Fi."));
        std::vector<WiimDevice> found = discoverWiims();
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
        g_status.at = millis();
        g_worker.command("setPlayerCmd:seek:" + String(pos / 1000));  // secondes
        show();
        return true;
    }
    return false;
}

static void playPreset(int p) {
    if (p < 0 || p >= (int)g_tiles.list.size()) return;
    logEvent("Preset %d : %s", g_tiles.list[p].slot, g_tiles.list[p].label.c_str());
    sound::tap();
    if (g_tiles.list[p].favId == kQobuzFavsId || g_tiles.list[p].favId == kDeezerFavsId) {
        openFavoriteList(g_tiles.list[p].favId == kDeezerFavsId);  // liste : on choisit, rien ne joue avant
        return;
    }
    g_active.index = p;
    g_active.since = millis();
    g_active.mode = -1;
    g_ui.setActivePreset(p);
    if (!g_tiles.list[p].favId.empty()) {
        g_worker.playFavorite(g_tiles.list[p].favId.c_str());  // file enregistrée
        markPending(wr::kPlayPause, Await::Music, true);
    } else {
        g_worker.command("MCUKeyShortClick:" + String(g_tiles.list[p].slot));
        markPending(wr::kPlayPause, Await::Music);
    }
    flash(p, -1);
}

// Bande des presets, suivi du doigt : enfoncer, glisser (page), relâcher (lancer)
static void serviceStrip(const m5::touch_detail_t& t) {
    if (!g_strip.touch) return;
    if (t.isPressed()) {
        if (!g_strip.dragged && abs(t.x - g_strip.startX) > kSwipePx) {
            g_strip.dragged = true;  // c'est un glissement : la tuile n'est plus enfoncée
            show();
        }
        return;
    }
    g_strip.touch = false;
    if (g_strip.dragged) {
        int dx = t.x - g_strip.startX;
        int before = g_ui.page();
        g_ui.setPage(before + (dx < 0 ? 1 : -1));
        if (g_ui.page() != before) sound::tap();
        show();
    } else if (g_strip.preset >= 0) {
        playPreset(g_strip.preset);
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
        g_strip.touch = true;
        g_strip.dragged = false;
        g_strip.startX = x;
        g_strip.preset = g_ui.hitPreset(x, y);
        if (g_strip.preset >= 0) show();  // tuile montrée enfoncée tant que le doigt reste
        return;
    }
    if (g_ui.hitFav(x, y)) {  // ♥ : ajouter ce qui joue aux favoris, ou l'en retirer
        if (g_jobs.favorite) return;
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
            g_jobs.favorite = true;
            g_jobs.since = millis();
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
        g_view.screen = Screen::Menu;
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
            g_status.at = millis();
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

}  // namespace app

using namespace app;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    // Avant tout appel Wi-Fi (y compris le portail de configuration)
    WiFi.setPins(kSdioClk, kSdioCmd, kSdioD0, kSdioD1, kSdioD2, kSdioD3, kSdioRst);
    setBrightness(kBrightOn);

    // Réglages : NVS ; copie de secours dans LittleFS (favoris), qu'un flash USB complet n'efface pas
    g_tiles.favOk = fav::begin();
    bool presetsOk = loadSettings(g_settings);
    bool restored = false;
    std::string backup;
    if (!g_settings.configured() && g_tiles.favOk && fav::settingsBackup(backup) && settingsFromBackup(backup, g_settings)) {
        saveSettings(g_settings);  // NVS effacée (flash USB) : réglages remis depuis la copie
        presetsOk = loadSettings(g_settings);
        restored = true;
    }
    if (g_settings.configured() && g_tiles.favOk) fav::saveSettingsBackup(settingsToBackup(g_settings));
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
    g_img.fetcher.begin();
    logEvent("Favoris : %s, %u", g_tiles.favOk ? "OK" : "m\xC3\xA9moire indisponible", (unsigned)fav::list().size());
    g_worker.begin(g_settings);
    if (!g_settings.autoPresets()) g_tiles.wiimPresets = g_settings.presets;  // liste saisie : tout de suite
    refreshTiles();
    requestPresets();
    g_state.online = true;  // jusqu'au premier relevé (une seconde environ)
    g_state.battery = batteryLevel();
    g_idle.lastTouch = g_idle.lastPlayingAt = millis();
    show();
}

void loop() {
    M5.update();
    auto t = M5.Touch.getDetail();
    // Touches (télécommande, clavier) : vidées ici, pas encore utilisées (étape D-pad à venir). Rien sur le Tab5.
    wr::KeyEvent keyEv;
    while (takeKey(keyEv)) { /* consumed by the D-pad step */ }
    const uint32_t now = millis();
    logMemory();
    // Images arrivées (tous les écrans : la file de l'ArtFetcher est bornée, un résultat non lu serait perdu)
    checkImages();
    retryImages();
    serviceTranscription();  // tous les écrans : un texte arrivé après la sortie de la recherche est jeté
    // Réponse jamais arrivée (évènement perdu) : on ne bloque pas la recherche ni le cœur jusqu'au redémarrage
    if ((g_jobs.search || g_jobs.favorite || g_jobs.albumFavorite) && wr::expired(now, g_jobs.since, 120000)) {
        logEvent("Pas de r\xC3\xA9ponse de la t\xC3\xA2" "che WiiM : attente abandonn\xC3\xA9" "e");
        const bool search = g_jobs.search, favorite = g_jobs.favorite || g_jobs.albumFavorite;
        g_jobs.search = g_jobs.favorite = g_jobs.albumFavorite = false;
        // Écran redessiné : plus de « recherche en cours » ni de cœur en attente
        const String noAnswer = wr::L("No answer from the WiiM. Try again.", "Pas de r\xC3\xA9ponse du WiiM. R\xC3\xA9" "essaie.");
        if (favorite) setMessage(noAnswer, 5000);
        if (search) {
            g_search.status = noAnswer;
            if (g_view.screen == Screen::Search) renderSearch();
            else if (g_view.screen == Screen::Results && g_search.results.empty()) g_ui.renderResults(g_search.resultsTitle, g_search.results, 0, noAnswer);
        }
        show();
    }

    // Test tactile : rond sous le doigt tant qu'il touche ; « Fermer » pour sortir
    if (g_view.screen == Screen::TouchTest) {
        if (t.wasPressed()) {
            ++g_view.touchCount;
            if (g_ui.hitTouchTestClose(t.x, t.y)) {
                sound::tap();
                g_view.screen = Screen::Controls;
                g_idle.lastTouch = now;
                show();
                delay(5);
                return;
            }
        }
        if (t.isPressed() && now - g_view.touchFrame > 40) {
            g_view.touchFrame = now;
            g_ui.renderTouchTest(t.x, t.y, g_view.touchCount);
        }
        serviceWiim();
        delay(5);
        return;
    }

    // Journal : rafraîchi chaque seconde ; « Fermer » pour sortir
    if (g_view.screen == Screen::Log) {
        if (t.wasPressed() && (g_ui.hitLogClose(t.x, t.y) || g_ui.hitHome(t.x, t.y))) {
            sound::tap();
            g_view.screen = Screen::Controls;
            g_idle.lastTouch = now;
            show();
        } else if (t.wasPressed() && g_ui.hitLogQueue(t.x, t.y)) {
            sound::tap();
            logEvent("Appui File du WiiM");
            g_worker.checkQueue();  // résultat dans le journal (tâche de fond)
            g_view.logFrame = 0;
        } else if (t.wasPressed() && g_ui.hitSearch(t.x, t.y)) {
            sound::tap();
            g_idle.lastTouch = now;
            openSearch();
        } else if (t.wasPressed() && g_ui.hitSettings(t.x, t.y)) {
            sound::tap();
            g_view.screen = Screen::Menu;
            g_idle.lastTouch = now;
            g_ui.renderMenu(FW_VERSION);
        } else if (now - g_view.logFrame > 1000) {
            g_view.logFrame = now;
            g_ui.renderLog(logLines(), FW_VERSION);
        }
        serviceWiim();
        delay(5);
        return;
    }

    // Recherche : clavier, puis résultats (la barre de gauche reste active)
    if (g_view.screen == Screen::Search || g_view.screen == Screen::Results) {
        if (t.wasPressed()) {
            g_idle.lastTouch = now;
            if (g_idle.brightness != kBrightOn) setBrightness(kBrightOn);
            else if (g_voice.listening && g_voice.dictation) {
                // Pendant la dictée : ■ termine ; Accueil ou Réglages annulent ; le reste est ignoré
                if (g_ui.hitMic(t.x, t.y)) {
                    sound::tap();
                    finishDictation(true);
                } else if (g_ui.hitHome(t.x, t.y) || g_ui.hitSettings(t.x, t.y)) {
                    finishDictation(false);
                }
            } else if (g_view.screen == Screen::Search && g_ui.hitMic(t.x, t.y)) {
                if (!g_jobs.search && !g_voice.stt.busy) startVoice();  // bip, puis écoute jusqu'au bouton rouge
            } else if (g_ui.hitHome(t.x, t.y)) {
                sound::tap();
                g_view.screen = Screen::Controls;
                show();
            } else if (g_ui.hitSettings(t.x, t.y)) {
                sound::tap();
                g_view.screen = Screen::Menu;
                g_ui.renderMenu(FW_VERSION);
            } else if (g_view.screen == Screen::Search && g_ui.hitSource(t.x, t.y) >= 0) {
                sound::tap();
                g_search.source = g_ui.hitSource(t.x, t.y);
                g_search.status = "";
                updateSearchField();
            } else if (g_view.screen == Screen::Search) {
                onSearchKey(g_ui.hitKey(t.x, t.y));
            } else {
                int r = g_ui.hitResult(t.x, t.y), pg = g_ui.hitResultsPage(t.x, t.y), h = g_ui.hitResultHeart(t.x, t.y);
                if (h >= 0) {
                    toggleAlbumFavorite(h);
                } else if (r >= 0) {
                    sound::tap();
                    renderResultsList(r);  // ligne enfoncée
                    logEvent("Recherche : lecture de %d (%s)", r + 1, g_search.results[r].title.c_str());
                    g_worker.playSearchResult(r);
                    markPending(wr::kPlayPause, Await::Music, true);
                    setMessage((std::string(wr::L("Playing: ", "Lecture : ")) + g_search.results[r].title +
                                (loadsAlbum(g_search.resultsKind) ? wr::L(" (loading the album\xE2\x80\xA6)",
                                                              " (chargement de l'album\xE2\x80\xA6)")
                                                      : ""))
                                   .c_str(),
                               loadsAlbum(g_search.resultsKind) ? 9000 : 4000);
                    g_view.screen = Screen::Controls;
                    show();
                } else if (pg) {
                    sound::tap();
                    g_search.page += pg;
                    renderResultsList();
                } else if (g_ui.hitResultsEdit(t.x, t.y) || g_ui.hitSearch(t.x, t.y)) {
                    sound::tap();
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
    if (g_view.ambient && t.isPressed() && !t.wasPressed()) {
        g_idle.lastTouch = now;
        setBrightness(kBrightOn);
        exitAmbient();
    }

    if (t.wasPressed()) {
        g_idle.lastTouch = now;
        if (g_idle.brightness != kBrightOn) {
            // Écran atténué ou éteint : ce premier appui ne fait que le rallumer
            logEvent("Appui : r\xC3\xA9veil de l'\xC3\xA9" "cran (%d,%d)", t.x, t.y);
            setBrightness(kBrightOn);
            if (g_view.ambient) exitAmbient();
        } else if (g_view.screen == Screen::Menu && g_ui.hitSearch(t.x, t.y)) {
            sound::tap();
            openSearch();
        } else if (g_view.screen == Screen::Menu && g_ui.hitHome(t.x, t.y)) {
            sound::tap();  // Accueil : fermer les réglages
            g_view.screen = Screen::Controls;
            show();
        } else if (g_view.screen == Screen::Menu) {
            Ui::MenuChoice c = g_ui.hitMenu(t.x, t.y);
            if (c != Ui::MenuNone) sound::tap();
            if (c == Ui::MenuConfig) {
                restartIntoPortal(wr::L("Setup requested.", "Reconfiguration demand\xC3\xA9" "e."));  // redémarre

            } else if (c == Ui::MenuUpdate) {
                runUpdateMode(g_ui);  // redémarre si une mise à jour est installée
                g_view.screen = Screen::Controls;
                show();
            } else if (c == Ui::MenuLog) {
                g_view.screen = Screen::Log;
                g_view.logFrame = now;
                g_ui.renderLog(logLines(), FW_VERSION);
            } else if (c == Ui::MenuTouch) {
                g_view.screen = Screen::TouchTest;
                g_view.touchCount = 0;
                g_ui.renderTouchTest(-1, -1, 0);
            } else if (c == Ui::MenuSleep) {
                g_view.screen = Screen::Controls;
                goToSleep("demand\xC3\xA9" "e depuis R\xC3\xA9glages");
            } else if (c == Ui::MenuClose) {
                g_view.screen = Screen::Controls;
                show();
            }
        } else if (g_view.ambient) {
            logEvent("Appui en vue ambiance (%d,%d)%s", t.x, t.y, g_ui.hitHome(t.x, t.y) ? " : Accueil" : "");
            exitAmbient();  // vue ambiance : un appui ramène les commandes
            sound::tap();
        } else if (handleBars(t.x, t.y)) {
            // volume, coupure du son ou progression
        } else if (g_ui.hitHeader(t.x, t.y)) {
            g_header.pressed = true;  // appui long : Réglages
            g_header.since = now;
        } else {
            handleTap(t.x, t.y);
        }
    }
    serviceStrip(t);
    if (g_header.pressed) {
        if (!t.isPressed()) g_header.pressed = false;
        else if (now - g_header.since > kLongPressMs) {
            sound::confirm();
            g_header.pressed = false;
            g_view.screen = Screen::Menu;  // Réglages : configuration ou mise à jour
            g_ui.renderMenu(FW_VERSION);
        }
    }

    serviceWiim();
    tickStatus(now);
    serviceMic();
    serviceVoice();
    serviceMotion(now);

    // Fin de l'effet « bouton enfoncé »
    if (g_flash.until && (int32_t)(now - g_flash.until) >= 0) {
        g_flash.until = 0;
        show();
    }
    // Progression : une image par seconde pendant la lecture (écran allumé ; WiiM joignable)
    const auto& p = g_state.player;
    const bool playing = playingNow();
    if (playing) g_idle.lastPlayingAt = now;
    // Seule la ligne de progression est redessinée si rien d'autre n'a changé (~3 % de l'écran) ; tout de suite
    // après un saut de position
    if (g_idle.brightness > 0 && playing && p.durationMs > 0 && (g_draw.progressDue || now - g_draw.lastTick >= 1000)) {
        g_draw.progressDue = false;
        if (!g_draw.dirty && !modalOpen() && g_ui.updateProgress(displayState())) g_draw.lastTick = now;
        else show();
    }
    // Chargement : anneau qui tourne autour du bouton lecture (~20 images/s)
    // Le WiiM n'a rien confirmé : on arrête d'attendre (un album chargé par le WiiM peut prendre ~15 s).
    // markPending() note millis(), plus tard que `now` : différence signée (wr::expired), sinon elle bouclait à
    // ~4e9 et l'anneau était annulé dans le même tour.
    if (g_pending.button >= 0 && wr::expired(now, g_pending.since, g_pending.await == Await::Music ? 20000u : 10000u)) {
        g_pending.button = -1;
        show();
    }
    if (g_idle.brightness > 0 && !g_view.ambient && (p.loading || g_pending.button >= 0) && !g_strip.touch && now - g_draw.lastTick >= 50) {
        // Animation d'attente : seulement la rangée des boutons, sauf si un rendu complet est déjà prévu ; le
        // bouton reste enfoncé pendant son court effet
        g_draw.lastTick = now;
        if (g_draw.dirty || modalOpen() || !g_ui.updateTransport(displayState(), flashActive(now) ? g_flash.transport : -1))
            show();
    }

    // Écran et veille : depuis le dernier appui, et depuis la dernière lecture vue (lib/core/src/appstate.h)
    auto since = [now](uint32_t t) { return (int32_t)(now - t) > 0 ? now - t : 0u; };
    wr::IdleInput idle;
    idle.touchIdleMs = since(g_idle.lastTouch);
    idle.notPlayingMs = since(g_idle.lastPlayingAt);
    idle.online = g_state.online;
    idle.playing = p.playing;
    idle.modal = modalOpen();
    idle.ambient = g_view.ambient;
    idle.fullBright = g_idle.brightness == kBrightOn;
    idle.dark = g_idle.brightness == 0;
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
            if (g_view.ambient) exitAmbient();  // micro coupé avec l'écran
            setBrightness(0);
            break;
        case wr::IdleAction::Sleep: goToSleep("automatique"); break;  // sans appui ni lecture depuis sleepMin minutes
    }
    renderNow();
    delay(5);
}
