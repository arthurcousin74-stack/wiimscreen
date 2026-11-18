// Audio sans Arduino (testable sur PC) : en-tête WAV pour la transcription de la dictée.
// Déplacés, toujours déclarés par cet en-tête : normalizeText, sharesWord (text.h) ; recherche Deezer, numéro de
// profil Deezer (deezer.h). Encodage d'URL : urlQueryEncode, upnp.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core.h"
#include "deezer.h"
#include "text.h"

namespace wr {

// En-tête WAV (44 octets) pour du PCM 16 bits mono
void wavHeader(uint8_t out[44], uint32_t dataBytes, uint32_t sampleRate);

}  // namespace wr
