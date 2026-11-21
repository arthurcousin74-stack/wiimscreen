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
LibraryView g_lib;
PressFlash g_flash;
MotionWake g_motion;
Redraw g_draw;
VolumeTouch g_volTouch;
PerfStats g_perf;
IdleClock g_idle;
StatusClock g_status;
HeaderPress g_header;
PendingCommand g_pending;
FocusState g_focus;

// Le WiiM charge l'album du morceau choisi (Deezer, albums) : plus long qu'une piste de la file
static bool loadsAlbum(SearchKind k) {
    return k == SearchKind::Deezer || k == SearchKind::DeezerFavorites || k == SearchKind::QobuzAlbums;
}

static constexpr int kSwipePx = 50;

static constexpr uint32_t kLongPressMs = 3000;
static constexpr uint32_t kTileHoldMs = 600;  // doigt tenu sur une tuile : son menu (épingler, retirer)
static constexpr uint32_t kWifiTimeoutMs = 20000;
static constexpr uint32_t kPresetRefreshMs = 10UL * 60 * 1000;
static constexpr uint32_t kPresetRetryMs = 30UL * 1000;
static constexpr uint32_t kFlashMs = 150;
// Volume au doigt : commande au plus toutes les 150 ms pendant un glissé ; −/+ maintenu répète après 400 ms
static constexpr uint32_t kVolumeSendMs = 150;
static constexpr uint32_t kVolumeHoldMs = 400, kVolumeRepeatMs = 150;
// Réactivité : une ligne de Journal tous les 20 appuis
static constexpr uint32_t kPerfEvery = 20;

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
    g_draw.transport = true;  // anneau d'attente : la rangée lecture seule (les autres chemins appellent show())
}

// Affichage différé : show() marque l'écran à redessiner, renderNow() le dessine une
// seule fois par tour de boucle, même si plusieurs changements sont arrivés.
void show() { g_draw.dirty = true; }

// Écran autre que les commandes. La dictée (g_voice.listening) n'a lieu que sur Search ou Results : elle
// commence sur le clavier, et seule la fin d'une recherche peut passer de l'un à l'autre pendant l'écoute.
static bool modalOpen() { return g_view.screen != Screen::Controls; }

// Court effet « bouton enfoncé » encore actif (différence signée : juste après 49 jours aussi)
static bool flashActive(uint32_t now) { return g_flash.until && (int32_t)(g_flash.until - now) > 0; }

// Bouton enfoncé : tuile sous le doigt, −/+ maintenu, ou court effet après un appui
static void pressedNow(uint32_t now, int& preset, int& transport) {
    preset = (g_strip.touch && !g_strip.dragged) ? g_strip.preset : -1;
    transport = g_volTouch.hold;
    if (flashActive(now)) {
        if (preset < 0) preset = g_flash.preset;
        if (transport < 0) transport = g_flash.transport;
    }
}

// Réactivité : durées des images (complètes, partielles)
static void notePerf(bool full, uint32_t ms) {
    if (full) {
        ++g_perf.fulls;
        g_perf.fullSum += ms;
        g_perf.fullMax = std::max(g_perf.fullMax, ms);
    } else {
        ++g_perf.parts;
        g_perf.partSum += ms;
    }
}

// Réactivité : appui → première image envoyée (complète ou partielle) ; une ligne de Journal tous les kPerfEvery
static void servicePerf(bool pressed, uint32_t now) {
    if (g_perf.tapAt) {
        if (g_ui.presentCount() != g_perf.presentsAtTap) {
            const uint32_t ms = g_ui.lastPresentMs() - g_perf.tapAt;
            g_perf.tapAt = 0;
            ++g_perf.taps;
            g_perf.tapSum += ms;
            g_perf.tapMax = std::max(g_perf.tapMax, ms);
            if (g_perf.taps >= kPerfEvery) {
                const uint32_t tapAvg = g_perf.tapSum / g_perf.taps;
                const uint32_t fullAvg = g_perf.fulls ? g_perf.fullSum / g_perf.fulls : 0;
                const uint32_t partAvg = g_perf.parts ? g_perf.partSum / g_perf.parts : 0;
                logTagged("perf",
                          "taps=%lu tap_avg=%lu tap_max=%lu full=%lu full_avg=%lu full_max=%lu part=%lu part_avg=%lu | "
                          "R\xC3\xA9" "activit\xC3\xA9 : image %lu ms apr\xC3\xA8s l'appui (max %lu), image compl\xC3\xA8te "
                          "%lu ms (max %lu), partielle %lu ms",
                          (unsigned long)g_perf.taps, (unsigned long)tapAvg, (unsigned long)g_perf.tapMax,
                          (unsigned long)g_perf.fulls, (unsigned long)fullAvg, (unsigned long)g_perf.fullMax,
                          (unsigned long)g_perf.parts, (unsigned long)partAvg, (unsigned long)tapAvg,
                          (unsigned long)g_perf.tapMax, (unsigned long)fullAvg, (unsigned long)g_perf.fullMax,
                          (unsigned long)partAvg);
                g_perf = PerfStats();
            }
        } else if (wr::expired(now, g_perf.tapAt, 1000)) {
            g_perf.tapAt = 0;  // appui qui n'a rien redessiné (hors bouton, écran rallumé…) : pas compté
        }
    }
    if (pressed) {
        g_perf.tapAt = now ? now : 1;
        g_perf.presentsAtTap = g_ui.presentCount();
    }
}

static void renderNow();

// Zones seules demandées (g_draw.volume, .transport, .tile) et aucune image complète prévue : chacune est redessinée
// à part. Hors vue commandes, ou si une zone ne s'y prête pas : image complète.
static void renderParts() {
    if (!g_draw.volume && !g_draw.transport && g_draw.tile < 0) return;
    const uint32_t now = millis();
    int pressedPreset, pressedTransport;
    pressedNow(now, pressedPreset, pressedTransport);
    const UiState st = displayState();
    bool ok = !g_view.ambient && g_ui.controlsShown();
    if (ok && g_draw.volume) ok = g_ui.updateVolume(st, pressedTransport);
    if (ok && g_draw.transport) {
        g_ui.setFavorite(favoriteState());
        ok = g_ui.updateTransport(st, pressedTransport);
    }
    if (ok && g_draw.tile >= 0) ok = g_ui.updateTile(g_draw.tile, g_draw.tile == pressedPreset);
    g_draw.volume = g_draw.transport = false;
    g_draw.tile = -1;
    if (!ok) {
        show();
        renderNow();
        return;
    }
    notePerf(false, millis() - now);
}

