// Texte et audio sans Arduino (testable sur PC) : normalisation des textes, recherche Deezer publique,
// encodage d'URL, en-tête WAV pour la transcription de la dictée.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core.h"

namespace wr {

// Minuscules, sans accents ni ponctuation, espaces simples : « Joue FIP ! » → « joue fip ».
// Lettres non latines (cyrillique, grec, CJK…) gardées telles quelles (cyrillique et grec en minuscules).
std::string normalizeText(const std::string& s);
// Some word of the query (3 letters or more, accents/case ignored) appears in one of the texts. A query with no
// such word ("U2", "Yo Yo Ma", "AC/DC") is compared whole, word for word; a query with nothing to compare counts as
// relevant. Letters outside Latin-1 (Cyrillic, Greek, CJK…) are kept by normalizeText and count as letters.
bool sharesWord(const std::string& query, const std::vector<std::string>& texts);

// Deezer public search (api.deezer.com/search?q=…, no key): tracks with their album.
// Ids are 64-bit on every target (`long` is 32-bit on the ESP32-P4: Deezer ids above 2^31 were clamped there).
struct DeezerTrack {
    int64_t id = 0, albumId = 0;
    std::string title, artist, album, cover;
};
std::vector<DeezerTrack> parseDeezerTracks(const std::string& json);  // aussi user/<id>/tracks (même forme)
// Deezer profile number from what the user typed: the number itself, or a profile address
// (https://www.deezer.com/fr/profile/123456). Empty if none found.
std::string deezerUserId(const std::string& text);
// En-tête WAV (44 octets) pour du PCM 16 bits mono
void wavHeader(uint8_t out[44], uint32_t dataBytes, uint32_t sampleRate);

}  // namespace wr
