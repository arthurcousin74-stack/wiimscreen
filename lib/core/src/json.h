// JSON helpers without Arduino, two entry styles:
// - flat lookups (jsonString, jsonInt): first occurrence of a key anywhere in the text, for the small flat answers
//   of the WiiM HTTP API;
// - depth-aware helpers (jsonTop, jsonArray, jsonText, jsonValuesOf): values at the right nesting level, for the
//   Qobuz and Deezer answers where an item holds several nested "title"/"name"/"id" keys.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace wr {

// Valeur d'un champ texte d'un objet JSON plat ({"cle":"valeur",...}).
bool jsonString(const std::string& json, const std::string& key, std::string& out);
// JSON escape \uXXXX at s[i] == 'u' (with its low surrogate when it is a pair, e.g. an emoji) appended as UTF-8;
// a lone surrogate becomes U+FFFD. Returns the index of the last character used (i if the digits are invalid).
size_t decodeJsonUnicode(const std::string& s, size_t i, std::string& out);
// Champ entier, écrit 12 ou "12".
bool jsonInt(const std::string& json, const std::string& key, long& out);

// Raw text of the value of `key` at the top level of a JSON object (string with its quotes, number,
// object or array); empty if absent.
std::string jsonTop(const std::string& object, const std::string& key);
// Unquoted, unescaped string value (\" \\ \/ \n \t \uXXXX handled; \r \b \f dropped); numbers returned as written.
std::string jsonText(const std::string& raw);
// Top-level elements of a JSON array (raw text each).
std::vector<std::string> jsonArray(const std::string& raw);
// Values of every "showName" key in a GetBasicUserInfo Result (linked services), in order.
std::vector<std::string> jsonValuesOf(const std::string& json, const std::string& key);

}  // namespace wr
