// Télécommande WiiM pour M5Stack Tab5.
//
// Démarrage :
//  - pas encore configuré → portail de configuration (Wi-Fi + QR code à l'écran)
//  - sinon → connexion Wi-Fi puis interface : pochette et titre en cours,
//    progression, presets du WiiM avec leurs images, lecture/pause,
//    piste précédente/suivante, volume
// Appui long (3 s) sur le bandeau → portail de configuration.
// Après 30 s sans appui pendant la lecture : vue ambiance (grande pochette).
// En vue ambiance, le micro anime un visualiseur (son analysé sur place, jamais envoyé).
// Après 10 min sans appui, l'écran s'atténue ; il s'éteint après 20 min si rien ne joue.
// Soulever ou déplacer le Tab5 rallume l'écran (capteur de mouvement).

#include <M5Unified.h>
#include <WiFi.h>

#include <esp_heap_caps.h>

#include <cmath>
#include <map>

#include "art.h"
#include "core.h"
#include "dsp.h"
#include "voice.h"
#include "voice_service.h"
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

static std::vector<wr::Preset> g_presets;              // presets affichés
static std::map<std::string, ArtImage> g_presetImages;  // images des presets, par adresse
static bool g_presetsFromWiim = false;
static uint32_t g_lastPresetFetch = 0;

static uint32_t g_lastPoll = 0;
static uint32_t g_statusAt = 0;  // instant du dernier relevé (pour faire avancer la progression)
static uint32_t g_lastTick = 0;
static uint32_t g_messageUntil = 0;
static uint32_t g_headerPressStart = 0;
static bool g_headerPressed = false;
static uint32_t g_lastTouch = 0;
static bool g_ambient = false;
static int g_brightness = -1;

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

static constexpr uint32_t kPollMs = 5000;
static constexpr uint32_t kLongPressMs = 3000;
static constexpr uint32_t kWifiTimeoutMs = 20000;
static constexpr uint32_t kPresetRefreshMs = 10UL * 60 * 1000;
static constexpr uint32_t kPresetRetryMs = 30UL * 1000;

// Vue ambiance après 30 s ; rétroéclairage atténué après 10 min, éteint après 20 min si rien ne joue
static constexpr uint32_t kAmbientMs = 30UL * 1000;
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

static bool g_listening;  // commande vocale en cours d'écoute (voir plus bas)

static void show(int pressedPreset = -1, int pressedTransport = -1) {
    if (g_listening) return;  // l'écran d'écoute reste affiché
    g_lastTick = millis();
    if (g_ambient) g_ui.renderAmbient(displayState());
    else g_ui.render(displayState(), pressedPreset, pressedTransport);
}

// Vue ambiance : le bus audio passe du haut-parleur au micro (partagés sur le Tab5)
static void enterAmbient() {
    g_ambient = true;
    if (g_settings.visualizer) {
        sound::suspend();
        g_micOn = M5.Mic.begin();
        g_micPending = false;
        g_vis.reset();
        g_ui.showLevels(g_vis.levels(), g_vis.peaks());
    }
    show();
}