static void renderNow() {
    if (modalOpen()) return;  // écrans modaux : rien par-dessus (rendu à leur fermeture)
    if (!g_draw.dirty) {
        renderParts();
        return;
    }
    g_draw.dirty = false;
    g_draw.volume = g_draw.transport = false;  // compris dans l'image complète
    g_draw.tile = -1;
    const uint32_t now = millis();
    int pressedPreset, pressedTransport;
    pressedNow(now, pressedPreset, pressedTransport);
    g_draw.lastTick = now;
    g_draw.progressDue = false;  // la progression fait partie de l'image complète
    g_ui.setFavorite(favoriteState());
    if (g_view.ambient) g_ui.renderAmbient(displayState());
    else g_ui.render(displayState(), pressedPreset, pressedTransport);
    notePerf(true, millis() - now);
}

// Une tuile seule à redessiner ; deux tuiles différentes dans le même tour : image complète
static void showTile(int index) {
    if (index < 0) return;
    if (g_draw.tile >= 0 && g_draw.tile != index) show();
    else g_draw.tile = index;
}

// Bouton −/+ dans la pilule du volume, les autres dans la rangée lecture
static void showTransport(int which) {
    if (which == wr::kVolDown || which == wr::kVolUp) g_draw.volume = true;
    else if (which >= 0) g_draw.transport = true;
}

