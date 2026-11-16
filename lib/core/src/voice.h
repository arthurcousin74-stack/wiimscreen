// Texte et audio sans Arduino (testable sur PC) : normalisation des textes, recherche Deezer publique,
// encodage d'URL, en-tête WAV pour la transcription de la dictée.
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

// Deezer public search (api.deezer.com/search?q=…, no key): tracks with their album
struct DeezerTrack {
    long id = 0, albumId = 0;
    std::string title, artist, album, cover;
};
std::vector<DeezerTrack> parseDeezerTracks(const std::string& json);  // aussi user/<id>/tracks (même forme)
// Deezer profile number from what the user typed: the number itself, or a profile address
// (https://www.deezer.com/fr/profile/123456). Empty if none found.
std::string deezerUserId(const std::string& text);
// Encodage d'URL (RFC 3986) ; keepSlashColon garde « : » et « / » tels quels
std::string urlEncode(const std::string& s, bool keepSlashColon = false);

// En-tête WAV (44 octets) pour du PCM 16 bits mono
void wavHeader(uint8_t out[44], uint32_t dataBytes, uint32_t sampleRate);

}  // namespace wr
