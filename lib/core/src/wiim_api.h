// WiiM (LinkPlay) HTTP API answers without Arduino: getPlayerStatus, getMetaInfo, getPresetInfo, getStatusEx,
// SSDP discovery, and the texts made from them (source name, time, quality). See docs/WIIM_API_NOTES.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wr {

constexpr int kMaxPresets = 12;

struct Preset {
    int slot = 0;
    std::string label;
    std::string picUrl;  // image du preset (getPresetInfo), vide si aucune
    std::string favId;   // favori enregistré sur la télécommande (file du WiiM), vide pour un preset du WiiM
};

// Le WiiM encode Title/Artist en hexadécimal UTF-8 : décode si c'est le cas.
std::string decodeHexText(const std::string& s);

struct PlayerStatus {
    bool ok = false;
    bool playing = false;
    bool loading = false;  // "loading" : le WiiM prépare la lecture (après un preset)
    int volume = 0;
    bool muted = false;
    int mode = 0;            // source : 1 AirPlay, 2 DLNA, 31 Spotify, 41 Bluetooth…
    int loop = -1;           // "loop" : répétition et lecture aléatoire (0-5), -1 si absent
    uint32_t positionMs = 0;
    uint32_t durationMs = 0;  // 0 : flux sans durée (radio)
    // Les WiiM ne donnent pas toujours titre/artiste/album ici : voir TrackMeta
    std::string title;
    std::string artist;
    std::string album;
    bool operator==(const PlayerStatus& o) const {
        return ok == o.ok && playing == o.playing && loading == o.loading && volume == o.volume &&
               muted == o.muted &&
               mode == o.mode && positionMs == o.positionMs && durationMs == o.durationMs &&
               title == o.title && artist == o.artist && album == o.album;
    }
    bool operator!=(const PlayerStatus& o) const { return !(*this == o); }
};

PlayerStatus parsePlayerStatus(const std::string& json);

// Nom lisible de la source (champ "mode" de getPlayerStatus), vide si inconnue.
std::string sourceName(int mode);

// Réponse de getMetaInfo : {"metaData":{"album":…,"albumArtURI":…,"sampleRate":…}}
struct TrackMeta {
    bool ok = false;
    std::string title;
    std::string artist;
    std::string album;
    std::string artUrl;  // http(s) uniquement, vide sinon
    int sampleRate = 0;  // Hz
    int bitDepth = 0;
    bool operator==(const TrackMeta& o) const {
        return ok == o.ok && title == o.title && artist == o.artist && album == o.album &&
               artUrl == o.artUrl && sampleRate == o.sampleRate && bitDepth == o.bitDepth;
    }
    bool operator!=(const TrackMeta& o) const { return !(*this == o); }
};
TrackMeta parseMetaInfo(const std::string& json);

// Presets enregistrés sur le WiiM (getPresetInfo) : numéro, nom, image.
bool parsePresetInfo(const std::string& json, std::vector<Preset>& out);

// "3:07", "1:02:03"
std::string formatTime(uint32_t ms);

// --- Découverte du WiiM sur le réseau
// Réponse SSDP (UPnP) : adresse IPv4 tirée de l'en-tête LOCATION
bool ssdpHost(const std::string& response, std::string& ip);
// getStatusEx : identité d'un lecteur WiiM / LinkPlay (ok faux pour un autre appareil)
struct WiimIdentity {
    bool ok = false;
    std::string name;  // nom choisi dans l'app WiiM Home
    std::string uuid;  // identifiant unique, stable
};
WiimIdentity parseStatusEx(const std::string& json);

// "24 bits · 96 kHz", "16 bits · 44,1 kHz" ; vide si inconnu.
std::string formatQuality(int sampleRate, int bitDepth);

}  // namespace wr
