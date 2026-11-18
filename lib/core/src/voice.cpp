#include "voice.h"

#include "qobuz.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace wr {

namespace {

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

// Numéro Deezer (nombre JSON ou chaîne de chiffres) sur 64 bits ; 0 si absent ou invalide
int64_t deezerId(const std::string& raw) {
    const std::string t = jsonText(raw);
    if (t.empty() || t.size() > 18) return 0;
    int64_t v = 0;
    for (char c : t) {
        if (c < '0' || c > '9') return 0;
        v = v * 10 + (c - '0');
    }
    return v;
}

}  // namespace

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

std::vector<DeezerTrack> parseDeezerTracks(const std::string& json) {
    std::vector<DeezerTrack> out;
    // Clés lues au bon niveau (jsonTop) : un titre « album » ou « artist » n'est pas pris pour une clé
    for (const std::string& obj : jsonArray(jsonTop(json, "data"))) {
        DeezerTrack t;
        t.id = deezerId(jsonTop(obj, "id"));
        t.title = jsonText(jsonTop(obj, "title"));
        t.artist = jsonText(jsonTop(jsonTop(obj, "artist"), "name"));
        const std::string album = jsonTop(obj, "album");
        t.albumId = deezerId(jsonTop(album, "id"));
        t.album = jsonText(jsonTop(album, "title"));
        const std::string c = jsonText(jsonTop(album, "cover_medium"));
        if (isHttpUrl(c)) t.cover = c;
        if (t.id > 0 && !t.title.empty()) out.push_back(t);
        if (out.size() >= 100) break;
    }
    return out;
}

std::string deezerUserId(const std::string& text) {
    auto digitsAt = [&](size_t i) {
        size_t e = i;
        while (e < text.size() && std::isdigit((unsigned char)text[e])) ++e;
        return text.substr(i, e - i);
    };
    size_t a = 0, b = text.size();
    while (a < b && std::isspace((unsigned char)text[a])) ++a;
    while (b > a && std::isspace((unsigned char)text[b - 1])) --b;
    std::string id;
    const size_t p = text.find("profile/");
    if (p != std::string::npos) id = digitsAt(p + 8);
    else if (b > a && digitsAt(a).size() == b - a) id = text.substr(a, b - a);
    return id.size() >= 1 && id.size() <= 15 ? id : "";
}

void wavHeader(uint8_t h[44], uint32_t dataBytes, uint32_t sampleRate) {
    auto le32 = [&](int at, uint32_t v) {
        for (int k = 0; k < 4; ++k) h[at + k] = static_cast<uint8_t>(v >> (8 * k));
    };
    auto le16 = [&](int at, uint16_t v) {
        h[at] = static_cast<uint8_t>(v);
        h[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    std::memcpy(h, "RIFF", 4);
    le32(4, 36 + dataBytes);
    std::memcpy(h + 8, "WAVEfmt ", 8);
    le32(16, 16);           // taille du bloc fmt
    le16(20, 1);            // PCM
    le16(22, 1);            // mono
    le32(24, sampleRate);
    le32(28, sampleRate * 2);
    le16(32, 2);            // octets par échantillon
    le16(34, 16);           // bits
    std::memcpy(h + 36, "data", 4);
    le32(40, dataBytes);
}

}  // namespace wr
