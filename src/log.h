#pragma once

#include <Arduino.h>

#include <vector>

// Journal des derniers évènements (appuis, commandes envoyées au WiiM et leur
// résultat, changements de vue, erreurs) : affiché par Réglages > Journal pour
// diagnostiquer sur l'appareil, et copié sur le port série (115200 bauds).
// Utilisable depuis plusieurs tâches. Ne jamais y écrire de secret.
void logEvent(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::vector<String> logLines();  // du plus ancien au plus récent
