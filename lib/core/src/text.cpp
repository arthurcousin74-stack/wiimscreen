#include "text.h"

#include <algorithm>
#include <cctype>

namespace wr {

namespace {

// Découpe en mots en gardant les séparateurs simples
std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Retire le dernier caractère UTF-8 sur place (tout le texte s'il n'y a que des octets de suite)
void popUtf8(std::string& s) {
    while (!s.empty()) {
        unsigned char c = s.back();
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}

// Caractères latins accentués (UTF-8 sur 2 octets) → lettre de base
char baseLetter(unsigned c1, unsigned c2) {
    unsigned cp = ((c1 & 0x1F) << 6) | (c2 & 0x3F);
    if (cp >= 0xC0 && cp <= 0xC5) return 'a';
    if (cp == 0xC7) return 'c';
    if (cp >= 0xC8 && cp <= 0xCB) return 'e';
    if (cp >= 0xCC && cp <= 0xCF) return 'i';
    if (cp == 0xD1) return 'n';
    if (cp >= 0xD2 && cp <= 0xD6) return 'o';
    if (cp >= 0xD9 && cp <= 0xDC) return 'u';
    if (cp == 0xDD) return 'y';
    if (cp >= 0xE0 && cp <= 0xE5) return 'a';
    if (cp == 0xE7) return 'c';
    if (cp >= 0xE8 && cp <= 0xEB) return 'e';
    if (cp >= 0xEC && cp <= 0xEF) return 'i';
    if (cp == 0xF1) return 'n';
    if (cp >= 0xF2 && cp <= 0xF6) return 'o';
    if (cp >= 0xF9 && cp <= 0xFC) return 'u';
    if (cp == 0xFD || cp == 0xFF) return 'y';
    return ' ';
}

// Point de code UTF-8 à s[i] ; n : nombre d'octets (0 si la suite est invalide)
unsigned long utf8At(const std::string& s, size_t i, size_t& n) {
    const unsigned char c = s[i];
    n = (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (n == 0 || i + n > s.size()) return n = 0, 0;
    unsigned long cp = c & (n == 2 ? 0x1F : n == 3 ? 0x0F : 0x07);
    for (size_t k = 1; k < n; ++k) {
        const unsigned char d = s[i + k];
        if ((d & 0xC0) != 0x80) return n = 0, 0;
        cp = (cp << 6) | (d & 0x3F);
    }
    return cp;
}

// Ponctuation, symboles et émojis hors ASCII : séparateurs de mots
bool isSeparator(unsigned long cp) {
    return (cp >= 0x80 && cp <= 0xBF) || cp == 0xD7 || cp == 0xF7 || (cp >= 0x2000 && cp <= 0x2BFF) ||
           (cp >= 0x2E00 && cp <= 0x2E7F) || (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFE30 && cp <= 0xFE4F) ||
           (cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0x1F000 && cp <= 0x1FAFF) || cp == 0xFEFF;
}

// Minuscule des lettres cyrilliques et grecques de base
unsigned long lowerCp(unsigned long cp) {
    if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;  // А-Я
    if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;  // Ѐ-Џ (Ё…)
    if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) return cp + 0x20;  // Α-Ω
    return cp;
}

// Nombre de caractères (points de code) d'un texte UTF-8
size_t charCount(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) n += (c & 0xC0) != 0x80;
    return n;
}

}  // namespace

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void appendUtf8(std::string& o, unsigned long cp) {
    if (cp < 0x80) {
        o += static_cast<char>(cp);
    } else if (cp < 0x800) {
        o += static_cast<char>(0xC0 | (cp >> 6));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o += static_cast<char>(0xE0 | (cp >> 12));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        o += static_cast<char>(0xF0 | (cp >> 18));
        o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string htmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
        }
    }
    return o;
}

std::vector<std::string> wrapText(const std::string& text, int maxWidth, int maxLines,
                                  const std::function<int(const std::string&)>& measure) {
    std::vector<std::string> lines;
    std::vector<std::string> ws = words(text);
    size_t i = 0;
    const std::string ell = "\xE2\x80\xA6";  // …
    while (i < ws.size() && static_cast<int>(lines.size()) < maxLines) {
        std::string line = ws[i++];
        while (i < ws.size() && measure(line + " " + ws[i]) <= maxWidth) line += " " + ws[i++];
        bool last = static_cast<int>(lines.size()) == maxLines - 1;
        if (last && i < ws.size()) {
            // Texte restant : on coupe au mot si possible, sinon au caractère
            i = ws.size();
            if (measure(line + ell) > maxWidth) {
                while (!line.empty() && measure(line + ell) > maxWidth) popUtf8(line);
                line = trim(line);
            }
            line += ell;
        } else if (measure(line) > maxWidth) {
            while (!line.empty() && measure(line + ell) > maxWidth) popUtf8(line);
            line += ell;
        }
        lines.push_back(line);
    }
    // Texte vide ou fait d'espaces : une ligne vide quand même (les appelants lisent [0])
    if (lines.empty() && maxLines >= 1) lines.emplace_back();
    return lines;
}

