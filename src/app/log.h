#pragma once

#include <Arduino.h>

#include <vector>

// Journal des derniers évènements (appuis, commandes envoyées au WiiM et leur
// résultat, changements de vue, erreurs) : affiché par Réglages > Journal pour
// diagnostiquer sur l'appareil, et copié sur le port série (115200 bauds).
// Utilisable depuis plusieurs tâches. Ne jamais y écrire de secret.
void logEvent(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Tagged Journal line, for the lines that automated tests check (tools/relay/selftest.sh,
// platform/pc/selftest.sh). The French sentence stays as it is; the tests match the tag and the values instead.
//   logTagged("search.restore", "paused=%d create=%d | Après la recherche : … (%d, %d)", paused, c, c2, ...);
// `fmt` is "<key=value pairs> | <sentence>" (no pairs: "| <sentence>"); it is split at the first "| ".
// - Journal screen (RAM, logLines()): the sentence only, exactly as logEvent would show it.
// - Serial port / simulator terminal: "<time>  [<tag>] <key=value pairs> | <sentence>".
// Tag: lower-case ASCII, dotted by area (wiim.status, search.results…). Values: ASCII, no spaces
// (numbers, 0/1, ok/refused/network/pin/http, skip). Renaming a tag or a key breaks the self-tests: change them
// together. Rewording the sentence is harmless.
void logTagged(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
std::vector<String> logLines();  // du plus ancien au plus récent
