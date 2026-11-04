// Télécommande WiiM pour M5PaperS3.
//
// Démarrage :
//  - pas encore configuré → portail de configuration (Wi-Fi + QR code à l'écran)
//  - sinon → connexion Wi-Fi puis interface : pochette et titre en cours,
//    presets, lecture/pause, piste précédente/suivante, volume
// Appui long (3 s) sur l'en-tête → portail de configuration.

#include <M5Unified.h>
#include <WiFi.h>

#include "art.h"
#include "core.h"
#include "portal.h"
#include "settings.h"
#include "sound.h"
#include "ui.h"
#include "wiim_client.h"

static Settings g_settings;
static Ui g_ui;
static WiimClient g_wiim;
static UiState g_state;
static ArtFetcher g_artFetcher;
static ArtImage g_art;           // pochette affichée
static std::string g_artWanted;  // adresse de la pochette attendue ("" = aucune)
static int g_metaRefresh = 0;    // getMetaInfo à refaire (la pochette suit parfois le titre avec retard)

static uint32_t g_lastPoll = 0;
static uint32_t g_lastFull = 0;
static uint32_t g_messageUntil = 0;
static uint32_t g_headerPressStart = 0;
static bool g_headerPressed = false;
static uint32_t g_lastTouch = 0;

static constexpr uint32_t kPollMs = 5000;
static constexpr uint32_t kFullRefreshMs = 30UL * 60 * 1000;
static constexpr uint32_t kLongPressMs = 3000;
static constexpr uint32_t kWifiTimeoutMs = 20000;

static int batteryLevel() {
    int b = M5.Power.getBatteryLevel();
    return b < 0 ? -1 : (b + 2) / 5 * 5;  // arrondi à 5 % : évite de redessiner pour 1 %
}

static void setMessage(const String& m, uint32_t durationMs = 8000) {
    g_state.message = m;
    g_messageUntil = m.length() ? millis() + durationMs : 0;
}

static String errorText(WiimError e) {
    switch (e) {
        case WiimError::None: return "";
        case WiimError::Network: return "WiiM injoignable : vérifie qu'il est allumé et son IP";
        case WiimError::PinMismatch:
            return "Certificat du WiiM inattendu (" + g_wiim.lastSeenFingerprint().substring(0, 12) +
                   "…). Appui long ici pour reconfigurer";
        case WiimError::Http: return "Réponse inattendue du WiiM";
        case WiimError::Refused: return "Le WiiM a refusé la commande";
    }
    return "";
}

static void afterCommand(WiimError e) {
    if (e != WiimError::None) sound::error();
    if (g_wiim.takeJustPinned()) setMessage("Certificat du WiiM enregistré");
    else setMessage(errorText(e), e == WiimError::PinMismatch ? 600000 : 8000);
    g_state.online = (e == WiimError::None);
    g_ui.drawHeader(g_state);
    g_lastPoll = millis() - kPollMs + 1200;  // rafraîchir le titre peu après
}

static void poll() {
    g_lastPoll = millis();
    UiState next = g_state;
    next.battery = batteryLevel();
    if (g_messageUntil && millis() > g_messageUntil) {
        next.message = "";
        g_messageUntil = 0;
    }

    wr::PlayerStatus st;
    WiimError e = g_wiim.status(st);
    next.online = (e == WiimError::None);
    if (next.online) next.player = st;
    if (g_wiim.takeJustPinned()) {
        next.message = "Certificat du WiiM enregistré";
        g_messageUntil = millis() + 8000;
    } else if (e == WiimError::PinMismatch) {
        next.message = errorText(e);
        g_messageUntil = millis() + 600000;
    }

    // Nouvelle piste (ou première réponse) : album, qualité et pochette via getMetaInfo,
    // redemandés au relevé suivant car le WiiM les met parfois à jour en retard
    if (next.online) {
        bool trackChanged = !g_state.online || next.player.title != g_state.player.title ||
                            next.player.artist != g_state.player.artist ||
                            next.player.album != g_state.player.album;
        if (trackChanged) g_metaRefresh = 2;
        if (g_metaRefresh > 0) {
            --g_metaRefresh;
            wr::TrackMeta meta;
            if (g_wiim.metaInfo(meta) == WiimError::None) next.meta = meta;
            else if (trackChanged) next.meta = wr::TrackMeta();
        }
        if (next.meta.artUrl != g_artWanted) {
            g_artWanted = next.meta.artUrl;
            if (g_artWanted.empty()) {
                g_art.reset();
                g_ui.drawArt(nullptr);
            } else {
                g_artFetcher.request(g_artWanted);
            }
        }
    }

    bool playingChanged = next.player.playing != g_state.player.playing;
    bool headerChanged = next.player != g_state.player || next.meta != g_state.meta ||
                         next.online != g_state.online || next.message != g_state.message ||
                         next.battery != g_state.battery;
    g_state = next;
    if (headerChanged) g_ui.drawHeader(g_state);
    if (playingChanged) g_ui.showTransport(wr::kPlayPause, false, g_state.player.playing);
}

