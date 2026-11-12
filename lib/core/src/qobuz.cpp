#include "qobuz.h"

#include <cctype>
#include <cstdlib>

namespace wr {

namespace {

// End of the JSON value starting at `i` (index just after it)
size_t valueEnd(const std::string& s, size_t i) {
    if (i >= s.size()) return i;
    if (s[i] == '"') {
        for (size_t k = i + 1; k < s.size(); ++k) {
            if (s[k] == '\\') ++k;
            else if (s[k] == '"') return k + 1;
        }
        return s.size();
    }
    if (s[i] == '{' || s[i] == '[') {
        int depth = 0;
        bool str = false;
        for (size_t k = i; k < s.size(); ++k) {
            char c = s[k];
            if (str) {
                if (c == '\\') ++k;
                else if (c == '"') str = false;
                continue;
            }
            if (c == '"') str = true;
            else if (c == '{' || c == '[') ++depth;
            else if ((c == '}' || c == ']') && --depth == 0) return k + 1;
        }
        return s.size();
    }
    size_t k = i;
    while (k < s.size() && s[k] != ',' && s[k] != '}' && s[k] != ']' && !std::isspace((unsigned char)s[k])) ++k;
    return k;
}

size_t skipSpace(const std::string& s, size_t i) {
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    return i;
}

void appendUtf8(std::string& o, unsigned cp) {
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) o += char(0xC0 | (cp >> 6)), o += char(0x80 | (cp & 0x3F));
    else o += char(0xE0 | (cp >> 12)), o += char(0x80 | ((cp >> 6) & 0x3F)), o += char(0x80 | (cp & 0x3F));
}

bool headerSafe(const std::string& v) {
    if (v.empty() || v.size() > 512) return false;
    for (unsigned char c : v)
        if (c <= 0x20 || c >= 0x7f) return false;
    return true;
}

}  // namespace

std::string jsonTop(const std::string& o, const std::string& key) {
    size_t i = skipSpace(o, 0);
    if (i >= o.size() || o[i] != '{') return "";
    ++i;
    while (i < o.size()) {
        i = skipSpace(o, i);
        if (i >= o.size() || o[i] == '}') return "";
        if (o[i] == ',') {
            ++i;
            continue;
        }
        if (o[i] != '"') return "";
        size_t ke = valueEnd(o, i);
        std::string k = jsonText(o.substr(i, ke - i));
        i = skipSpace(o, ke);
        if (i >= o.size() || o[i] != ':') return "";
        i = skipSpace(o, i + 1);
        size_t ve = valueEnd(o, i);
        if (k == key) return o.substr(i, ve - i);
        i = ve;
    }
    return "";
}

std::string jsonText(const std::string& raw) {
    if (raw.size() < 2 || raw[0] != '"') return raw == "null" ? "" : raw;
    std::string o;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        char c = raw[i];
        if (c != '\\' || i + 2 >= raw.size()) {
            o += c;
            continue;
        }
        char e = raw[++i];
        switch (e) {
            case 'n': o += '\n'; break;
            case 't': o += '\t'; break;
            case 'r': break;
            case 'u':
                if (i + 4 < raw.size()) {
                    appendUtf8(o, (unsigned)std::strtoul(raw.substr(i + 1, 4).c_str(), nullptr, 16));
                    i += 4;
                }
                break;
            default: o += e;  // \" \\ \/
        }
    }
    return o;
}

std::vector<std::string> jsonArray(const std::string& raw) {
    std::vector<std::string> out;
    size_t i = skipSpace(raw, 0);
    if (i >= raw.size() || raw[i] != '[') return out;
    ++i;
    while (i < raw.size()) {
        i = skipSpace(raw, i);
        if (i >= raw.size() || raw[i] == ']') break;
        if (raw[i] == ',') {
            ++i;
            continue;
        }
        size_t e = valueEnd(raw, i);
        out.push_back(raw.substr(i, e - i));
        i = e;
    }
    return out;
}

std::vector<QobuzTrack> parseQobuzTracks(const std::string& json) {
    std::vector<QobuzTrack> out;
    for (const std::string& it : jsonArray(jsonTop(jsonTop(json, "tracks"), "items"))) {
        QobuzTrack t;
        t.id = jsonText(jsonTop(it, "id"));
        t.title = jsonText(jsonTop(it, "title"));
        const std::string version = jsonText(jsonTop(it, "version"));
        if (!version.empty()) t.title += " (" + version + ")";
        t.artist = jsonText(jsonTop(jsonTop(it, "performer"), "name"));
        const std::string album = jsonTop(it, "album");
        t.albumId = jsonText(jsonTop(album, "id"));
        t.album = jsonText(jsonTop(album, "title"));
        if (t.artist.empty()) t.artist = jsonText(jsonTop(jsonTop(album, "artist"), "name"));
        t.image = jsonText(jsonTop(jsonTop(album, "image"), "small"));
        if (!t.id.empty() && !t.title.empty()) out.push_back(t);
        if (out.size() >= 100) break;
    }
    return out;
}

std::vector<QobuzAlbum> parseQobuzAlbums(const std::string& json) {
    std::vector<QobuzAlbum> out;
    for (const std::string& it : jsonArray(jsonTop(jsonTop(json, "albums"), "items"))) {
        QobuzAlbum a;
        a.id = jsonText(jsonTop(it, "id"));
        a.title = jsonText(jsonTop(it, "title"));
        a.artist = jsonText(jsonTop(jsonTop(it, "artist"), "name"));
        const std::string img = jsonTop(it, "image");
        a.image = jsonText(jsonTop(img, "small"));
        if (a.image.empty()) a.image = jsonText(jsonTop(img, "thumbnail"));
        a.tracks = std::atoi(jsonText(jsonTop(it, "tracks_count")).c_str());
        if (!a.id.empty() && !a.title.empty()) out.push_back(a);
        if (out.size() >= 100) break;
    }
    return out;
}

bool parseQobuzAccess(const std::string& json, QobuzAccess& out) {
    QobuzAccess a;
    a.appId = jsonText(jsonTop(json, "clientId"));
    a.token = jsonText(jsonTop(json, "accessToken"));
    if (!headerSafe(a.appId) || !headerSafe(a.token)) return false;
    out = a;
    return true;
}

}  // namespace wr
