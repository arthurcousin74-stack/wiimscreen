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

#include <cmath>
#include <map>

#include "art.h"
#include "core.h"
#include "discovery.h"
#include "dsp.h"
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
static std::map<std::string, ArtImage> g_presetImages;  // images des presets, par adresse
static bool g_presetsFromWiim = false;
static uint32_t g_lastPresetFetch = 0;

static uint32_t g_statusAt = 0;  // instant du dernier relevé (pour faire avancer la progression)
static uint32_t g_lastTick = 0;
static uint32_t g_lastStatusTick = 0;
static uint32_t g_messageUntil = 0;
static uint32_t g_headerPressStart = 0;
static bool g_headerPressed = false;
static uint32_t g_lastTouch = 0;
static bool g_ambient = false;
static int g_brightness = -1;
static bool g_listening = false;  // commande vocale en cours d'écoute
static bool g_menuOpen = false;   // menu Réglages affiché (appui long)
static bool g_touchTest = false;  // écran de test tactile (menu Réglages)
static bool g_logOpen = false;    // écran Journal (menu Réglages)
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
static UiState displayState() {
    UiState s = g_state;
    auto& p = s.player;
    if (p.playing && p.durationMs > 0)
        p.positionMs = std::min(p.durationMs, p.positionMs + (millis() - g_statusAt));
    return s;
}

// Affichage différé : show() marque l'écran à redessiner, renderNow() le dessine une
// seule fois par tour de boucle, même si plusieurs changements sont arrivés.
static bool g_dirty = false;

static void show() { g_dirty = true; }

static bool modalOpen() { return g_listening || g_menuOpen || g_touchTest || g_logOpen; }

static void renderNow() {
    if (!g_dirty || modalOpen()) return;  // écrans modaux : rien par-dessus (rendu à leur fermeture)
    g_dirty = false;
    const uint32_t now = millis();
    // Bouton enfoncé : tuile sous le doigt, ou court effet après un appui
    int pressedPreset = (g_stripTouch && !g_stripDragged) ? g_stripPreset : -1;
    int pressedTransport = -1;
    if (now < g_flashUntil) {
        if (pressedPreset < 0) pressedPreset = g_flashPreset;
        pressedTransport = g_flashTransport;
    }
    g_lastTick = now;
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
        case WiimError::Network: return "WiiM injoignable : v\xC3\xA9rifie qu'il est allum\xC3\xA9";
        case WiimError::PinMismatch:
            return "Certificat du WiiM inattendu (" + fingerprint.substring(0, 12) +
                   "\xE2\x80\xA6). Appui long ici pour reconfigurer";
        case WiimError::Http: return "R\xC3\xA9ponse inattendue du WiiM";
        case WiimError::Refused: return "Le WiiM a refus\xC3\xA9 la commande";
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

// ------------------------------------------------------------------- presets

static bool samePresets(const std::vector<wr::Preset>& a, const std::vector<wr::Preset>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].slot != b[i].slot || a[i].label != b[i].label || a[i].picUrl != b[i].picUrl) return false;
    return true;
}

static void applyPresets(const std::vector<wr::Preset>& list) {
    g_presets = list;
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
    if (samePresets(list, g_presets)) return;
    applyPresets(list);
    show();
}

static void requestPresets() {
    g_lastPresetFetch = millis();
    g_worker.refreshPresets();
}

// Images téléchargées en tâche de fond : pochette ou image de preset
static void checkImages() {
    ArtImage img;
    bool changed = false;
    while (g_artFetcher.poll(img)) {
        if (!g_artWanted.empty() && img.url == g_artWanted) {
            g_art = std::move(img);
            g_ui.setArt(&g_art);
            changed = true;
            continue;
        }
        bool used = false;
        for (auto& p : g_presets)
            if (p.picUrl == img.url) used = true;
        if (!used) continue;  // piste ou liste déjà changée : image périmée
        std::string url = img.url;
        ArtImage& stored = g_presetImages[url];
        stored = std::move(img);
        for (size_t i = 0; i < g_presets.size(); ++i)
            if (g_presets[i].picUrl == url) g_ui.setPresetArt((int)i, &stored);
        changed = true;
    }
    if (changed) show();
}

