#include "voice_service.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include "ca_bundle.h"
#include "core.h"

static const char* kBoundary = "----wiimtab5-7c3f9a1e";

// Champ de formulaire : retours à la ligne retirés (ils casseraient le corps multipart)
static std::string field(const char* name, std::string value) {
    for (char& c : value)
        if (c == '\r' || c == '\n') c = ' ';
    return std::string("--") + kBoundary + "\r\nContent-Disposition: form-data; name=\"" + name +
           "\"\r\n\r\n" + value + "\r\n";
}

bool transcribe(const int16_t* pcm, size_t samples, uint32_t sampleRate, const String& apiKey,
                const String& model, const std::string& hint, std::string& text, String& error) {
    text.clear();
    if (WiFi.status() != WL_CONNECTED) {
        error = "Pas de Wi-Fi";
        return false;
    }
    // Corps multipart : modèle, langue, indices, puis le fichier WAV
    std::string pre = field("model", model.c_str()) + field("language", "fr") + field("response_format", "json");
    if (!hint.empty()) pre += field("prompt", hint.substr(0, 600));
    pre += std::string("--") + kBoundary +
           "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"voix.wav\"\r\n"
           "Content-Type: audio/wav\r\n\r\n";
    const std::string post = std::string("\r\n--") + kBoundary + "--\r\n";
    const size_t pcmBytes = samples * 2;
    const size_t len = pre.size() + 44 + pcmBytes + post.size();
    auto* body = static_cast<uint8_t*>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) {
        error = "Mémoire insuffisante";
        return false;
    }
    size_t at = 0;
    memcpy(body, pre.data(), pre.size());
    at += pre.size();
    wr::wavHeader(body + at, pcmBytes, sampleRate);
    at += 44;
    memcpy(body + at, pcm, pcmBytes);  // l'ESP32 est petit-boutiste, comme le WAV
    at += pcmBytes;
    memcpy(body + at, post.data(), post.size());

    WiFiClientSecure client;
    client.setCACertBundle(ca_bundle_start, caBundleSize());
    client.setHandshakeTimeout(10);
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(20000);
    http.setReuse(false);
    bool ok = false;
    if (http.begin(client, "https://api.openai.com/v1/audio/transcriptions")) {
        http.addHeader("Authorization", "Bearer " + apiKey);
        http.addHeader("Content-Type", String("multipart/form-data; boundary=") + kBoundary);
        int code = http.POST(body, len);
        String resp = code > 0 ? http.getString() : String();
        http.end();
        if (code == 200) {
            ok = wr::jsonString(resp.c_str(), "text", text);
            if (!ok) error = "Réponse inattendue d'OpenAI";
        } else if (code == 401) {
            error = "Clé OpenAI refusée : vérifie-la dans la configuration";
        } else if (code == 429) {
            error = "Quota OpenAI dépassé (crédit ou limite de débit)";
        } else if (code == 404 || code == 400) {
            error = "OpenAI refuse la requête (" + String(code) + ") : vérifie le modèle de transcription";
        } else if (code < 0) {
            error = "OpenAI injoignable";
        } else {
            error = "Erreur OpenAI " + String(code);
        }
        if (!ok) log_w("transcription : HTTP %d %s", code, resp.substring(0, 200).c_str());
    } else {
        error = "OpenAI injoignable";
    }
    heap_caps_free(body);
    return ok;
}

bool findRadio(const std::string& query, wr::RadioStation& out, String& error) {
    if (query.empty()) {
        error = "Nom de radio vide";
        return false;
    }
    const std::string url = "https://all.api.radio-browser.info/json/stations/search?name=" + wr::urlEncode(query) +
                            "&order=clickcount&reverse=true&limit=12&hidebroken=true";
    WiFiClientSecure client;
    client.setCACertBundle(ca_bundle_start, caBundleSize());
    client.setHandshakeTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(6000);
    http.setTimeout(10000);
    http.setReuse(false);
    http.setUserAgent("wiim-remote-tab5/1.0");  // demandé par Radio Browser
    if (!http.begin(client, url.c_str())) {
        error = "Annuaire de radios injoignable";
        return false;
    }
    int code = http.GET();
    int size = http.getSize();
    if (code != 200 || size > 256 * 1024) {
        http.end();
        error = "Annuaire de radios injoignable";
        return false;
    }
    String resp = http.getString();
    http.end();
    auto stations = wr::parseRadioBrowser(resp.c_str());
    int best = wr::bestStation(stations, query);
    if (best < 0) {
        error = String("Aucune radio « ") + query.c_str() + " »";
        return false;
    }
    out = stations[best];
    return true;
}
