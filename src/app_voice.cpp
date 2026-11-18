// Dictée de la recherche : bouton micro → écoute (la musique est baissée le temps d'écouter) → appui sur
// le bouton rouge → transcription OpenAI en tâche de fond → texte cherché.

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <cmath>
#include <cstring>

#include "app_internal.h"
#include "i18n.h"
#include "log.h"
#include "sound.h"
#include "voice_service.h"

namespace app {

void stopListening() {
    M5.Mic.end();
    g_mic.pending = false;
    g_voice.listening = false;
    sound::resume();
    if (g_voice.duckedFrom >= 0) {
        g_worker.command("setPlayerCmd:vol:" + String(g_voice.duckedFrom));
        g_voice.duckedFrom = -1;
    }
}

void startVoice() {
    if (!g_settings.voiceEnabled()) return;
    g_voice.dictation = true;
    if (!g_voice.buf)
        g_voice.buf = static_cast<int16_t*>(heap_caps_malloc(kVoiceMax * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_voice.buf) {
        g_search.status = wr::L("Not enough memory to listen", "M\xC3\xA9moire insuffisante pour \xC3\xA9" "couter");
        renderSearch();
        return;
    }
    const auto& p = g_state.player;
    if (g_state.online && p.playing && !p.muted && p.volume > 15) {
        g_voice.duckedFrom = p.volume;
        g_worker.command("setPlayerCmd:vol:12");
    }
    sound::confirm();
    delay(200);  // le bip finit avant que le bus audio passe au micro
    sound::suspend();
    if (!M5.Mic.begin()) {
        stopListening();
        g_voice.dictation = false;
        g_search.status = wr::L("Microphone unavailable", "Micro indisponible");
        renderSearch();
        return;
    }
    g_voice.listening = true;
    g_voice.len = 0;
    g_voice.level = 0;
    g_mic.pending = false;
    g_mic.cur = 0;
    renderSearch();
}

// Dictée de la recherche : la phrase entendue devient le texte cherché, puis la recherche part
static void sttTask(void*) {
    // Indication pour la transcription, dans la langue de l'interface (choisie à l'exécution, pas à l'initialisation)
    const std::string hint = wr::L("Music search: artist names, track and album titles, in English or French.",
                                   "Recherche de musique : noms d'artistes, titres de morceaux et d'albums, en "
                                   "fran\xC3\xA7" "ais ou en anglais.");
    std::string text;
    String err;
    const bool ok = transcribe(g_voice.buf, g_voice.len, kMicRate, g_settings.openaiKey, g_settings.sttModel, hint, text, err);
    g_voice.stt.text = text;
    g_voice.stt.err = err;
    g_voice.stt.ok = ok;
    g_voice.stt.done = true;  // écrit en dernier : la boucle lit les champs après
    vTaskDelete(nullptr);
}

void finishDictation(bool send) {
    stopListening();
    g_voice.dictation = false;
    if (!send || g_voice.len < kMicRate / 3) {
        g_search.status = send ? String(wr::L("I didn't hear anything", "Je n'ai rien entendu")) : String();
        renderSearch();
        return;
    }
    if (g_voice.stt.busy) return;
    g_ui.updateSearchField(g_search.query, "", false, g_search.source, g_search.deezerOk, 2);
    g_voice.stt.busy = true;
    g_voice.stt.done = false;
    // Pile en RAM interne (TLS) ; cœur 0, comme la tâche WiiM : le cœur 1 reste à l'écran et au toucher
    if (xTaskCreatePinnedToCore(sttTask, "stt", 16 * 1024, nullptr, 1, nullptr, 0) != pdPASS) {
        g_voice.stt.busy = false;
        g_search.status = wr::L("Not enough memory for the transcription", "M\xC3\xA9moire insuffisante pour la transcription");
        renderSearch();
    }
}

// Résultat de la transcription (appelé par la boucle de l'écran de recherche)
void serviceTranscription() {
    if (!g_voice.stt.busy || !g_voice.stt.done) return;
    g_voice.stt.busy = false;
    g_voice.stt.done = false;
    if (g_view.screen != Screen::Search) return;  // l'écran a été quitté entre-temps
    if (!g_voice.stt.ok) {
        logEvent("Transcription : %s", g_voice.stt.err.c_str());
        sound::error();
        g_search.status = g_voice.stt.err;
        renderSearch();
        return;
    }
    const std::string text = wr::cleanTranscript(g_voice.stt.text, 60);  // guillemets et ponctuation ajoutés retirés
    logEvent("Dict\xC3\xA9" "e : \xC2\xAB %s \xC2\xBB", text.c_str());
    g_search.query = text;
    g_search.status = "";
    if (text.empty()) {
        g_search.status = wr::L("I didn't understand anything", "Je n'ai rien compris");
        renderSearch();
        return;
    }
    runSearchQuery();
}

// Micro pendant l'écoute : enregistre, suit le niveau, détecte la fin de la phrase
void serviceVoice() {
    if (!g_voice.listening) return;
    if (!g_mic.pending) {
        g_mic.pending = M5.Mic.record(g_mic.buf[g_mic.cur], kMicBlock, kMicRate);
        return;
    }
    if (M5.Mic.isRecording()) return;
    const int done = g_mic.cur;
    g_mic.cur ^= 1;
    g_mic.pending = M5.Mic.record(g_mic.buf[g_mic.cur], kMicBlock, kMicRate);
    const int16_t* b = g_mic.buf[done];
    double sum = 0;
    for (int i = 0; i < kMicBlock; ++i) sum += double(b[i]) * b[i];
    const float rms = std::sqrt(sum / kMicBlock) / 32768.0f;
    const size_t n = std::min<size_t>(kMicBlock, kVoiceMax - g_voice.len);
    memcpy(g_voice.buf + g_voice.len, b, n * 2);
    g_voice.len += n;
    g_voice.level = std::max(std::min(1.0f, rms * 10), g_voice.level * 0.8f);
    // On écoute jusqu'à l'appui sur ■ (ou la limite du tampon)
    if (millis() - g_voice.frame > 100) {
        g_voice.frame = millis();
        g_ui.updateSearchField(g_search.query, "", false, g_search.source, g_search.deezerOk, 1, g_voice.level);
    }
    if (g_voice.len >= kVoiceMax) finishDictation(true);
}

}  // namespace app