static void exitAmbient() {
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
    show();
    g_lastPoll = millis() - kPollMs + 1200;  // relire l'état peu après
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

// Presets du WiiM (noms et images), ou liste saisie complétée par les images du WiiM
static bool refreshPresets() {
    g_lastPresetFetch = millis();
    std::vector<wr::Preset> fromWiim;
    bool ok = g_wiim.presets(fromWiim) == WiimError::None;
    std::vector<wr::Preset> list;
    if (g_settings.autoPresets()) {
        if (ok && !fromWiim.empty()) {
            list = fromWiim;
        } else if (!g_presets.empty()) {
            return false;  // on garde la liste actuelle
        } else {
            for (int i = 1; i <= 6; ++i) list.push_back(wr::Preset{i, "Preset " + std::to_string(i), ""});
        }
    } else {
        list = g_settings.presets;
        if (ok)
            for (auto& p : list)
                for (auto& w : fromWiim)
                    if (w.slot == p.slot) p.picUrl = w.picUrl;
    }
    g_presetsFromWiim = ok;
    if (samePresets(list, g_presets)) return false;
    applyPresets(list);
    return true;
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

// --------------------------------------------------------------------- relevé

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
    if (g_wiim.takeJustPinned()) {
        next.message = "Certificat du WiiM enregistré";
        g_messageUntil = millis() + 8000;
    } else if (e == WiimError::PinMismatch) {
        next.message = errorText(e);
        g_messageUntil = millis() + 600000;
    }

    bool artCleared = false;
    if (next.online) {
        // Titre, artiste, album, pochette et qualité : getMetaInfo (les WiiM ne les
        // donnent pas dans getPlayerStatus)
        wr::TrackMeta meta;
        if (g_wiim.metaInfo(meta) == WiimError::None) {
            next.meta = meta;
            if (!meta.title.empty()) st.title = meta.title;
            if (!meta.artist.empty()) st.artist = meta.artist;
            if (!meta.album.empty()) st.album = meta.album;
        }
        next.player = st;
        g_statusAt = millis();

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

    bool presetsChanged = false;
    if (next.online) {
        bool due = millis() - g_lastPresetFetch > (g_presetsFromWiim ? kPresetRefreshMs : kPresetRetryMs);
        if (due || !g_state.online) presetsChanged = refreshPresets();
    }

    bool changed = artCleared || presetsChanged || next.player != g_state.player ||
                   next.meta != g_state.meta || next.online != g_state.online ||
                   next.message != g_state.message || next.battery != g_state.battery;
    g_state = next;
    if (changed) show();
}

// --------------------------------------------------------------------- appuis

// Barres du bandeau (volume, coupure du son, progression) : action immédiate
static bool handleBars(int x, int y) {
    int vol;
    uint32_t pos;
    if (g_ui.hitVolume(x, y, vol)) {
        sound::tap();
        WiimError e = g_wiim.command("setPlayerCmd:vol:" + String(vol));
        if (e == WiimError::None) {
            g_state.player.volume = vol;
            g_state.player.muted = false;
        }
        afterCommand(e);
        return true;
    }
    if (g_ui.hitMute(x, y)) {
        sound::tap();
        bool mute = !g_state.player.muted;
        WiimError e = g_wiim.command(String("setPlayerCmd:mute:") + (mute ? "1" : "0"));
        if (e == WiimError::None) g_state.player.muted = mute;
        afterCommand(e);
        return true;
    }
    if (g_ui.hitProgress(x, y, pos)) {
        sound::tap();
        WiimError e = g_wiim.command("setPlayerCmd:seek:" + String(pos / 1000));  // secondes
        if (e == WiimError::None) {
            g_state.player.positionMs = pos;
            g_statusAt = millis();
        }
        afterCommand(e);
        return true;
    }
    return false;
}

static void playPreset(int p) {
    sound::tap();
    show(p);
    WiimError e = g_wiim.command("MCUKeyShortClick:" + String(g_presets[p].slot));
    if (e == WiimError::None) {
        g_activePreset = p;
        g_activeSince = millis();
        g_activeMode = -1;
        g_ui.setActivePreset(p);
    }
    afterCommand(e);
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
        g_wiim.command("setPlayerCmd:vol:" + String(g_duckedFrom));
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
    if (g_state.online && p.playing && !p.muted && p.volume > 15 &&
        g_wiim.command("setPlayerCmd:vol:12") == WiimError::None)
        g_duckedFrom = p.volume;
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
    const String heard = String("\xC2\xAB ") + text.c_str() + " \xC2\xBB";
    const String dash = " \xE2\x80\x94 ";
    auto& p = g_state.player;
    WiimError e = WiimError::None;
    String done;
    switch (in.kind) {
        case wr::VoiceIntent::None:
            sound::error();
            setMessage("Je n'ai pas compris " + heard);
            show();
            return;
        case wr::VoiceIntent::Play:
            e = g_wiim.command("setPlayerCmd:resume");
            if (e == WiimError::None) p.playing = true, g_statusAt = millis();
            done = "Lecture";
            break;
        case wr::VoiceIntent::Pause:
            e = g_wiim.command("setPlayerCmd:pause");
            if (e == WiimError::None) p.playing = false;
            done = "Pause";
            break;
        case wr::VoiceIntent::Next:
            e = g_wiim.command("setPlayerCmd:next");
            done = "Morceau suivant";
            break;
        case wr::VoiceIntent::Prev:
            e = g_wiim.command("setPlayerCmd:prev");
            done = "Morceau pr\xC3\xA9" "c\xC3\xA9" "dent";
            break;
        case wr::VoiceIntent::VolUp:
        case wr::VoiceIntent::VolDown:
        case wr::VoiceIntent::SetVolume: {
            int v = in.kind == wr::VoiceIntent::SetVolume ? in.volume
                    : p.volume + (in.kind == wr::VoiceIntent::VolUp ? 2 : -2) * g_settings.volStep;
            v = constrain(v, 0, 100);
            e = g_wiim.command("setPlayerCmd:vol:" + String(v));
            if (e == WiimError::None) p.volume = v, p.muted = false;
            done = "Volume " + String(v);
            break;
        }
        case wr::VoiceIntent::Mute:
        case wr::VoiceIntent::Unmute: {
            bool mute = in.kind == wr::VoiceIntent::Mute;
            e = g_wiim.command(String("setPlayerCmd:mute:") + (mute ? "1" : "0"));
            if (e == WiimError::None) p.muted = mute;
            done = mute ? "Son coup\xC3\xA9" : "Son r\xC3\xA9tabli";
            break;
        }
        case wr::VoiceIntent::Preset:
            g_ui.setPage(in.preset / std::max(1, g_ui.perPage()));  // montre la tuile du preset
            playPreset(in.preset);  // gère l'affichage et les erreurs
            if (g_state.online) {
                setMessage(heard + dash + g_presets[in.preset].label.c_str());
                show();
            }
            return;
        case wr::VoiceIntent::Radio: {
            wr::RadioStation st;
            String err;
            if (!findRadio(in.query, st, err)) {
                sound::error();
                setMessage(heard + dash + err);
                show();
                return;
            }
            e = g_wiim.command("setPlayerCmd:play:" + String(wr::urlEncode(st.url, true).c_str()));
            if (e == WiimError::None) {
                g_activePreset = -1;
                g_ui.setActivePreset(-1);
            }
            done = String("Radio ") + st.name.c_str();
            break;
        }
    }
    afterCommand(e);
    if (e == WiimError::None) {
        setMessage(heard + dash + done);
        show();
    }
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
        if (g_stripPreset >= 0) show(g_stripPreset);
        return;
    }

    if (g_ui.hitMic(x, y)) {
        startVoice();
        return;
    }

    int t = g_ui.hitTransport(x, y);
    if (t < 0) return;
    sound::tap();
    show(-1, t);
    WiimError e = WiimError::None;
    switch (t) {
        case wr::kVolDown:
        case wr::kVolUp: {
            int v = g_state.player.volume + (t == wr::kVolUp ? g_settings.volStep : -g_settings.volStep);
            v = constrain(v, 0, 100);
            e = g_wiim.command("setPlayerCmd:vol:" + String(v));
            if (e == WiimError::None) {
                g_state.player.volume = v;
                g_state.player.muted = false;
            }
            break;
        }
        case wr::kPrev: e = g_wiim.command("setPlayerCmd:prev"); break;
        case wr::kNext: e = g_wiim.command("setPlayerCmd:next"); break;
        case wr::kPlayPause:
            e = g_wiim.command("setPlayerCmd:onepause");
            if (e == WiimError::None) {
                g_state.player.playing = !g_state.player.playing;
                g_statusAt = millis();
            }
            break;
    }
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
    // Avant tout appel Wi-Fi (y compris le portail de configuration)
    WiFi.setPins(kSdioClk, kSdioCmd, kSdioD0, kSdioD1, kSdioD2, kSdioD3, kSdioRst);
    setBrightness(kBrightOn);

    bool presetsOk = loadSettings(g_settings);
    sound::begin(g_settings.beep);
    g_ui.begin(g_settings);
    g_ui.setVisualizer(g_settings.visualizer);
    g_ui.setVoiceEnabled(g_settings.voiceEnabled());

    if (!g_settings.configured()) runSetupPortal(g_settings, g_ui, "Première configuration.");
    if (!presetsOk) runSetupPortal(g_settings, g_ui, "La liste de presets enregistrée est invalide.");

    connectWifi();
    g_wiim.configure(g_settings);
    g_artFetcher.begin();
    g_state.battery = batteryLevel();
    g_lastTouch = millis();
    refreshPresets();
    poll();
    show();
}

void loop() {
    M5.update();
    auto t = M5.Touch.getDetail();
    const uint32_t now = millis();

    if (t.wasPressed()) {
        g_lastTouch = now;
        if (g_brightness != kBrightOn) {
            // Écran atténué ou éteint : ce premier appui ne fait que le rallumer
            setBrightness(kBrightOn);
            if (g_ambient) exitAmbient();
        } else if (g_listening) {
            finishVoice(!g_ui.hitVoiceCancel(t.x, t.y));  // Annuler, ou « j'ai fini »
        } else if (g_ambient) {
            exitAmbient();  // vue ambiance : un appui ramène les commandes
            sound::tap();
        } else if (handleBars(t.x, t.y)) {
            // volume, coupure du son ou progression
        } else if (g_ui.hitHeader(t.x, t.y)) {
            g_headerPressed = true;  // appui long : configuration
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
            runSetupPortal(g_settings, g_ui, "Reconfiguration demandée.");
        }
    }

    if (!g_listening && now - g_lastPoll > kPollMs) poll();  // pas de pause dans l'écoute
    checkImages();
    serviceMic();
    serviceVoice();
    serviceMotion(now);

    // Progression : une image par seconde pendant la lecture (écran allumé)
    const auto& p = g_state.player;
    if (g_brightness > 0 && p.playing && p.durationMs > 0 && now - g_lastTick >= 1000) show();
    // Chargement : anneau qui tourne autour du bouton lecture (~20 images/s)
    if (g_brightness > 0 && !g_ambient && p.loading && !g_stripTouch && now - g_lastTick >= 50) show();

    const uint32_t idle = now - g_lastTouch;
    if (!g_ambient && !g_listening && g_brightness == kBrightOn && idle > kAmbientMs && p.playing &&
        g_state.online)
        enterAmbient();
    if (idle > kOffMs && !p.playing) {
        if (g_ambient) exitAmbient();  // micro coupé avec l'écran
        setBrightness(0);
    }
    else if (idle > kDimMs && g_brightness == kBrightOn) setBrightness(kBrightDim);
    delay(10);
}