// Bouton enfoncé pendant un court instant, puis relâché par la boucle
static void flash(int preset, int transport) {
    g_flash.preset = preset;
    g_flash.transport = transport;
    g_flash.until = millis() + kFlashMs;
    if (preset >= 0) show();  // preset lancé : tuile active, anneau, message (image complète)
    showTransport(transport);
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
        if (g_volTouch.drag || g_volTouch.hold >= 0) {  // doigt sur le volume : le niveau affiché reste le sien
            next.player.volume = g_state.player.volume;
            next.player.muted = g_state.player.muted;
        }
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
static bool isArrow(wr::NavKey k);
static wr::Dir dirOf(wr::NavKey k);

static void chooseWiim() {
    g_focus.picking = true;  // anneau (touches) : g_focus.picker
    g_focus.picker = wr::FocusSpot{};
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
            const wr::Target* tg = t.wasPressed() ? g_ui.targets().hit(t.x, t.y) : nullptr;
            if (t.wasPressed() && g_focus.visible) {  // appui : l'anneau disparaît
                g_focus.visible = false;
                g_ui.renderPicker(list);
                tg = g_ui.targets().hit(t.x, t.y);
            }
            // Touches : flèches entre les lecteurs et les boutons, OK comme un appui, Retour = Réessayer
            wr::Target chosen;
            wr::KeyEvent k;
            while (!tg && takeKey(k)) {
                if (k.key == wr::NavKey::Back) {
                    chosen.act = wr::Act::PickRetry;
                    tg = &chosen;
                } else if (isArrow(k.key) || k.key == wr::NavKey::Ok) {
                    if (!g_focus.visible) {  // première touche : montrer l'anneau
                        g_focus.visible = true;
                        g_ui.renderPicker(list);
                        continue;
                    }
                    const wr::Target* cur = wr::focusedTarget(g_ui.targets(), g_focus.picker);
                    if (!cur) continue;
                    if (k.key == wr::NavKey::Ok) {
                        chosen = *cur;
                        tg = &chosen;
                        continue;
                    }
                    const wr::NavResult r = wr::railMove(g_ui.targets(), cur, dirOf(k.key));
                    if (!r.target) continue;
                    g_focus.picker = wr::FocusSpot{r.target->act, r.target->arg};
                    g_ui.renderPicker(list);
                }
            }
            if (tg && tg->act == wr::Act::PickDevice && tg->arg >= 0 && tg->arg < (int)found.size()) {
                sound::tap();
                useWiim(found[tg->arg]);
                g_focus.picking = false;
                return;
            }
            if (tg && tg->act == wr::Act::PickRetry) sound::tap(), retry = true;
            if (tg && tg->act == wr::Act::PickSetup)
                restartIntoPortal(wr::L("Enter the IP address of the WiiM.", "Saisis l'adresse IP du WiiM."));
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

// Élément visé par un appui : le premier de la liste de l'écran (Ui::targets()) qui contient le point. La zone de
// la progression suit aussi updateProgress() : hitProgress() la confirme (sinon on passe aux éléments suivants).
static const wr::Target* tapTarget(int x, int y) {
    uint32_t pos;
    for (const wr::Target& t : g_ui.targets()) {
        if (!t.hit.contains(x, y)) continue;
        if (t.act == wr::Act::Progress && !g_ui.hitProgress(x, y, pos)) continue;
        return &t;
    }
    return nullptr;
}

// Activation d'un élément : par un appui (au point touché) ou par la touche OK (au centre de l'élément). Un appui
// et OK sur le même élément font la même chose ; seuls la bande des presets (suivre le doigt) et l'en-tête (appui
// long) dépendent du doigt.
struct Press {
    int x = 0, y = 0;
    bool touch = true;
    uint32_t now = 0;  // millis() du tour de boucle (l'appui long de l'en-tête se compte avec lui)
};

// ------------------------------------------------------------ menu d'une tuile

// Tuile du menu ouvert : retrouvée par sa clé (la liste a pu changer depuis l'ouverture) ; -1 si elle n'existe plus
static int tileMenuIndex() {
    for (size_t i = 0; i < g_tiles.list.size(); ++i)
        if (wr::tileKey(g_tiles.list[i]) == g_view.tileKey) return (int)i;
    return -1;
}

// Seuls les favoris enregistrés sur la télécommande se retirent ici ; les albums des comptes (Qobuz, Deezer) et les
// listes de favoris viennent des comptes, les presets du WiiM se changent dans l'app WiiM Home
static bool tileRemovable(const wr::Preset& p) {
    return !p.favId.empty() && !p.list && p.favId.rfind("qobuz-album:", 0) != 0 && p.favId.rfind("deezer-album:", 0) != 0;
}

static void renderTileMenu() {
    const int i = tileMenuIndex();
    const std::string& key = g_view.tileKey;
    g_ui.renderTileMenu(i, g_tiles.stats.pinned(key), i >= 0 && tileRemovable(g_tiles.list[i]));
}

static void openTileMenu(int index) {
    if (index < 0 || index >= (int)g_tiles.list.size() || wr::tileKey(g_tiles.list[index]).empty()) return;
    g_view.tileKey = wr::tileKey(g_tiles.list[index]);
    g_view.tileMenuBack = g_view.screen == Screen::Library ? Screen::Library : Screen::Controls;
    logEvent("Menu de la tuile : %s", g_tiles.list[index].label.c_str());
    g_view.screen = Screen::TileMenu;
    g_focus.spot[(int)Screen::TileMenu] = wr::FocusSpot{};
    renderTileMenu();
}

static void openMenu();

static void activateTileMenu(const wr::Target& tg, uint32_t now) {
    switch (tg.act) {
        case wr::Act::Search:
            sound::tap();
            openSearch();
            return;
        case wr::Act::Settings: openMenu(); return;
        case wr::Act::Library:
            sound::tap();
            openLibrary();
            return;
        case wr::Act::Home: g_view.tileMenuBack = Screen::Controls; break;  // Accueil : fermer, vers les commandes
        case wr::Act::TileChoice: break;
        default: return;
    }
    sound::tap();
    const int i = tileMenuIndex();
    if (tg.act == wr::Act::TileChoice && i >= 0) {
        const wr::Preset p = g_tiles.list[i];  // copie : la liste change ci-dessous
        if (tg.arg == Ui::TilePin) {
            const bool on = !g_tiles.stats.pinned(g_view.tileKey);
            pinTile(i, on);
            setMessage(String(on ? wr::L("Pinned: ", "\xC3\x89pingl\xC3\xA9" "e : ") : wr::L("Unpinned: ", "D\xC3\xA9s\xC3\xA9pingl\xC3\xA9" "e : ")) +
                           p.label.c_str(),
                       4000);
        } else if (tg.arg == Ui::TileRemove && tileRemovable(p)) {
            if (g_tiles.stats.pinned(g_view.tileKey)) pinTile(i, false);  // plus de place réservée en tête
            const bool ok = fav::remove(p.favId);
            logEvent("Favori retir\xC3\xA9 depuis sa tuile (%s) : %s", p.favId.c_str(), ok ? "OK" : "erreur");
            refreshTiles();
            setMessage(ok ? wr::L("Removed from favorites", "Retir\xC3\xA9 des favoris")
                          : wr::L("Favorite not removed", "Favori non retir\xC3\xA9"),
                       4000);
        }
    }
    g_view.tileKey.clear();
    g_idle.lastTouch = now;
    if (g_view.tileMenuBack == Screen::Library) {  // ouvert depuis la Bibliothèque : on y revient (même filtre, page)
        g_view.screen = Screen::Library;
        renderLibrary();
        return;
    }
    g_view.screen = Screen::Controls;
    show();
}

static void playPreset(int p) {
    if (p < 0 || p >= (int)g_tiles.list.size()) return;
    logEvent("Preset %d : %s", g_tiles.list[p].slot, g_tiles.list[p].label.c_str());
    sound::tap();
    noteTilePlayed(p);  // historique : l'ordre des tuiles change au prochain repos
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
static void serviceStrip(const m5::touch_detail_t& t, uint32_t now) {
    if (!g_strip.touch) return;
    if (t.isPressed()) {
        if (!g_strip.dragged && abs(t.x - g_strip.startX) > kSwipePx) {
            g_strip.dragged = true;  // c'est un glissement : la tuile n'est plus enfoncée
            showTile(g_strip.preset);
        } else if (!g_strip.dragged && g_strip.preset >= 0 && wr::expired(now, g_strip.since, kTileHoldMs)) {
            g_strip.touch = false;  // appui long : le menu de la tuile ; le doigt relevé ne lance rien
            sound::confirm();
            openTileMenu(g_strip.preset);
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

// Bibliothèque : barre, filtres, pages ; une tuile se joue au relâcher du doigt (appui long : son menu,
// serviceLibraryTouch), tout de suite avec OK. Après le lancement : retour aux commandes (ce qui joue)
static void activateLibrary(const wr::Target& tg, const Press& at) {
    switch (tg.act) {
        case wr::Act::Home:
            sound::tap();
            g_view.screen = Screen::Controls;
            show();
            return;
        case wr::Act::Search:
            sound::tap();
            openSearch();
            return;
        case wr::Act::Settings: openMenu(); return;
        case wr::Act::Library:  // déjà ici : tous les filtres, première page
            sound::tap();
            g_lib.filter = -1;
            g_lib.page = 0;
            renderLibrary();
            return;
        case wr::Act::LibFilter:
            sound::tap();
            g_lib.filter = tg.arg;
            g_lib.page = 0;
            renderLibrary();
            return;
        case wr::Act::LibPage:
            sound::tap();
            g_lib.page += tg.arg;
            renderLibrary();
            return;
        case wr::Act::LibTile:
            if (tg.arg < 0 || tg.arg >= (int)g_tiles.list.size()) return;
            if (at.touch) {  // doigt suivi : relâché = jouer, tenu = menu
                g_lib.touch = tg.arg;
                g_lib.since = at.now;
                renderLibrary(tg.arg);  // tuile enfoncée
                return;
            }
            g_view.screen = Screen::Controls;
            playPreset(tg.arg);
            show();
            return;
        default: return;
    }
}

static void serviceLibraryTouch(const m5::touch_detail_t& t, uint32_t now) {
    if (g_lib.touch < 0 || g_view.screen != Screen::Library) return;
    const int i = g_lib.touch;
    if (t.isPressed()) {
        if (wr::expired(now, g_lib.since, kTileHoldMs)) {
            g_lib.touch = -1;  // appui long : menu ; le doigt relevé ne lance rien
            sound::confirm();
            openTileMenu(i);
        }
        return;
    }
    g_lib.touch = -1;
    // Relâché sur la tuile : elle joue ; ailleurs (doigt glissé) : rien
    const wr::Target* on = g_ui.targets().hit(t.x, t.y);
    if (on && on->act == wr::Act::LibTile && on->arg == i) {
        g_view.screen = Screen::Controls;
        playPreset(i);
        show();
    } else {
        renderLibrary();
    }
}

// −/+ : un pas de volume (réglage volStep), affiché tout de suite, commande en fond
static void stepVolume(int which) {
    auto& p = g_state.player;
    const int v = constrain(p.volume + (which == wr::kVolUp ? g_settings.volStep : -g_settings.volStep), 0, 100);
    p.volume = v;
    p.muted = false;
    g_worker.command("setPlayerCmd:vol:" + String(v));
}

// Doigt sur le volume, suivi après l'appui : curseur glissé, ou −/+ maintenu
static void serviceVolumeTouch(const m5::touch_detail_t& t, uint32_t now) {
    auto& v = g_volTouch;
    if (!v.drag && v.hold < 0) return;
    auto& p = g_state.player;
    if (modalOpen() || g_view.ambient) {  // autre écran : plus rien à suivre
        v.drag = false;
        v.hold = -1;
        return;
    }
    if (v.drag) {
        if (t.isPressed()) {
            const int vol = g_ui.volumeAt(t.x);
            if (vol != p.volume || p.muted) {
                p.volume = vol;
                p.muted = false;
                v.moved = true;
                g_draw.volume = true;
            }
            if (vol != v.sent && wr::expired(now, v.sentAt, kVolumeSendMs)) {
                g_worker.command("setPlayerCmd:vol:" + String(vol));  // le worker ne garde que le dernier
                v.sent = vol;
                v.sentAt = now;
            }
        } else {
            v.drag = false;
            if (p.volume != v.sent) g_worker.command("setPlayerCmd:vol:" + String(p.volume));
            if (v.moved) logEvent("Volume gliss\xC3\xA9 : %d", p.volume);
        }
        return;
    }
    if (!t.isPressed()) {
        if (v.steps) logEvent("Volume maintenu : %d", p.volume);
        v.hold = -1;
        g_draw.volume = true;  // bouton relâché
        return;
    }
    if ((int32_t)(now - v.nextStep) < 0) return;
    v.nextStep = now + kVolumeRepeatMs;
    const int before = p.volume;
    stepVolume(v.hold);
    if (p.volume != before) {
        sound::tap();
        ++v.steps;
        g_draw.volume = true;
    }
}

// Vue commandes : un élément de Ui::targets() activé (appui ou OK)
static void activateControls(const wr::Target& tg, const Press& at) {
    const int x = at.x, y = at.y;
    const char* how = at.touch ? "Appui" : "Touche";  // Journal
    auto& p = g_state.player;
    switch (tg.act) {
        // Barres (volume, coupure du son, progression) : affichage immédiat, commande en fond
        case wr::Act::Volume: {
            int vol;
            if (!g_ui.hitVolume(x, y, vol)) return;
            logEvent("Appui volume %d (%d,%d)", vol, x, y);
            sound::tap();
            p.volume = vol;
            p.muted = false;
            g_worker.command("setPlayerCmd:vol:" + String(vol));
            g_draw.volume = true;
            if (at.touch) {  // le doigt peut ensuite glisser sur le curseur (serviceVolumeTouch)
                g_volTouch.drag = true;
                g_volTouch.moved = false;
                g_volTouch.sent = vol;
                g_volTouch.sentAt = at.now;
            }
            return;
        }
        case wr::Act::Mute:
            if (!wiimStateKnown("son coup\xC3\xA9/r\xC3\xA9tabli")) return;
            logEvent("%s son coup\xC3\xA9/r\xC3\xA9tabli (%d,%d)", how, x, y);
            sound::tap();
            p.muted = !p.muted;
            g_worker.command(String("setPlayerCmd:mute:") + (p.muted ? "1" : "0"));
            g_draw.volume = true;
            return;
        case wr::Act::Progress: {
            uint32_t pos;
            if (!g_ui.hitProgress(x, y, pos)) return;
            logEvent("Appui progression %lu s (%d,%d)", (unsigned long)(pos / 1000), x, y);
            sound::tap();
            p.positionMs = pos;
            g_status.at = millis();
            g_worker.command("setPlayerCmd:seek:" + String(pos / 1000));  // secondes
            if (playingNow()) g_draw.progressDue = true;  // la ligne de progression seule (loop())
            else show();
            return;
        }
        case wr::Act::Header:  // appui long : Réglages (loop() suit le doigt)
            if (!at.touch) return;
            g_header.pressed = true;
            g_header.since = at.now;
            return;
        case wr::Act::Dots:  // moitié gauche : page précédente, moitié droite : suivante
            g_ui.setPage(g_ui.page() + tg.arg);
            sound::tap();
            show();
            return;
        case wr::Act::Preset:
        case wr::Act::Strip:
            if (!at.touch) {  // OK : la tuile tout de suite (pas de doigt à suivre)
                if (tg.act == wr::Act::Preset) playPreset(tg.arg);
                return;
            }
            g_strip.touch = true;
            g_strip.dragged = false;
            g_strip.startX = x;
            g_strip.since = at.now;
            g_strip.preset = tg.act == wr::Act::Preset ? tg.arg : -1;
            showTile(g_strip.preset);  // tuile montrée enfoncée tant que le doigt reste (elle seule)
            return;
        case wr::Act::Fav: {  // ♥ : ajouter ce qui joue aux favoris, ou l'en retirer
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
                logEvent("%s Favori : %s", how, album.c_str());
                g_jobs.favorite = true;
                g_jobs.since = millis();
                g_worker.saveFavorite(st.player.title.c_str(), st.player.artist.c_str(), album.c_str(), st.meta.artUrl.c_str());
                setMessage(wr::L("Adding to favorites\xE2\x80\xA6", "Ajout aux favoris\xE2\x80\xA6"), 15000);
            }
            show();
            return;
        }
        case wr::Act::Home:  // Accueil (barre) : déjà ici, retour à la première page de presets
            logEvent("%s Accueil (%d,%d)", how, x, y);
            sound::tap();
            g_ui.setPage(0);
            show();
            return;
        case wr::Act::Search:  // Recherche (barre)
            logEvent("%s Recherche (%d,%d)", how, x, y);
            sound::tap();
            openSearch();
            return;
        case wr::Act::Library:  // Bibliothèque (barre)
            logEvent("%s Biblioth\xC3\xA8que (%d,%d)", how, x, y);
            sound::tap();
            openLibrary();
            return;
        case wr::Act::Settings:  // Réglages (barre)
            logEvent("%s R\xC3\xA9glages (%d,%d)", how, x, y);
            sound::tap();
            g_view.screen = Screen::Menu;
            g_ui.renderMenu(FW_VERSION);
            return;
        case wr::Act::Transport: break;  // ci-dessous
        default: return;
    }

    const int t = tg.arg;
    if (t < 0 || t >= wr::kTransportCount) return;
    static const char* kNames[] = {"volume -", "pr\xC3\xA9" "c\xC3\xA9" "dent", "lecture/pause", "suivant", "volume +"};
    if ((t == wr::kVolDown || t == wr::kVolUp || t == wr::kPlayPause) && !wiimStateKnown(kNames[t])) return;
    logEvent("%s %s (%d,%d)", how, kNames[t], x, y);
    sound::tap();
    switch (t) {
        case wr::kVolDown:
        case wr::kVolUp:
            stepVolume(t);
            if (at.touch) {  // doigt maintenu : répétition (serviceVolumeTouch)
                g_volTouch.hold = t;
                g_volTouch.nextStep = at.now + kVolumeHoldMs;
                g_volTouch.steps = 0;
            }
            break;
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

// Appui sur la vue commandes. La liste des éléments doit être la sienne : juste après la fermeture d'un autre écran
// (Journal, test tactile, recherche : leur tour de boucle se termine avant renderNow()) ou au premier tour après
// setup(), l'écran affiché est encore l'ancien ; la vue commandes est alors dessinée d'abord (show() est déjà
// demandé dans ces cas).
static void tapControls(int x, int y, uint32_t now) {
    if (!g_ui.controlsShown()) renderNow();
    const wr::Target* tg = tapTarget(x, y);
    if (!tg) {
        logEvent("Appui hors bouton (%d,%d)", x, y);
        return;
    }
    Press at;
    at.x = x, at.y = y, at.now = now;
    activateControls(*tg, at);
}

// Menu Réglages : Recherche, Accueil (fermer) ou un des choix
static void activateMenu(const wr::Target& tg, uint32_t now) {
    switch (tg.act) {
        case wr::Act::Search:
            sound::tap();
            openSearch();
            return;
        case wr::Act::Home:
            sound::tap();  // Accueil : fermer les réglages
            g_view.screen = Screen::Controls;
            show();
            return;
        case wr::Act::Library:
            sound::tap();
            openLibrary();
            return;
        case wr::Act::MenuChoice: break;  // ci-dessous
        default: return;                  // Réglages : déjà ici
    }
    sound::tap();
    switch (tg.arg) {
        case Ui::MenuConfig:
            restartIntoPortal(wr::L("Setup requested.", "Reconfiguration demand\xC3\xA9" "e."));  // redémarre
            break;
        case Ui::MenuUpdate:
            runUpdateMode(g_ui);  // redémarre si une mise à jour est installée
            g_view.screen = Screen::Controls;
            show();
            break;
        case Ui::MenuLog:
            g_view.screen = Screen::Log;
            g_view.logFrame = now;
            g_ui.renderLog(logLines(), FW_VERSION);
            break;
        case Ui::MenuTouch:
            g_view.screen = Screen::TouchTest;
            g_view.touchCount = 0;
            g_ui.renderTouchTest(-1, -1, 0);
            break;
        case Ui::MenuSleep:
            g_view.screen = Screen::Controls;
            goToSleep("demand\xC3\xA9" "e depuis R\xC3\xA9glages");
            break;
        case Ui::MenuClose:
            g_view.screen = Screen::Controls;
            show();
            break;
    }
}

// Journal : Fermer ou Accueil, File du WiiM, Recherche, Réglages. false : rien d'activé (le Journal se rafraîchit)
static bool activateLog(const wr::Target& tg, uint32_t now) {
    switch (tg.act) {
        case wr::Act::LogClose:
        case wr::Act::Home:
            sound::tap();
            g_view.screen = Screen::Controls;
            g_idle.lastTouch = now;
            show();
            return true;
        case wr::Act::LogQueue:
            sound::tap();
            logEvent("Appui File du WiiM");
            g_worker.checkQueue();  // résultat dans le journal (tâche de fond)
            g_view.logFrame = 0;
            return true;
        case wr::Act::Search:
            sound::tap();
            g_idle.lastTouch = now;
            openSearch();
            return true;
        case wr::Act::Settings:
            sound::tap();
            g_view.screen = Screen::Menu;
            g_idle.lastTouch = now;
            g_ui.renderMenu(FW_VERSION);
            return true;
        case wr::Act::Library:
            sound::tap();
            g_idle.lastTouch = now;
            openLibrary();
            return true;
        default: return false;
    }
}

// Test tactile : Fermer. true : l'écran est fermé
static bool activateTouchTest(const wr::Target& tg, uint32_t now) {
    if (tg.act != wr::Act::TouchClose) return false;
    sound::tap();
    g_view.screen = Screen::Controls;
    g_idle.lastTouch = now;
    show();
    return true;
}

// Recherche : clavier (micro, sources, touches) et liste des résultats (lignes, cœurs, pages, Modifier) ; la barre
// de gauche reste active. Pas pendant la dictée (loop() : le micro y termine l'écoute).
static void activateSearch(const wr::Target& tg) {
    const bool keyboard = g_view.screen == Screen::Search;
    switch (tg.act) {
        case wr::Act::Mic:
            if (!g_jobs.search && !g_voice.stt.busy) startVoice();  // bip, puis écoute jusqu'au bouton rouge
            return;
        case wr::Act::Home:
            sound::tap();
            g_view.screen = Screen::Controls;
            show();
            return;
        case wr::Act::Settings:
            sound::tap();
            g_view.screen = Screen::Menu;
            g_ui.renderMenu(FW_VERSION);
            return;
        case wr::Act::Library:
            sound::tap();
            openLibrary();
            return;
        case wr::Act::Source:
            sound::tap();
            g_search.source = tg.arg;
            g_search.status = "";
            updateSearchField();
            return;
        case wr::Act::Key: onSearchKey(tg.arg); return;
        case wr::Act::ResultHeart: toggleAlbumFavorite(tg.arg); return;
        case wr::Act::ResultRow: {
            const int r = tg.arg;
            if (r < 0 || r >= (int)g_search.results.size()) return;
            sound::tap();
            renderResultsList(r);  // ligne enfoncée
            logTagged("search.play", "row=%d | Recherche : lecture de %d (%s)", r, r + 1, g_search.results[r].title.c_str());
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
            return;
        }
        case wr::Act::ResultsPage:
            sound::tap();
            g_search.page += tg.arg;
            renderResultsList();
            return;
        case wr::Act::ResultsEdit:
        case wr::Act::Search:  // Recherche (barre) : depuis la liste, retour au clavier ; sur le clavier, rien
            if (keyboard) return;
            sound::tap();
            openSearch();
            return;
        default: return;
    }
}

// --------------------------------------------------------- touches (télécommande, clavier, manette)
//
// Lues dans loop() seulement (serviceKeys). Le Tab5 n'en reçoit aucune : rien ne change pour lui.
// Flèches : focus d'un élément à l'autre (wr::railMove) ; OK : la même activation qu'un appui ; Retour : l'écran
// d'avant ; touches de lecture et de volume : comme les boutons ; Menu : Réglages ; Recherche : le clavier.

// Anneau du focus (Ui::setFocusSource) : montré après une touche, sur l'élément retenu pour l'écran affiché
static bool focusSource(wr::FocusSpot& spot) {
    if (!g_focus.visible) return false;
    if (g_focus.picking) {
        spot = g_focus.picker;
        return true;
    }
    spot = g_focus.spot[(int)g_view.screen];
    return true;
}

// Écran affiché redessiné (anneau montré, déplacé ou caché)
static void redrawForFocus() {
    switch (g_view.screen) {
        case Screen::Controls: show(); break;
        case Screen::Menu: g_ui.renderMenu(FW_VERSION); break;
        case Screen::Log:
            g_view.logFrame = millis();
            g_ui.renderLog(logLines(), FW_VERSION);
            break;
        case Screen::TouchTest: g_ui.renderTouchTest(-1, -1, g_view.touchCount); break;
        case Screen::Search: renderSearch(g_search.keyDown); break;  // la touche enfoncée le reste
        case Screen::Results: renderResultsList(-1, g_search.results.empty() ? g_search.note : String()); break;
        case Screen::TileMenu: renderTileMenu(); break;
        case Screen::Library: renderLibrary(); break;
        case Screen::Count: break;
    }
}

// Premier appui sur l'écran : l'anneau disparaît (« focus-visible »)
static void hideFocus() {
    if (!g_focus.visible) return;
    g_focus.visible = false;
    redrawForFocus();
}

static wr::Dir dirOf(wr::NavKey k) {
    switch (k) {
        case wr::NavKey::Up: return wr::Dir::Up;
        case wr::NavKey::Down: return wr::Dir::Down;
        case wr::NavKey::Left: return wr::Dir::Left;
        default: return wr::Dir::Right;
    }
}

static bool isArrow(wr::NavKey k) {
    return k == wr::NavKey::Up || k == wr::NavKey::Down || k == wr::NavKey::Left || k == wr::NavKey::Right;
}

// Activation au clavier : centre de l'élément, sans doigt à suivre
static Press keyPress(const wr::Target& t, uint32_t now) {
    Press at;
    at.x = t.r.x + t.r.w / 2, at.y = t.r.y + t.r.h / 2, at.touch = false, at.now = now;
    return at;
}

// Touches de lecture, de volume et de coupure du son : les boutons de la vue commandes (même chemin non bloquant)
static void mediaKey(wr::NavKey k, uint32_t now) {
    wr::Target t;
    t.act = wr::Act::Transport;
    switch (k) {
        case wr::NavKey::PlayPause: t.arg = wr::kPlayPause; break;
        case wr::NavKey::Play:
            if (g_state.player.ok && g_state.player.playing) return;  // joue déjà
            t.arg = wr::kPlayPause;
            break;
        case wr::NavKey::Pause:
            if (g_state.player.ok && !g_state.player.playing) return;  // déjà en pause
            t.arg = wr::kPlayPause;
            break;
        case wr::NavKey::Next: t.arg = wr::kNext; break;
        case wr::NavKey::Prev: t.arg = wr::kPrev; break;
        case wr::NavKey::VolUp: t.arg = wr::kVolUp; break;
        case wr::NavKey::VolDown: t.arg = wr::kVolDown; break;
        case wr::NavKey::Mute: t.act = wr::Act::Mute; break;
        default: return;
    }
    Press at;
    at.x = at.y = -1, at.touch = false, at.now = now;
    activateControls(t, at);
}

static void openMenu() {
    sound::tap();
    g_view.screen = Screen::Menu;
    g_ui.renderMenu(FW_VERSION);
}

// Retour : fermer Réglages, Journal, test tactile ; la liste revient au clavier, le clavier aux commandes ; pendant
// la dictée, l'annuler. Sur les commandes : rien (la vue ambiance vient seule après 60 s).
static void backKey(uint32_t now) {
    switch (g_view.screen) {
        case Screen::Controls: return;
        case Screen::Search:
        case Screen::Results:
            if (g_voice.listening && g_voice.dictation) {
                finishDictation(false);
                return;
            }
            sound::tap();
            if (g_view.screen == Screen::Results) {
                openSearch();
                return;
            }
            break;
        case Screen::TileMenu:
            sound::tap();
            if (g_view.tileMenuBack == Screen::Library) {
                g_view.screen = Screen::Library;
                g_idle.lastTouch = now;
                renderLibrary();
                return;
            }
            break;
        default: sound::tap(); break;
    }
    g_view.screen = Screen::Controls;
    g_idle.lastTouch = now;
    show();
}

// Flèche : focus déplacé. Sur la bande des presets, Gauche / Droite sans autre tuile de ce côté changent de page
// (comme un glissement) et gardent le focus sur une tuile : la première de la page suivante, la dernière de la
// précédente. Voulu : sur une page après la première, Gauche depuis la première tuile revient d'une page au lieu
// d'aller à la barre (sinon les pages précédentes seraient inatteignables au D-pad : les points ne prennent pas le
// focus) ; la barre reste à Gauche depuis la première page, ou par Haut puis Gauche. Au bord de l'écran : rien.
//
// Liste des résultats : Bas depuis la dernière ligne d'une page (ou son cœur) montre la page suivante, focus sur sa
// première ligne ; Haut depuis la première ligne, la page précédente, focus sur sa dernière ligne (wr::pageEdgeMove).
// Sur la première page, Haut va à Modifier ; sur la dernière, Bas va à Précédents.
static void moveFocus(const wr::Target& cur, wr::Dir d) {
    wr::FocusSpot& spot = g_focus.spot[(int)g_view.screen];
    if (g_view.screen == Screen::Results && (cur.act == wr::Act::ResultRow || cur.act == wr::Act::ResultHeart)) {
        const int to = wr::pageEdgeMove(cur.arg, g_ui.resultsPerPage(), (int)g_search.results.size(), d);
        if (to >= 0) {
            sound::tap();
            g_search.page = to / g_ui.resultsPerPage();
            spot = wr::FocusSpot{wr::Act::ResultRow, to};
            renderResultsList();
            return;
        }
        // Ligne ↔ son cœur (albums) : le cœur est dans le rectangle de la ligne, la géométrie seule n'y mène pas
        const wr::Act other = cur.act == wr::Act::ResultRow ? wr::Act::ResultHeart : wr::Act::ResultRow;
        const wr::Dir toOther = cur.act == wr::Act::ResultRow ? wr::Dir::Right : wr::Dir::Left;
        if (d == toOther && g_ui.targets().find(other, cur.arg)) {
            spot = wr::FocusSpot{other, cur.arg};
            redrawForFocus();
            return;
        }
    }
    const wr::NavResult r = wr::railMove(g_ui.targets(), &cur, d);
    if (g_view.screen == Screen::Controls && cur.act == wr::Act::Preset && (d == wr::Dir::Left || d == wr::Dir::Right) &&
        !(r.target && r.target->act == wr::Act::Preset)) {
        const int before = g_ui.page();
        g_ui.setPage(before + (d == wr::Dir::Right ? 1 : -1));
        if (g_ui.page() != before) {
            sound::tap();
            const int first = g_ui.page() * g_ui.perPage();
            const int last = std::min((int)g_tiles.list.size(), first + g_ui.perPage()) - 1;
            spot.act = wr::Act::Preset;
            spot.arg = d == wr::Dir::Right ? first : last;
            show();
            return;
        }
    }
    if (!r.target) return;  // bord
    spot.act = r.target->act;
    spot.arg = r.target->arg;
    redrawForFocus();
}

// Clavier de l'ordinateur sur la recherche : texte tapé, Effacer, Entrée. Le texte suit les règles des touches de
// l'écran (wr::typeText) et seul le champ est redessiné. Taper déplace l'anneau sur la touche Chercher : Entrée (OK)
// lance alors la recherche (« taper, Entrée »). Sur la liste des résultats, taper ou effacer revient au clavier avec
// le texte modifié (chercher autre chose) ; Entrée y est OK. Pendant la dictée : ignoré.
static void typingKey(const wr::KeyEvent& k) {
    using wr::NavKey;
    if (k.key == NavKey::Enter) {  // touche Entrée distincte de OK (aucune plateforme ne l'envoie encore)
        if (g_view.screen == Screen::Search) runSearchQuery();
        return;
    }
    std::string q = k.key == NavKey::Text ? wr::typeText(g_search.query, k.text) : wr::utf8PopBack(g_search.query);
    // Anneau sur la touche Chercher (montré : une touche le montre, seul un appui le cache)
    // (indice de la touche dans wr::keyboardLayout : le même à toutes les tailles, aussi avant le premier rendu)
    static int enterKey = -1;
    if (enterKey < 0) {
        const std::vector<wr::Key> keys = wr::keyboardLayout(wr::Rect{0, 0, 1000, 500});
        for (size_t i = 0; i < keys.size(); ++i)
            if (keys[i].kind == wr::Key::Enter) enterKey = (int)i;
    }
    wr::FocusSpot& spot = g_focus.spot[(int)Screen::Search];
    const wr::FocusSpot want{wr::Act::Key, enterKey};
    const bool moved = !g_focus.visible || spot.act != want.act || spot.arg != want.arg;
    g_focus.visible = true;
    spot = want;
    if (g_view.screen == Screen::Results) {
        g_search.query = q;
        openSearch();  // rendu complet, anneau compris
        return;
    }
    if (moved) {
        g_search.query = q;
        if (!g_jobs.search) g_search.status = String();
        renderSearch(g_search.keyDown);
        return;
    }
    editQuery(q);  // champ seul
}

static void onKey(const wr::KeyEvent& k, uint32_t now) {
    using wr::NavKey;
    const Screen screen = g_view.screen;
    const bool search = screen == Screen::Search || screen == Screen::Results;
    // Texte, Effacer, Entrée : pour la recherche seulement
    const bool typing = k.key == NavKey::Text || k.key == NavKey::Backspace || k.key == NavKey::Enter;
    if (k.key == NavKey::None || (typing && !search)) return;
    g_idle.lastTouch = now;  // activité, comme un appui (atténuation, vue ambiance, veille)
    // Touche de lecture, de volume ou de coupure du son : agit aussi en réveillant (sur la vue ambiance, c'est
    // l'usage principal d'une télécommande) ; les autres touches ne font que réveiller, comme un premier appui.
    const bool media = !isArrow(k.key) && k.key != NavKey::Ok && k.key != NavKey::Back && k.key != NavKey::Menu &&
                       k.key != NavKey::Search && !search;
    // Écran atténué ou éteint, ou vue ambiance
    if (g_idle.brightness != kBrightOn) {
        logEvent("Touche : r\xC3\xA9veil de l'\xC3\xA9" "cran");
        setBrightness(kBrightOn);
        if (g_view.ambient) exitAmbient();
        if (media) mediaKey(k.key, now);
        return;
    }
    if (g_view.ambient && screen == Screen::Controls) {
        logEvent("Touche en vue ambiance");
        exitAmbient();
        if (media) mediaKey(k.key, now);
        else sound::tap();
        return;
    }
    const bool dictation = g_voice.listening && g_voice.dictation;
    if (k.key == NavKey::Back) return backKey(now);
    if (k.key == NavKey::Menu) {
        if (screen == Screen::Menu) return backKey(now);
        if (!dictation) openMenu();
        return;
    }
    if (k.key == NavKey::Search) {
        if (screen != Screen::Search && !dictation) {
            sound::tap();
            openSearch();
        }
        return;
    }
    if (typing) {
        if (!dictation) typingKey(k);
        return;
    }
    // Recherche : les touches de lecture et de volume ne font rien (Espace y est du texte, pas lecture/pause)
    if (!isArrow(k.key) && k.key != NavKey::Ok) {
        if (!search) mediaKey(k.key, now);
        return;
    }
    // Pendant la dictée : OK la termine (comme le bouton rouge), les flèches ne font rien
    if (dictation) {
        if (k.key == NavKey::Ok && screen == Screen::Search) {
            sound::tap();
            finishDictation(true);
        }
        return;
    }

    if (screen == Screen::Controls && !g_ui.controlsShown()) renderNow();  // liste de la vue commandes (cf. tapControls)
    // OK tenu sur une tuile de la Bibliothèque : son menu (comme le doigt tenu)
    if (k.key == NavKey::Ok && k.kind == wr::KeyEvent::LongPress && screen == Screen::Library && g_focus.visible) {
        const wr::Target* on = wr::focusedTarget(g_ui.targets(), g_focus.spot[(int)screen]);
        if (on && on->act == wr::Act::LibTile) {
            sound::confirm();
            openTileMenu(on->arg);
        }
        return;
    }
    // OK tenu sur les commandes : Réglages, comme l'appui long sur l'en-tête
    if (k.key == NavKey::Ok && k.kind == wr::KeyEvent::LongPress && screen == Screen::Controls) {
        // … sauf sur une tuile : son menu, comme l'appui long du doigt
        const wr::Target* on = g_focus.visible ? wr::focusedTarget(g_ui.targets(), g_focus.spot[(int)screen]) : nullptr;
        if (on && on->act == wr::Act::Preset) {
            sound::confirm();
            openTileMenu(on->arg);
            return;
        }
        sound::confirm();
        g_view.screen = Screen::Menu;
        g_ui.renderMenu(FW_VERSION);
        return;
    }
    // Anneau caché : la première flèche ou OK le montre seulement (rien ne bouge, rien ne s'active)
    if (!g_focus.visible) {
        g_focus.visible = true;
        redrawForFocus();
        return;
    }
    const wr::Target* cur = wr::focusedTarget(g_ui.targets(), g_focus.spot[(int)screen]);
    if (!cur) return;
    if (isArrow(k.key)) return moveFocus(*cur, dirOf(k.key));
    // OK : la même activation qu'un appui (copie : l'activation peut redessiner l'écran et vider la liste)
    const wr::Target tg = *cur;
    g_focus.spot[(int)screen] = wr::FocusSpot{tg.act, tg.arg};
    static const char* const kScreenNames[kScreenCount] = {"controls", "menu", "touchtest", "log", "search", "results", "tilemenu", "library"};
    logTagged("focus.ok", "screen=%s act=%s arg=%d | Touche OK", kScreenNames[(int)screen], wr::actName(tg.act), tg.arg);
    switch (screen) {
        case Screen::Controls: activateControls(tg, keyPress(tg, now)); break;
        case Screen::Menu: activateMenu(tg, now); break;
        case Screen::TileMenu: activateTileMenu(tg, now); break;
        case Screen::Library: activateLibrary(tg, keyPress(tg, now)); break;
        case Screen::Log: activateLog(tg, now); break;
        case Screen::TouchTest: activateTouchTest(tg, now); break;
        case Screen::Search:
        case Screen::Results: activateSearch(tg); break;
        default: break;
    }
}

static void serviceKeys(uint32_t now) {
    wr::KeyEvent k;
    while (takeKey(k)) onKey(k, now);
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
    g_ui.setFocusSource(focusSource);              // anneau du focus (touches ; jamais sur le Tab5)

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
    loadTileStats();  // ordre des tuiles dès la première liste
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
    const uint32_t now = millis();
    // Touches (télécommande, clavier, manette ; rien sur le Tab5), puis appui : l'anneau du focus disparaît
    serviceKeys(now);
    servicePerf(t.wasPressed(), now);
    serviceTileOrder(now, t.isPressed() || t.wasPressed());  // jamais avec un doigt posé (cible d'avant)
    if (t.wasPressed()) hideFocus();
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
            else if (g_view.screen == Screen::Results && g_search.results.empty()) showResultsNote(noAnswer);
        }
        show();
    }

    // Test tactile : rond sous le doigt tant qu'il touche ; « Fermer » pour sortir
    if (g_view.screen == Screen::TouchTest) {
        if (t.wasPressed()) {
            ++g_view.touchCount;
            const wr::Target* tg = g_ui.targets().hit(t.x, t.y);
            if (tg && activateTouchTest(*tg, now)) {
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
        const wr::Target* tg = t.wasPressed() ? g_ui.targets().hit(t.x, t.y) : nullptr;
        if (tg && activateLog(*tg, now)) {
            // écran quitté, ou file du WiiM demandée
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
            const bool dark = g_idle.brightness == 0;  // écran éteint : ce premier appui ne fait que le rallumer
            setBrightness(kBrightOn);                  // atténué : on voit l'écran, l'appui agit aussi
            if (dark) {
                // rien d'autre : rallumé seulement
            } else if (g_voice.listening && g_voice.dictation) {
                // Pendant la dictée : ■ termine ; Accueil ou Réglages annulent ; le reste est ignoré
                if (g_ui.hitMic(t.x, t.y)) {
                    sound::tap();
                    finishDictation(true);
                } else if (g_ui.hitHome(t.x, t.y) || g_ui.hitSettings(t.x, t.y)) {
                    finishDictation(false);
                }
            } else if (const wr::Target* tg = g_ui.targets().hit(t.x, t.y)) {
                activateSearch(*tg);
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
        // Écran éteint, ou vue ambiance atténuée : ce premier appui ne fait que rallumer. Écran atténué sur les
        // commandes ou le menu : on y voit les boutons, l'appui rallume et agit.
        const bool dimOnly = g_idle.brightness > 0 && g_idle.brightness != kBrightOn && !g_view.ambient;
        if (dimOnly) {
            logEvent("Appui : r\xC3\xA9veil de l'\xC3\xA9" "cran att\xC3\xA9nu\xC3\xA9, l'appui agit (%d,%d)", t.x, t.y);
            setBrightness(kBrightOn);
        }
        if (g_idle.brightness != kBrightOn) {
            // Écran éteint (ou vue ambiance atténuée) : ce premier appui ne fait que le rallumer
            logEvent("Appui : r\xC3\xA9veil de l'\xC3\xA9" "cran (%d,%d)", t.x, t.y);
            setBrightness(kBrightOn);
            if (g_view.ambient) exitAmbient();
        } else if (g_view.screen == Screen::Menu) {
            if (const wr::Target* tg = g_ui.targets().hit(t.x, t.y)) activateMenu(*tg, now);
        } else if (g_view.screen == Screen::TileMenu) {
            if (const wr::Target* tg = g_ui.targets().hit(t.x, t.y)) activateTileMenu(*tg, now);
        } else if (g_view.screen == Screen::Library) {
            if (const wr::Target* tg = g_ui.targets().hit(t.x, t.y)) {
                Press at;
                at.x = t.x, at.y = t.y, at.touch = true, at.now = now;
                activateLibrary(*tg, at);
            }
        } else if (g_view.ambient) {
            logEvent("Appui en vue ambiance (%d,%d)%s", t.x, t.y, g_ui.hitHome(t.x, t.y) ? " : Accueil" : "");
            exitAmbient();  // vue ambiance : un appui ramène les commandes
            sound::tap();
        } else {
            tapControls(t.x, t.y, now);  // y compris l'appui long de l'en-tête et la bande des presets (suivis ci-dessous)
        }
    }
    serviceStrip(t, now);
    serviceLibraryTouch(t, now);
    serviceVolumeTouch(t, now);
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
        showTile(g_flash.preset);  // bouton relâché : sa zone seule
        showTransport(g_flash.transport);
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
