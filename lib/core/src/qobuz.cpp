#include "qobuz.h"

#include "core.h"
#include "upnp.h"

#include <cctype>
#include <cstdlib>

namespace wr {

namespace {

bool headerSafe(const std::string& v) {
    if (v.empty() || v.size() > 512) return false;
    for (unsigned char c : v)
        if (c <= 0x20 || c >= 0x7f) return false;
    return true;
}

}  // namespace

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
    a.user = jsonText(jsonTop(json, "userName"));
    if (!headerSafe(a.appId) || !headerSafe(a.token)) return false;
    out = a;
    return true;
}

std::string qobuzQueueModel(const std::string& user) {
    if (user.empty()) return "";
    return "<?xml version=\"1.0\"?><PlayList><ListName>Qobuz</ListName><ListInfo><SourceName>Qobuz</SourceName>"
           "<SearchUrl></SearchUrl><Login_username>" +
           xmlEscape(user) +
           "</Login_username><MarkSearch>0</MarkSearch><TrackNumber>0</TrackNumber><TotalNumber>0</TotalNumber>"
           "<Quality>0</Quality><UpdateTime>0</UpdateTime><LastPlayIndex>1</LastPlayIndex><UserId>0</UserId>"
           "<StationBackup>0</StationBackup></ListInfo><Tracks></Tracks></PlayList>";
}

std::string qobuzAlbumUrl(const std::string& albumId) {
    return "https://www.qobuz.com/api.json/0.2/album/get?album_id=" + urlQueryEncode(albumId) + "&offset=0&limit=50";
}

std::string qobuzAlbumKey(const std::string& albumId) {
    QueueInfo q;
    q.searchUrl = qobuzAlbumUrl(albumId);
    return queueKey(q);
}

}  // namespace wr
