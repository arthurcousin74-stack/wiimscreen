#include "deezer.h"

#include "http.h"
#include "json.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace wr {

namespace {

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

std::vector<QobuzAlbum> parseDeezerAlbums(const std::string& json) {
    std::vector<QobuzAlbum> out;
    for (const std::string& it : jsonArray(jsonTop(json, "data"))) {
        QobuzAlbum a;
        a.id = jsonText(jsonTop(it, "id"));
        a.title = jsonText(jsonTop(it, "title"));
        a.artist = jsonText(jsonTop(jsonTop(it, "artist"), "name"));
        a.image = jsonText(jsonTop(it, "cover_medium"));
        if (a.image.empty()) a.image = jsonText(jsonTop(it, "cover"));
        a.tracks = std::atoi(jsonText(jsonTop(it, "nb_tracks")).c_str());
        if (!a.id.empty() && !a.title.empty()) out.push_back(a);
        if (out.size() >= 100) break;
    }
    return out;
}

std::string deezerLovedPlaylist(const std::string& json) {
    for (const std::string& it : jsonArray(jsonTop(json, "data")))
        if (jsonTop(it, "is_loved_track") == "true") {
            const std::string id = jsonText(jsonTop(it, "id"));
            if (!id.empty() && id.find_first_not_of("0123456789") == std::string::npos) return id;
        }
    return "";
}

std::string baselineCoverUrl(const std::string& url) {
    static const char* hosts[] = {"https://cdn-images.dzcdn.net/", "https://e-cdns-images.dzcdn.net/",
                                  "http://cdn-images.dzcdn.net/", "http://e-cdns-images.dzcdn.net/"};
    bool deezer = false;
    for (const char* h : hosts) deezer |= url.compare(0, std::strlen(h), h) == 0;
    if (!deezer || url.find('?') != std::string::npos) return url;
    const size_t slash = url.rfind('/');
    std::string name = url.substr(slash + 1);
    if (name.size() < 8 || name.compare(name.size() - 4, 4, ".jpg") != 0) return url;
    // <w>x<h>-<colour>-<quality>-<a>-<b>.jpg : exactly four dashes, last field "0"
    int dashes = 0;
    for (char c : name) dashes += c == '-';
    const size_t last = name.rfind('-');
    if (dashes != 4 || name.compare(last, std::string::npos, "-0.jpg") != 0) return url;
    return url.substr(0, slash + 1) + name.substr(0, last) + "-1.jpg";
}

}  // namespace wr
