#include "wiim_api.h"

#include "http.h"
#include "i18n.h"
#include "json.h"
#include "text.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace wr {

namespace {

bool validUtf8NoControl(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        if (n == 1 && (c < 0x20 || c == 0x7f)) return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

bool isUnknown(const std::string& s) {
    return s.empty() || s == "unknow" || s == "Unknown" || s == "un_known" || s == "unknown";
}

}  // namespace

std::string decodeHexText(const std::string& raw) {
    std::string s = trim(raw);
    if (s.empty() || s.size() % 2 != 0) return s;
    std::string out;
    out.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) {
        int hi = hexVal(s[i]), lo = hexVal(s[i + 1]);
        if (hi < 0 || lo < 0) return s;
        out += static_cast<char>(hi * 16 + lo);
    }
    return validUtf8NoControl(out) ? out : s;
}

PlayerStatus parsePlayerStatus(const std::string& json) {
    PlayerStatus st;
    std::string v;
    if (!jsonString(json, "status", v)) return st;
    st.ok = true;
    // "load" : vu sur un WiiM (2026-11-16) pendant le chargement d'une file ; "loading" sur d'autres LinkPlay
    st.loading = (v == "loading" || v == "load");
    st.playing = (v == "play" || st.loading);
    if (jsonString(json, "vol", v)) st.volume = std::max(0, std::min(100, std::atoi(v.c_str())));
    // Seuls Title/Artist/Album de getPlayerStatus sont en hexadécimal (docs/WIIM_API_NOTES.md) ;
    // espaces retirés : un titre « » ne compte pas comme un titre
    if (jsonString(json, "Title", v)) st.title = trim(decodeHexText(v));
    if (jsonString(json, "Artist", v)) st.artist = trim(decodeHexText(v));
    if (jsonString(json, "Album", v)) st.album = trim(decodeHexText(v));
    long n;
    if (jsonInt(json, "mode", n)) st.mode = static_cast<int>(n);
    if (jsonInt(json, "mute", n)) st.muted = n == 1;
    if (jsonInt(json, "loop", n) && n >= 0 && n <= 5) st.loop = static_cast<int>(n);
    if (jsonInt(json, "curpos", n) && n > 0) st.positionMs = static_cast<uint32_t>(n);
    if (jsonInt(json, "totlen", n) && n > 0) st.durationMs = static_cast<uint32_t>(n);
    for (std::string* f : {&st.title, &st.artist, &st.album})
        if (isUnknown(*f)) f->clear();
    return st;
}

// Valeurs du champ "mode" d'après la documentation WiiM (HTTP API v1.2)
std::string sourceName(int mode) {
    switch (mode) {
        case 1: return "AirPlay";
        case 2: return "DLNA";
        case 11: return "USB";
        case 16: return L("SD card", "Carte SD");
        case 31: return "Spotify";
        case 32: return "TIDAL";
        case 40: return L("Line in", "Entrée ligne");
        case 41: return "Bluetooth";
        case 42: return L("External storage", "Stockage externe");
        case 43: return L("Optical", "Optique");
        case 99: return "Multiroom";
        default: return "";
    }
}

bool ssdpHost(const std::string& response, std::string& ip) {
    std::string lower = response;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t p = lower.find("\nlocation:");
    if (p == std::string::npos) return false;
    p = lower.find("://", p);
    if (p == std::string::npos) return false;
    p += 3;
    size_t e = p;
    while (e < response.size() && (std::isdigit(static_cast<unsigned char>(response[e])) || response[e] == '.')) ++e;
    std::string host = response.substr(p, e - p);
    // IPv4 complète : quatre nombres de 0 à 255
    int parts = 0;
    size_t s = 0;
    while (s <= host.size()) {
        size_t d = host.find('.', s);
        if (d == std::string::npos) d = host.size();
        std::string n = host.substr(s, d - s);
        if (n.empty() || n.size() > 3 || std::atoi(n.c_str()) > 255) return false;
        ++parts;
        s = d + 1;
    }
    if (parts != 4) return false;
    ip = host;
    return true;
}

