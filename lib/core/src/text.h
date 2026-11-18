// Text helpers without Arduino: trimming, UTF-8, word wrapping, HTML escaping, normalisation for comparisons.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace wr {

// Copy without the leading and trailing white space (std::isspace)
std::string trim(const std::string& s);
// Value of a hexadecimal digit (0-9, a-f, A-F), -1 for any other character
int hexVal(char c);
// UTF-8 of a code point (U+0000..U+10FFFF; others ignored)
void appendUtf8(std::string& out, unsigned long cp);
// Retire le dernier caractère UTF-8 (pas seulement le dernier octet)
std::string utf8PopBack(const std::string& s);

std::string htmlEscape(const std::string& s);

// Coupe un texte UTF-8 en lignes de largeur maximale, en ajoutant "…" si ça déborde.
std::vector<std::string> wrapText(const std::string& text, int maxWidth, int maxLines,
                                  const std::function<int(const std::string&)>& measure);

// Minuscules, sans accents ni ponctuation, espaces simples : « Joue FIP ! » → « joue fip ».
// Lettres non latines (cyrillique, grec, CJK…) gardées telles quelles (cyrillique et grec en minuscules).
std::string normalizeText(const std::string& s);
// Some word of the query (3 letters or more, accents/case ignored) appears in one of the texts. A query with no
// such word ("U2", "Yo Yo Ma", "AC/DC") is compared whole, word for word; a query with nothing to compare counts as
// relevant. Letters outside Latin-1 (Cyrillic, Greek, CJK…) are kept by normalizeText and count as letters.
bool sharesWord(const std::string& query, const std::vector<std::string>& texts);

}  // namespace wr