// Pochette téléchargée en tâche de fond : affichage dès qu'elle est prête
static void checkArt() {
    if (g_artWanted.empty()) return;
    ArtImage img;
    if (!g_artFetcher.take(g_artWanted, img)) return;
    g_art = std::move(img);
    g_ui.drawArt(&g_art);
}

static void handleTap(int x, int y) {
    int p = g_ui.hitPreset(x, y);
    if (p >= 0) {
        sound::tap();
        g_ui.showPreset(p, true);
        WiimError e = g_wiim.command("MCUKeyShortClick:" + String(g_settings.presets[p].slot));
        g_ui.showPreset(p, false);
        afterCommand(e);
        return;
    }

    int t = g_ui.hitTransport(x, y);
    if (t < 0) return;
    sound::tap();
    g_ui.showTransport(t, true, g_state.player.playing);
    WiimError e = WiimError::None;
    switch (t) {
        case wr::kVolDown:
        case wr::kVolUp: {
            int v = g_state.player.volume + (t == wr::kVolUp ? g_settings.volStep : -g_settings.volStep);
            v = constrain(v, 0, 100);
            e = g_wiim.command("setPlayerCmd:vol:" + String(v));
            if (e == WiimError::None) g_state.player.volume = v;
            break;
        }
        case wr::kPrev: e = g_wiim.command("setPlayerCmd:prev"); break;
        case wr::kNext: e = g_wiim.command("setPlayerCmd:next"); break;
        case wr::kPlayPause:
            e = g_wiim.command("setPlayerCmd:onepause");
            if (e == WiimError::None) g_state.player.playing = !g_state.player.playing;
            break;
    }
    g_ui.showTransport(t, false, g_state.player.playing);
    afterCommand(e);
}

// Connexion Wi-Fi ; en cas d'échec, l'écran le dit et un appui ouvre la configuration.
static void connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_settings.ssid.c_str(), g_settings.pass.c_str());
    g_ui.showInfo("Connexion…", "Réseau Wi-Fi : " + g_settings.ssid);

    uint32_t start = millis();
    bool shownError = false;
    while (WiFi.status() != WL_CONNECTED) {
        M5.update();
        if (!shownError && millis() - start > kWifiTimeoutMs) {
            g_ui.showInfo("Wi-Fi injoignable",
                          "Impossible de rejoindre « " + g_settings.ssid +
                              " ». Nouvel essai en cours. Si le réseau ou le mot de passe a changé, "
                              "touche l'écran pour reconfigurer.",
                          "Touche l'écran pour ouvrir la configuration");
            shownError = true;
        }
        if (shownError && M5.Touch.getDetail().wasPressed()) {
            sound::tap();
            runSetupPortal(g_settings, g_ui, "Le Wi-Fi « " + g_settings.ssid + " » est injoignable.");
        }
        delay(20);
    }
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    bool presetsOk = loadSettings(g_settings);
    sound::begin(g_settings.beep);
    g_ui.begin(g_settings);

    if (!g_settings.configured()) runSetupPortal(g_settings, g_ui, "Première configuration.");
    if (!presetsOk) runSetupPortal(g_settings, g_ui, "La liste de presets enregistrée est invalide.");

    connectWifi();
    g_wiim.configure(g_settings);
    g_artFetcher.begin();
    g_state.battery = batteryLevel();
    g_ui.drawMain(g_state, nullptr, true);
    g_lastFull = millis();
    poll();
}

void loop() {
    M5.update();
    auto t = M5.Touch.getDetail();

    if (t.wasPressed()) {
        g_lastTouch = millis();
        if (g_ui.hitHeader(t.x, t.y)) {
            g_headerPressed = true;
            g_headerPressStart = millis();
        } else {
            handleTap(t.x, t.y);
        }
    }
    if (g_headerPressed) {
        if (!t.isPressed()) g_headerPressed = false;
        else if (millis() - g_headerPressStart > kLongPressMs) {
            sound::confirm();
            runSetupPortal(g_settings, g_ui, "Reconfiguration demandée.");
        }
    }

    if (millis() - g_lastPoll > kPollMs) poll();
    checkArt();

    // Rafraîchissement complet contre les images fantômes de l'e-ink, jamais pendant l'utilisation
    bool idle = millis() - g_lastTouch > 10000;
    if (idle && (g_ui.needsDeghost() || millis() - g_lastFull > kFullRefreshMs)) {
        g_ui.drawMain(g_state, &g_art, true);
        g_lastFull = millis();
    }
    delay(10);
}
