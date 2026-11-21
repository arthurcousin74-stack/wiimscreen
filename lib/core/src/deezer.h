// Deezer without Arduino: public API answers (search, user tracks and albums), profile number, cover addresses.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "catalog.h"  // QobuzAlbum

namespace wr {

// Deezer public search (api.deezer.com/search?q=…, no key): tracks with their album.
// Ids are 64-bit on every target (`long` is 32-bit on the ESP32-P4: Deezer ids above 2^31 were clamped there).
struct DeezerTrack {
    int64_t id = 0, albumId = 0;
    std::string title, artist, album, cover;
};
std::vector<DeezerTrack> parseDeezerTracks(const std::string& json);  // aussi user/<id>/tracks (même forme)
// Deezer profile number from what the user typed: the number itself, or a profile address
// (https://www.deezer.com/fr/profile/123456). Empty if none found.
std::string deezerUserId(const std::string& text);

// Deezer API (api.deezer.com/user/<profile>/albums, public): {"data":[{id,title,cover_medium,nb_tracks,
// artist{name}}]}. Same structure as QobuzAlbum (id, title, artist, image, tracks).
std::vector<QobuzAlbum> parseDeezerAlbums(const std::string& json);
// Deezer API user/<profile>/playlists (public): id of the playlist of the profile's favorite tracks
// ("is_loved_track": true), empty if none.
std::string deezerLovedPlaylist(const std::string& json);

// Address of the same cover as a baseline JPEG when the server can make one: the decoder of the device (TJpgDec)
// cannot draw progressive JPEGs. Deezer (cdn-images.dzcdn.net / e-cdns-images.dzcdn.net, name
// "<w>x<h>-<colour>-<quality>-<a>-<b>.jpg"): last field 1 instead of 0 (36 of 36 covers baseline, checked
// 2026-11-16; with 0, 22 of 36 were progressive). Other addresses are returned unchanged.
std::string baselineCoverUrl(const std::string& url);

}  // namespace wr
