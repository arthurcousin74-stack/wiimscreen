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
    std::string picUrl;  // image du preset (getPresetInfo), vide si aucune
};

// Une ligne par preset : "1=Radio Swiss Jazz". Lignes vides et commentaires (#) ignorés.
// Renvoie false et remplit `error` si une ligne est invalide.
bool parsePresets(const std::string& text, std::vector<Preset>& out, std::string& error);

// Le WiiM encode Title/Artist en hexadécimal UTF-8 : décode si c'est le cas.
std::string decodeHexText(const std::string& s);

struct PlayerStatus {
    bool ok = false;
    bool playing = false;
    bool loading = false;  // "loading" : le WiiM prépare la lecture (après un preset)
    int volume = 0;
    bool muted = false;
    int mode = 0;            // source : 1 AirPlay, 2 DLNA, 31 Spotify, 41 Bluetooth…
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

// Valeur d'un champ texte d'un objet JSON plat ({"cle":"valeur",...}).
bool jsonString(const std::string& json, const std::string& key, std::string& out);
// Champ entier, écrit 12 ou "12".
bool jsonInt(const std::string& json, const std::string& key, long& out);
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

// Où en est la lecture d'une réponse sur une connexion gardée ouverte (keep-alive) :
// complete : la réponse entière est arrivée (Content-Length atteint, ou fin "chunked") ;
// reusable : la connexion peut servir à la requête suivante (le serveur ne la ferme
// pas et la fin du corps est délimitée). Sans longueur connue, il faut lire jusqu'à
// la fermeture : complete et reusable restent faux.
struct HttpFrame {
    bool complete = false;
    bool reusable = false;
};
HttpFrame httpFrame(const std::string& raw);

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
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    bool intersects(const Rect& o) const {
        return w > 0 && h > 0 && o.w > 0 && o.h > 0 && x < o.right() && o.x < right() && y < o.bottom() &&
               o.y < bottom();
    }
};

enum Transport { kVolDown = 0, kPrev, kPlayPause, kNext, kVolUp, kTransportCount };

// Mise en page de la vue commandes.
// Paysage : grande pochette à gauche ; à droite, de haut en bas, les textes, la
// progression, les commandes de lecture et le volume ; les presets en tuiles
// (pochette + nom) sur une bande en bas, par pages.
// Portrait : la même pile, pochette en haut, presets sur deux rangées.
// Barre de navigation (Accueil en haut, Réglages en bas) : à gauche en paysage, en haut en portrait.
struct Layout {
    Rect art;       // pochette, carrée (appui long sur elle ou les textes : configuration)
    Rect info;      // puces (source, qualité), titre, artiste, album
    Rect progress;  // temps écoulé, barre, durée
    Rect transport[kTransportCount];  // cercles : vol−, précédent, lecture, suivant, vol+
    Rect mic;       // bouton micro (commande vocale), au bout de la rangée des commandes
    Rect volume;    // pilule de volume (contient vol−, haut-parleur, curseur, valeur, vol+)
    Rect mute;      // icône du haut-parleur (coupe le son)
    Rect volBar;    // curseur de volume
    Rect strip;     // bande des presets (glisser pour changer de page)
    std::vector<Rect> presets;  // emplacements d'une page
    int perPage = 0;
    Rect dots;      // indicateur de pages
    Rect rail;         // barre de navigation
    Rect navHome;      // Accueil (écran principal)
    Rect navSettings;  // Réglages
    Rect content;      // reste de l'écran, à côté de la barre
};

// Les dimensions suivent la taille de l'écran (référence : 540 px sur le petit côté).
Layout computeLayout(int width, int height, int presetCount);

// Couleur stable tirée d'un nom (fond des presets sans image), lisible avec du texte blanc.
uint32_t nameColor(const std::string& name);

// Couleur d'accent tirée d'une pochette (pixels RGB888 à la suite) : moyenne des
// couleurs vives, ramenée à une luminosité lisible sur fond sombre.
// Renvoie `fallback` pour une image en noir et blanc ou presque.
uint32_t accentColor(const uint8_t* rgb, size_t pixelCount, uint32_t fallback);

}  // namespace wr