std::string utf8PopBack(const std::string& s) {
    std::string o = s;
    popUtf8(o);  // octets de suite 10xxxxxx, puis le premier octet du caractère
    return o;
}

std::string typeText(const std::string& query, const std::string& text, size_t maxBytes) {
    std::string q = query;
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = (unsigned char)text[i];
        // Longueur du caractère d'après son premier octet ; octet de suite isolé ou caractère tronqué : sauté
        const size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        bool ok = n > 0 && i + n <= text.size();
        for (size_t k = 1; ok && k < n; ++k) ok = ((unsigned char)text[i + k] & 0xC0) == 0x80;
        if (!ok) {
            ++i;
            continue;
        }
        if (c == ' ') {
            if (!q.empty() && q.back() != ' ') q += ' ';
        } else if (c >= 0x20 && c != 0x7F && q.size() < maxBytes) {
            q.append(text, i, n);
        }
        i += n;
    }
    return q;
}

bool sharesWord(const std::string& query, const std::vector<std::string>& texts) {
    std::string all;
    for (auto& t : texts) all += " " + normalizeText(t) + " ";
    const std::string q = normalizeText(query);
    std::string w;
    bool comparable = false;  // au moins un mot de 3 lettres ou plus
    for (size_t i = 0; i <= q.size(); ++i) {
        if (i == q.size() || q[i] == ' ') {
            if (charCount(w) >= 3) {
                comparable = true;
                if (all.find(w) != std::string::npos) return true;
            }
            w.clear();
        } else {
            w += q[i];
        }
    }
    if (comparable) return false;
    // Rien que des mots courts (« U2 », « AC/DC », « Yo-Yo Ma ») : la requête entière, mot pour mot ; sans
    // espaces autour pour les écritures sans espaces entre les mots (CJK…). Rien à comparer : en rapport.
    if (q.empty()) return true;
    const bool ascii = std::all_of(q.begin(), q.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    return all.find(ascii ? " " + q + " " : q) != std::string::npos;
}

std::string normalizeText(const std::string& s) {
    std::string out;
    auto put = [&](const std::string& add) {
        if (add == " " && (out.empty() || out.back() == ' ')) return;
        out += add;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = s[i];
        if (c < 0x80) {
            put(std::isalnum(c) ? std::string(1, static_cast<char>(std::tolower(c))) : " ");
            continue;
        }
        size_t n;
        const unsigned long cp = utf8At(s, i, n);
        if (n == 0) {  // octet invalide : séparateur
            put(" ");
            continue;
        }
        const char base = n == 2 ? baseLetter(c, static_cast<unsigned char>(s[i + 1])) : ' ';
        i += n - 1;
        if (isSeparator(cp)) put(" ");
        else if (cp == 0x152 || cp == 0x153) put("oe");  // œ
        else if (cp == 0xC6 || cp == 0xE6) put("ae");    // æ
        else if (cp == 0xD8 || cp == 0xF8) put("o");     // ø
        else if (cp == 0xDF) put("ss");                  // ß
        else if (base != ' ') put(std::string(1, base));  // é → e
        else {
            std::string u;  // autre lettre (ð, þ, cyrillique, grec, CJK…) : gardée
            appendUtf8(u, lowerCp(cp));
            put(u);
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Note d'édition en fin de titre : « (Legacy Edition) », « [Remastered 2011] », « - Deluxe »
static bool editionNote(const std::string& inner) {
    static const char* const kWords[] = {"remaster", "deluxe", "edition", "expanded", "anniversary",
                                         "bonus",    "legacy", "reissue", "explicit"};
    const std::string n = normalizeText(inner);
    for (const char* w : kWords)
        if (n.find(w) != std::string::npos) return true;
    return false;
}

std::string shortTitle(const std::string& title) {
    std::string s = trim(title);
    for (;;) {
        if (s.empty()) break;
        size_t cut = std::string::npos, from = 0, to = 0;
        const char last = s.back();
        if (last == ')' || last == ']') {
            cut = s.rfind(last == ')' ? '(' : '[');
            if (cut != std::string::npos) from = cut + 1, to = s.size() - 1;
        } else {
            cut = s.rfind(" - ");
            if (cut != std::string::npos) from = cut + 3, to = s.size();
        }
        if (cut == std::string::npos || cut == 0) break;
        const std::string inner = s.substr(from, to - from);
        if (inner.find_first_of("()[]") != std::string::npos || !editionNote(inner)) break;  // imbriqué : gardé
        const std::string rest = trim(s.substr(0, cut));
        if (rest.empty()) break;
        s = rest;
    }
    return s;
}

}  // namespace wr
