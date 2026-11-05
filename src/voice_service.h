#pragma once

#include <Arduino.h>

#include <string>

#include "voice.h"

// Services en ligne de la commande vocale, en HTTPS vérifié (certificats Mozilla) :
//  - transcription de la phrase par OpenAI (le son n'est envoyé que pendant l'écoute)
//  - recherche de radios dans l'annuaire libre Radio Browser (sans compte)

// PCM 16 bits mono → texte. hint : noms propres attendus (presets) pour aider la
// transcription. En cas d'échec, `error` contient un message court en français.
bool transcribe(const int16_t* pcm, size_t samples, uint32_t sampleRate, const String& apiKey,
                const String& model, const std::string& hint, std::string& text, String& error);

// Meilleure station pour `query` (flux direct de préférence)
bool findRadio(const std::string& query, wr::RadioStation& out, String& error);
