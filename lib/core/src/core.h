// Logique pure (sans Arduino) : testable sur PC avec `pio test -e native`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wr {

constexpr int kMaxPresets = 12;

struct Preset {
    int slot = 0;
    std::string label;
};

// Une ligne par preset : "1=Radio Swiss Jazz". Lignes vides et commentaires (#) ignorés.
// Renvoie false et remplit `error` si une ligne est invalide.
bool parsePresets(const std::string& text, std::vector<Preset>& out, std::string& error);

// Le WiiM encode Title/Artist en hexadécimal UTF-8 : décode si c'est le cas.
std::string decodeHexText(const std::string& s);

struct PlayerStatus {
    bool ok = false;
    bool playing = false;
    int volume = 0;
    int mode = 0;  // source : 1 AirPlay, 2 DLNA, 31 Spotify, 41 Bluetooth…
    std::string title;
    std::string artist;
    std::string album;
    bool operator==(const PlayerStatus& o) const {
        return ok == o.ok && playing == o.playing && volume == o.volume && mode == o.mode &&
               title == o.title && artist == o.artist && album == o.album;
    }
    bool operator!=(const PlayerStatus& o) const { return !(*this == o); }
};

// Valeur d'un champ texte d'un objet JSON plat ({"cle":"valeur",...}).
bool jsonString(const std::string& json, const std::string& key, std::string& out);
PlayerStatus parsePlayerStatus(const std::string& json);

// Nom lisible de la source (champ "mode" de getPlayerStatus), vide si inconnue.
std::string sourceName(int mode);

// Réponse de getMetaInfo : {"metaData":{"album":…,"albumArtURI":…,"sampleRate":…}}
struct TrackMeta {
    bool ok = false;
    std::string album;
    std::string artUrl;  // http(s) uniquement, vide sinon
    int sampleRate = 0;  // Hz
    int bitDepth = 0;
    bool operator==(const TrackMeta& o) const {
        return ok == o.ok && album == o.album && artUrl == o.artUrl && sampleRate == o.sampleRate &&
               bitDepth == o.bitDepth;
    }
    bool operator!=(const TrackMeta& o) const { return !(*this == o); }
};
TrackMeta parseMetaInfo(const std::string& json);

// "24 bits · 96 kHz", "16 bits · 44,1 kHz" ; vide si inconnu.
std::string formatQuality(int sampleRate, int bitDepth);

// URL http:// ou https:// avec un hôte, sans espace ni caractère de contrôle.
bool isHttpUrl(const std::string& url);

enum class ImageType { Unknown, Jpeg, Png };
ImageType sniffImage(const uint8_t* data, size_t len);

struct HttpResponse {
    int status = 0;
    std::string body;
};
// Réponse HTTP/1.x brute (en-têtes + corps), corps "chunked" accepté.
bool parseHttpResponse(const std::string& raw, HttpResponse& out);

std::string toHex(const uint8_t* data, size_t len);
// Accepte "AA:BB:..." ou "aabb..." (64 caractères hexa utiles).
bool parseFingerprint(const std::string& s, uint8_t out[32]);
bool constantTimeEq(const uint8_t* a, const uint8_t* b, size_t len);

// IPv4 ou nom d'hôte, avec ":port" optionnel.
bool isValidHost(const std::string& host);
void splitHostPort(const std::string& host, std::string& name, uint16_t& port, uint16_t defaultPort);

std::string htmlEscape(const std::string& s);

// Coupe un texte UTF-8 en lignes de largeur maximale, en ajoutant "…" si ça déborde.
std::vector<std::string> wrapText(const std::string& text, int maxWidth, int maxLines,
                                  const std::function<int(const std::string&)>& measure);

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum Transport { kVolDown = 0, kPrev, kPlayPause, kNext, kVolUp, kTransportCount };

struct Layout {
    Rect header;  // bandeau « en cours de lecture » (pochette + infos), appui long = configuration
    Rect art;     // pochette, carrée, à gauche du bandeau
    Rect info;    // titre, artiste, volume…
    std::vector<Rect> presets;
    Rect transport[kTransportCount];
};

// Le bandeau grandit tant que les boutons de presets gardent une hauteur confortable.
Layout computeLayout(int width, int height, int columns, int presetCount);

}  // namespace wr