// ------------------------------------------------------- état venant du WiiM

static void applyReport(const WiimReport& r) {
    UiState next = g_state;
    next.online = r.online;
    bool artCleared = false;
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
    // le WiiM bascule), puis on l'oublie si la source change ou si la lecture s'arrête
    if (g_activePreset >= 0 && next.online) {
        bool stopped = !next.player.playing && next.player.title.empty();
        if (millis() - g_activeSince > 6000) {
            if (g_activeMode < 0) g_activeMode = next.player.mode;
            else if (next.player.mode != g_activeMode) stopped = true;
        }
        if (stopped) {
            g_activePreset = -1;
            g_ui.setActivePreset(-1);
        }
    }

    if (next.online) {
        bool due = millis() - g_lastPresetFetch > (g_presetsFromWiim ? kPresetRefreshMs : kPresetRetryMs);
        if (due || !g_state.online) requestPresets();
    }

    bool changed = artCleared || next.player != g_state.player || next.meta != g_state.meta ||
                   next.online != g_state.online;
    g_state = next;
    if (changed) show();
}

static void serviceWiim() {
    WiimReport r;
    // Relevé commencé avant la dernière commande : il annulerait l'affichage optimiste
    if (g_worker.takeReport(r) && r.cmdSeq >= g_worker.queued()) applyReport(r);

    WiimEvent ev;
    while (g_worker.takeEvent(ev)) {
        switch (ev.kind) {
            case WiimEvent::CommandDone:
                if (ev.error != WiimError::None && ev.error != WiimError::PinMismatch) {
                    sound::error();
                    setMessage(errorText(ev.error));
                    if (ev.error == WiimError::Network) g_state.online = false;
                    g_worker.pollSoon();  // l'état réel remplace l'affichage optimiste
                    show();
                }
                break;
            case WiimEvent::Presets: onPresets(ev.ok, ev.presets); break;
            case WiimEvent::Pinned:
                setMessage("Certificat du WiiM enregistr\xC3\xA9");
                show();
                break;
            case WiimEvent::PinMismatch:
                sound::error();
                setMessage(errorText(WiimError::PinMismatch, ev.text), 600000);
                show();
                break;
            case WiimEvent::Moved:
                g_settings.host = ev.text;
                setMessage("WiiM retrouv\xC3\xA9 \xC3\xA0 sa nouvelle adresse (" + ev.text + ")");
                show();
                break;
            case WiimEvent::Identity:
                g_settings.wiimUuid = ev.uuid;
                g_settings.wiimName = ev.name;
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
    if (g_messageUntil && now > g_messageUntil) {
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
        g_ui.showInfo("Recherche du WiiM\xE2\x80\xA6", "Le Tab5 cherche les lecteurs WiiM sur ton Wi-Fi.");
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
                if (c == Ui::PickConfig) runSetupPortal(g_settings, g_ui, "Saisis l'adresse IP du WiiM.");
            }
            delay(20);
        }
    }
}

// --------------------------------------------------------------------- appuis

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

static void playPreset(int p) {
    logEvent("Preset %d : %s", g_presets[p].slot, g_presets[p].label.c_str());
    sound::tap();
    g_activePreset = p;
    g_activeSince = millis();
    g_activeMode = -1;
    g_ui.setActivePreset(p);
    g_worker.command("MCUKeyShortClick:" + String(g_presets[p].slot));
    flash(p, -1);
}

// ------------------------------------------------------------ commande vocale
//
// Bouton micro → écoute (la musique est baissée le temps d'écouter) → fin de parole
// détectée (ou appui) → transcription OpenAI → commande, preset ou radio.

