// Commande vocale sans Arduino (testable sur PC) : compréhension de la phrase
// transcrite, détection de fin de parole, recherche de radios, encodage audio.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core.h"

namespace wr {

// Minuscules, sans accents ni ponctuation, espaces simples : « Joue FIP ! » → « joue fip »
std::string normalizeText(const std::string& s);
// Some word of the query (3 letters or more, accents/case ignored) appears in one of the texts
bool sharesWord(const std::string& query, const std::vector<std::string>& texts);

// Similarité de deux textes normalisés (0..1), tolérante aux petites fautes de transcription
float similarity(const std::string& a, const std::string& b);

struct VoiceIntent {
    enum Kind { None, Play, Pause, Next, Prev, VolUp, VolDown, SetVolume, Mute, Unmute, Preset, Radio };
    Kind kind = None;
    int preset = -1;    // indice dans la liste des presets
    int volume = -1;    // SetVolume : 0..100
    std::string query;  // Radio : nom de station cherché
};

// Ordre : commandes de lecture, puis presets (nom approché ou « preset 3 »),
// puis recherche de radio avec ce qui reste de la phrase.
VoiceIntent parseVoice(const std::string& text, const std::vector<Preset>& presets);

struct RadioStation {
    std::string name;
    std::string url;      // flux à lire
    std::string favicon;  // logo, peut être vide
    bool hls = false;
    long clicks = 0;
};
// Réponse JSON de Radio Browser (/json/stations/search)
std::vector<RadioStation> parseRadioBrowser(const std::string& json);

// Deezer public search (api.deezer.com/search?q=…, no key): tracks with their album
struct DeezerTrack {
    long id = 0, albumId = 0;
    std::string title, artist, album, cover;
};
std::vector<DeezerTrack> parseDeezerTracks(const std::string& json);
// Meilleure station pour la recherche : nom le plus proche, flux direct de préférence,
// puis la plus écoutée. -1 si aucune.
int bestStation(const std::vector<RadioStation>& stations, const std::string& query);

// Encodage d'URL (RFC 3986) ; keepSlashColon garde « : » et « / » tels quels
std::string urlEncode(const std::string& s, bool keepSlashColon = false);

// En-tête WAV (44 octets) pour du PCM 16 bits mono
void wavHeader(uint8_t out[44], uint32_t dataBytes, uint32_t sampleRate);

// Fin de parole : attend que la voix démarre (niveau nettement au-dessus du bruit de
// fond), puis s'arrête après un silence ; abandonne si rien n'est dit.
class SpeechEndpoint {
public:
    enum State { Waiting, Speaking, Done, NothingHeard };
    // blockMs : durée d'un bloc ; silenceMs : silence de fin ; maxMs : durée maximale
    SpeechEndpoint(int blockMs = 32, int silenceMs = 900, int maxMs = 7000, int waitMs = 4000);
    State feed(float rms);  // niveau RMS du bloc (0..1)
    State state() const { return state_; }

private:
    int blockMs_, silenceMs_, maxMs_, waitMs_;
    State state_ = Waiting;
    float noise_ = -1;
    int elapsed_ = 0, quiet_ = 0, voiced_ = 0;
};

}  // namespace wr
