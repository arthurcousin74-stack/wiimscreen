#pragma once

#include <Arduino.h>

#include <string>

#include "voice.h"

// Transcription de la dictée (recherche) par OpenAI, en HTTPS vérifié (certificats Mozilla) ; le son
// n'est envoyé que pendant l'écoute.

// PCM 16 bits mono → texte. hint : contexte pour aider la
// transcription. En cas d'échec, `error` contient un message court dans la langue de l'interface (wr::L).
bool transcribe(const int16_t* pcm, size_t samples, uint32_t sampleRate, const String& apiKey,
                const String& model, const std::string& hint, std::string& text, String& error);