static int16_t* g_voiceBuf = nullptr;
static size_t g_voiceLen = 0;
static constexpr size_t kVoiceMax = kMicRate * 8;  // 8 s au plus
static wr::SpeechEndpoint g_endpoint;
static float g_voiceLevel = 0;
static uint32_t g_voiceFrame = 0;
static int g_duckedFrom = -1;  // volume d'avant l'écoute, à rétablir

// Noms propres attendus : aident la transcription (« Couleur 3 », « FIP »…)
static std::string voiceHint() {
    std::string h =
        "T\xC3\xA9l\xC3\xA9" "commande de musique. Commandes : joue, mets, pause, reprends, suivant, "
        "pr\xC3\xA9" "c\xC3\xA9" "dent, plus fort, moins fort, volume, coupe le son. Radios et presets : ";
    for (size_t i = 0; i < g_presets.size(); ++i) h += (i ? ", " : "") + g_presets[i].label;
    return h + ".";
}

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

static void startVoice() {
    if (!g_settings.voiceEnabled()) return;
    if (!g_voiceBuf)
        g_voiceBuf = static_cast<int16_t*>(heap_caps_malloc(kVoiceMax * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_voiceBuf) {
        setMessage("M\xC3\xA9moire insuffisante pour \xC3\xA9" "couter");
        show();
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
        setMessage("Micro indisponible");
        show();
        return;
    }
    g_listening = true;
    g_voiceLen = 0;
    g_voiceLevel = 0;
    g_endpoint = wr::SpeechEndpoint();
    g_micPending = false;
    g_micCur = 0;
    g_ui.renderVoice(Ui::VoicePhase::Listening, 0);
}

static void runVoice(const std::string& text) {
    const wr::VoiceIntent in = wr::parseVoice(text, g_presets);
    logEvent("Voix : \xC2\xAB %s \xC2\xBB -> intention %d", text.c_str(), (int)in.kind);
    const String heard = String("\xC2\xAB ") + text.c_str() + " \xC2\xBB";
    const String dash = " \xE2\x80\x94 ";
    auto& p = g_state.player;
    String done;
    switch (in.kind) {
        case wr::VoiceIntent::None:
            sound::error();
            setMessage("Je n'ai pas compris " + heard);
            show();
            return;
        case wr::VoiceIntent::Play:
            g_worker.command("setPlayerCmd:resume");
            p.playing = true;
            g_statusAt = millis();
            done = "Lecture";
            break;
        case wr::VoiceIntent::Pause:
            g_worker.command("setPlayerCmd:pause");
            p.playing = false;
            done = "Pause";
            break;
        case wr::VoiceIntent::Next:
            g_worker.command("setPlayerCmd:next");
            done = "Morceau suivant";
            break;
        case wr::VoiceIntent::Prev:
            g_worker.command("setPlayerCmd:prev");
            done = "Morceau pr\xC3\xA9" "c\xC3\xA9" "dent";
            break;
        case wr::VoiceIntent::VolUp:
        case wr::VoiceIntent::VolDown:
        case wr::VoiceIntent::SetVolume: {
            int v = in.kind == wr::VoiceIntent::SetVolume ? in.volume
                    : p.volume + (in.kind == wr::VoiceIntent::VolUp ? 2 : -2) * g_settings.volStep;
            v = constrain(v, 0, 100);
            g_worker.command("setPlayerCmd:vol:" + String(v));
            p.volume = v;
            p.muted = false;
            done = "Volume " + String(v);
            break;
        }
        case wr::VoiceIntent::Mute:
        case wr::VoiceIntent::Unmute: {
            bool mute = in.kind == wr::VoiceIntent::Mute;
            g_worker.command(String("setPlayerCmd:mute:") + (mute ? "1" : "0"));
            p.muted = mute;
            done = mute ? "Son coup\xC3\xA9" : "Son r\xC3\xA9tabli";
            break;
        }
        case wr::VoiceIntent::Preset:
            g_ui.setPage(in.preset / std::max(1, g_ui.perPage()));  // montre la tuile du preset
            playPreset(in.preset);
            done = g_presets[in.preset].label.c_str();
            break;
        case wr::VoiceIntent::Radio: {
            wr::RadioStation st;
            String err;
            if (!findRadio(in.query, st, err)) {
                sound::error();
                setMessage(heard + dash + err);
                show();
                return;
            }
            g_worker.command("setPlayerCmd:play:" + String(wr::urlEncode(st.url, true).c_str()));
            g_activePreset = -1;
            g_ui.setActivePreset(-1);
            done = String("Radio ") + st.name.c_str();
            break;
        }
    }
    setMessage(heard + dash + done);  // une erreur du WiiM remplacera ce message
    show();
}

static void finishVoice(bool send) {
    stopListening();
    if (!send) {
        show();
        return;
    }
    if (g_voiceLen < kMicRate / 3) {
        setMessage("Je n'ai rien entendu");
        show();
        return;
    }
    g_ui.renderVoice(Ui::VoicePhase::Thinking, 0);
    std::string text;
    String err;
    if (!transcribe(g_voiceBuf, g_voiceLen, kMicRate, g_settings.openaiKey, g_settings.sttModel, voiceHint(), text,
                    err)) {
        logEvent("Transcription : %s", err.c_str());
        sound::error();
        setMessage(err);
        show();
        return;
    }
    runVoice(text);
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
    const auto state = g_endpoint.feed(rms);
    g_voiceLevel = std::max(std::min(1.0f, rms * 10), g_voiceLevel * 0.8f);
    if (millis() - g_voiceFrame > 80) {  // ~12 images/s : l'anneau suit la voix
        g_voiceFrame = millis();
        g_ui.renderVoice(Ui::VoicePhase::Listening, g_voiceLevel);
    }
    if (state == wr::SpeechEndpoint::Done || g_voiceLen >= kVoiceMax) {
        finishVoice(true);
    } else if (state == wr::SpeechEndpoint::NothingHeard) {
        stopListening();
        setMessage("Je n'ai rien entendu");
        show();
    }
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
    if (g_ui.hitMic(x, y)) {
        startVoice();
        return;
    }
    if (g_ui.hitHome(x, y)) {  // Accueil (barre) : déjà ici, retour à la première page de presets
        logEvent("Appui Accueil (%d,%d)", x, y);
        sound::tap();
        g_ui.setPage(0);
        show();
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
        case wr::kPrev: g_worker.command("setPlayerCmd:prev"); break;
        case wr::kNext: g_worker.command("setPlayerCmd:next"); break;
        case wr::kPlayPause:
            // Ce que montre l'icône, explicitement : plus fiable qu'une bascule (onepause)
            // quand l'état affiché a un temps de retard sur le WiiM
            g_worker.command(p.playing ? "setPlayerCmd:pause" : "setPlayerCmd:resume");
            p.playing = !p.playing;
            g_statusAt = millis();
            break;
    }
    flash(-1, t);
}

// Connexion Wi-Fi ; en cas d'échec, l'écran le dit et un appui ouvre la configuration.
static void connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(g_settings.ssid.c_str(), g_settings.pass.c_str());
    g_ui.showInfo("Connexion\xE2\x80\xA6", "R\xC3\xA9seau Wi-Fi : " + g_settings.ssid);

    uint32_t start = millis();
    bool shownError = false;
    while (WiFi.status() != WL_CONNECTED) {
        M5.update();
        if (!shownError && millis() - start > kWifiTimeoutMs) {
            g_ui.showInfo("Wi-Fi injoignable",
                          "Impossible de rejoindre \xC2\xAB " + g_settings.ssid +
                              " \xC2\xBB. Nouvel essai en cours. Si le r\xC3\xA9seau ou le mot de passe a chang\xC3\xA9, "
                              "touche l'\xC3\xA9" "cran pour reconfigurer.",
                          "Touche l'\xC3\xA9" "cran pour ouvrir la configuration");
            shownError = true;
        }
        if (shownError && M5.Touch.getDetail().wasPressed()) {
            sound::tap();
            runSetupPortal(g_settings, g_ui, "Le Wi-Fi \xC2\xAB " + g_settings.ssid + " \xC2\xBB est injoignable.");
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

    bool presetsOk = loadSettings(g_settings);
    sound::begin(g_settings.beep);
    g_ui.begin(g_settings);
    g_ui.setVisualizer(g_settings.visualizer);
    g_ui.setVoiceEnabled(g_settings.voiceEnabled());

    if (!g_settings.configured()) runSetupPortal(g_settings, g_ui, "Premi\xC3\xA8re configuration.");
    if (!presetsOk) runSetupPortal(g_settings, g_ui, "La liste de presets enregistr\xC3\xA9" "e est invalide.");

    logEvent("D\xC3\xA9marrage, version %s", FW_VERSION);
    connectWifi();
    logEvent("Wi-Fi connect\xC3\xA9, adresse du Tab5 %s", WiFi.localIP().toString().c_str());
    if (!g_settings.host.length()) chooseWiim();  // recherche automatique du WiiM
    logEvent("WiiM : %s %s", g_settings.host.c_str(), g_settings.wiimName.c_str());
    g_artFetcher.begin();
    g_worker.begin(g_settings);
    if (!g_settings.autoPresets()) applyPresets(g_settings.presets);  // liste saisie : tout de suite
    requestPresets();
    g_state.online = true;  // jusqu'au premier relevé (une seconde environ)
    g_state.battery = batteryLevel();
    g_lastTouch = millis();
    show();
}

void loop() {
    M5.update();
    auto t = M5.Touch.getDetail();
    const uint32_t now = millis();

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
        } else if (g_menuOpen && g_ui.hitHome(t.x, t.y)) {
            sound::tap();  // Accueil : fermer les réglages
            g_menuOpen = false;
            show();
        } else if (g_menuOpen) {
            Ui::MenuChoice c = g_ui.hitMenu(t.x, t.y);
            if (c != Ui::MenuNone) sound::tap();
            if (c == Ui::MenuConfig) {
                runSetupPortal(g_settings, g_ui, "Reconfiguration demand\xC3\xA9" "e.");  // redémarre
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
            } else if (c == Ui::MenuClose) {
                g_menuOpen = false;
                show();
            }
        } else if (g_listening) {
            finishVoice(!g_ui.hitVoiceCancel(t.x, t.y));  // Annuler, ou « j'ai fini »
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
    checkImages();
    serviceMic();
    serviceVoice();
    serviceMotion(now);

    // Fin de l'effet « bouton enfoncé »
    if (g_flashUntil && now >= g_flashUntil) {
        g_flashUntil = 0;
        show();
    }
    // Progression : une image par seconde pendant la lecture (écran allumé)
    const auto& p = g_state.player;
    // Seule la ligne de progression est redessinée si rien d'autre n'a changé (~3 % de l'écran)
    if (g_brightness > 0 && p.playing && p.durationMs > 0 && now - g_lastTick >= 1000) {
        if (!g_dirty && !modalOpen() && g_ui.updateProgress(displayState())) g_lastTick = now;
        else show();
    }
    // Chargement : anneau qui tourne autour du bouton lecture (~20 images/s)
    if (g_brightness > 0 && !g_ambient && p.loading && !g_stripTouch && now - g_lastTick >= 50) show();

    const uint32_t idle = now - g_lastTouch;
    if (!g_ambient && !g_listening && !g_menuOpen && !g_touchTest && !g_logOpen && g_brightness == kBrightOn && idle > kAmbientMs && p.playing &&
        g_state.online)
        enterAmbient();
    if (idle > kOffMs && !p.playing) {
        if (g_ambient) exitAmbient();  // micro coupé avec l'écran
        setBrightness(0);
    } else if (idle > kDimMs && g_brightness == kBrightOn) {
        setBrightness(kBrightDim);
    }
    renderNow();
    delay(5);
}