WiimIdentity parseStatusEx(const std::string& json) {
    WiimIdentity id;
    std::string v;
    if (!jsonString(json, "uuid", id.uuid) && !jsonString(json, "project", v)) return id;
    id.ok = true;
    if (!jsonString(json, "DeviceName", id.name) || trim(id.name).empty()) jsonString(json, "ssid", id.name);
    id.name = trim(id.name);  // texte ordinaire, pas d'hexadécimal (« 2021 » reste « 2021 »)
    if (id.name.size() > 60) id.name.resize(60);
    if (id.name.empty()) id.name = "WiiM";
    return id;
}

std::string formatTime(uint32_t ms) {
    uint32_t s = ms / 1000, h = s / 3600, m = (s / 60) % 60;
    s %= 60;
    char buf[16];
    if (h) snprintf(buf, sizeof buf, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    else snprintf(buf, sizeof buf, "%u:%02u", (unsigned)m, (unsigned)s);
    return buf;
}

bool parsePresetInfo(const std::string& json, std::vector<Preset>& out) {
    out.clear();
    size_t p = json.find("\"preset_list\"");
    if (p == std::string::npos) return false;
    p = json.find('[', p);
    if (p == std::string::npos) return false;
    // Objets plats successifs { ... } jusqu'au ']' de fin
    while (true) {
        size_t open = json.find_first_of("{]", p);
        if (open == std::string::npos || json[open] == ']') break;
        size_t close = json.find('}', open);
        if (close == std::string::npos) return false;
        std::string item = json.substr(open, close - open + 1);
        p = close + 1;
        long number = 0;
        std::string name, pic;
        if (!jsonInt(item, "number", number) || number < 1 || number > 20) continue;
        jsonString(item, "name", name);
        name = trim(name);  // texte ordinaire, pas d'hexadécimal
        if (isUnknown(name)) name = "Preset " + std::to_string(number);
        if (name.size() > 60) name.resize(60);
        if (jsonString(item, "picurl", pic) && !isHttpUrl(pic)) pic.clear();
        Preset preset;
        preset.slot = static_cast<int>(number);
        preset.label = name;
        preset.picUrl = pic;
        out.push_back(preset);
        if (static_cast<int>(out.size()) >= kMaxPresets) break;
    }
    std::sort(out.begin(), out.end(), [](const Preset& a, const Preset& b) { return a.slot < b.slot; });
    return true;
}

TrackMeta parseMetaInfo(const std::string& json) {
    TrackMeta m;
    if (json.find("\"metaData\"") == std::string::npos) return m;
    m.ok = true;
    // La documentation WiiM écrit certaines clés avec une espace finale ("albumArtURI ") :
    // les deux formes sont acceptées
    auto get = [&](const char* key, std::string& v) {
        return jsonString(json, key, v) || jsonString(json, std::string(key) + " ", v);
    };
    std::string v;
    // Espaces retirés : un titre « » ne compte pas comme un titre
    if (get("title", v) && !isUnknown(v = trim(v))) m.title = v;
    if (get("artist", v) && !isUnknown(v = trim(v))) m.artist = v;
    if (get("album", v) && !isUnknown(v = trim(v))) m.album = v;
    if (get("albumArtURI", v) && isHttpUrl(v)) m.artUrl = v;
    if (get("sampleRate", v)) m.sampleRate = std::max(0, std::atoi(v.c_str()));
    if (get("bitDepth", v)) m.bitDepth = std::max(0, std::atoi(v.c_str()));
    if (m.sampleRate > 768000) m.sampleRate = 0;
    if (m.bitDepth > 64) m.bitDepth = 0;
    return m;
}

std::string formatQuality(int sampleRate, int bitDepth) {
    std::string bits = bitDepth > 0 ? std::to_string(bitDepth) + " bits" : "";
    std::string rate;
    if (sampleRate >= 1000) {
        int tenths = (sampleRate + 50) / 100;  // en dixièmes de kHz
        rate = std::to_string(tenths / 10);
        if (tenths % 10) rate += "," + std::to_string(tenths % 10);
        rate += " kHz";
    }
    if (bits.empty()) return rate;
    if (rate.empty()) return bits;
    return bits + " \xC2\xB7 " + rate;  // ·
}

}  // namespace wr
