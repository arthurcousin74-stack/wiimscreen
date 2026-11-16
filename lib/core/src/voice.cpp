#include "voice.h"

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

// Découpe d'un tableau JSON d'objets plats en tenant compte des chaînes
std::vector<std::string> jsonObjects(const std::string& json) {
    std::vector<std::string> out;
    int depth = 0;
    bool inStr = false, esc = false;
    size_t start = 0;
    for (size_t i = 0; i < json.size(); ++i) {
        char c = json[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{') {
            if (depth++ == 0) start = i;
        } else if (c == '}' && depth > 0) {
            if (--depth == 0) out.push_back(json.substr(start, i - start + 1));
        }
    }
    return out;
}

}  // namespace

bool sharesWord(const std::string& query, const std::vector<std::string>& texts) {
    std::string all;
    for (auto& t : texts) all += " " + normalizeText(t) + " ";
    std::string q = normalizeText(query), w;
    for (size_t i = 0; i <= q.size(); ++i) {
        if (i == q.size() || q[i] == ' ') {
            if (w.size() >= 3 && all.find(w) != std::string::npos) return true;
            w.clear();
        } else {
            w += q[i];
        }
    }
    return false;
}

std::string normalizeText(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = s[i];
        char add = ' ';
        if (c < 0x80) {
            if (std::isalnum(c)) add = static_cast<char>(std::tolower(c));
            else if (c == '-' || c == '\'') add = ' ';
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            unsigned cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
            if (cp == 0x152 || cp == 0x153) out += 'o', add = 'e';  // œ → oe
            else add = baseLetter(c, static_cast<unsigned char>(s[i + 1]));
            ++i;
        } else {
            while (i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) ++i;
        }
        if (add == ' ' && (out.empty() || out.back() == ' ')) continue;
        out += add;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<DeezerTrack> parseDeezerTracks(const std::string& json) {
    std::vector<DeezerTrack> out;
    size_t d = json.find("\"data\"");
    if (d == std::string::npos) return out;
    d = json.find('[', d);
    if (d == std::string::npos) return out;
    for (auto& obj : jsonObjects(json.substr(d + 1))) {
        DeezerTrack t;
        jsonInt(obj, "id", t.id);  // premier « id » : celui du morceau
        jsonString(obj, "title", t.title);
        size_t a = obj.find("\"artist\"");
        if (a != std::string::npos) jsonString(obj.substr(a), "name", t.artist);
        size_t al = obj.find("\"album\"");
        if (al != std::string::npos) {
            const std::string album = obj.substr(al);
            jsonInt(album, "id", t.albumId);
            jsonString(album, "title", t.album);
            std::string c;
            if (jsonString(album, "cover_medium", c) && isHttpUrl(c)) t.cover = c;
        }
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

std::string urlEncode(const std::string& s, bool keepSlashColon) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' ||
            (keepSlashColon && (c == '/' || c == ':'))) {
            o += static_cast<char>(c);
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
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
