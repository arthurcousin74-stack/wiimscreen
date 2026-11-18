#pragma once

// Qobuz API answers (JSON) read with depth-aware helpers: an item holds several nested "title"/"name"/"id"
// keys (album, artist, label, genre…), so a plain "first occurrence" lookup picks the wrong one.
// Pure C++ (no Arduino headers), unit-tested.
//
// The remote gets the app ID and the user token from the WiiM itself (UPnP PlayQueue GetUserInfo, see
// docs/WIIM_API_NOTES.md); this file only parses.

#include <string>
#include <vector>

#include "catalog.h"  // QobuzAlbum
#include "deezer.h"   // parseDeezerAlbums (declared here before the split)
#include "json.h"     // jsonTop, jsonText, jsonArray, jsonValuesOf (declared here before the split)

namespace wr {

struct QobuzTrack {
    std::string id, title, artist, albumId, album, image;
};
// track/search, favorite/getUserFavorites?type=tracks : {"tracks":{"items":[…]}}
std::vector<QobuzTrack> parseQobuzTracks(const std::string& json);
// album/search, favorite/getUserFavorites?type=albums : {"albums":{"items":[…]}}
std::vector<QobuzAlbum> parseQobuzAlbums(const std::string& json);

// GetUserInfo (WiiM UPnP) Result: {"clientId":…,"accessToken":…,"userName":…} — false if a field is missing
// or not usable in an HTTP header (printable ASCII only, no spaces).
// `user` (userName, the account name) is kept for the Qobuz queue model below; never log it.
// Seen on a real WiiM (2026-11-16): this token works for track/album search, but Qobuz answers 401 « User
// authentication is required » for favorite/getUserFavorites and user/get (headers, query or Bearer).
struct QobuzAccess {
    std::string appId, token, user;
};
bool parseQobuzAccess(const std::string& json, QobuzAccess& out);

// Model of a WiiM Qobuz queue (PlayList, no tracks) for queueFromTemplate, built from the account name, the same
// fields as the queues the WiiM stores itself (BrowseQueue, 2026-11-16). Empty if `user` is empty.
std::string qobuzQueueModel(const std::string& user);

// SearchUrl of a Qobuz album queue (the WiiM fetches the tracks itself), and the favorite key (wr::queueKey) of a
// queue with this SearchUrl: a ♥ album saved from the search shares its key with the same album copied while it plays.
std::string qobuzAlbumUrl(const std::string& albumId);
std::string qobuzAlbumKey(const std::string& albumId);

}  // namespace wr
