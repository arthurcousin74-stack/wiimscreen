#pragma once

// Qobuz API answers (JSON) read with depth-aware helpers: an item holds several nested "title"/"name"/"id"
// keys (album, artist, label, genre…), so a plain "first occurrence" lookup picks the wrong one.
// Pure C++ (no Arduino headers), unit-tested.
//
// The remote gets the app ID and the user token from the WiiM itself (UPnP PlayQueue GetUserInfo, see
// docs/WIIM_API_NOTES.md); this file only parses.

#include <string>
#include <vector>

namespace wr {

// Raw text of the value of `key` at the top level of a JSON object (string with its quotes, number,
// object or array); empty if absent.
std::string jsonTop(const std::string& object, const std::string& key);
// Unquoted, unescaped string value (\" \\ \/ \n \uXXXX handled); numbers returned as written.
std::string jsonText(const std::string& raw);
// Top-level elements of a JSON array (raw text each).
std::vector<std::string> jsonArray(const std::string& raw);

struct QobuzTrack {
    std::string id, title, artist, albumId, album, image;
};
struct QobuzAlbum {
    std::string id, title, artist, image;
    int tracks = 0;
};
// track/search, favorite/getUserFavorites?type=tracks : {"tracks":{"items":[…]}}
std::vector<QobuzTrack> parseQobuzTracks(const std::string& json);
// album/search, favorite/getUserFavorites?type=albums : {"albums":{"items":[…]}}
std::vector<QobuzAlbum> parseQobuzAlbums(const std::string& json);

// GetUserInfo (WiiM UPnP) Result: {"clientId":…,"accessToken":…,"userName":…} — false if a field is missing
// or not usable in an HTTP header (printable ASCII only, no spaces).
struct QobuzAccess {
    std::string appId, token;
};
bool parseQobuzAccess(const std::string& json, QobuzAccess& out);

}  // namespace wr
