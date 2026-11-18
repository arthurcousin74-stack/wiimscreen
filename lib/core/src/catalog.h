// Album of a streaming catalogue as shown in the search lists. Named QobuzAlbum for history: the Deezer album
// parser (deezer.h) returns the same structure.
#pragma once

#include <string>

namespace wr {

struct QobuzAlbum {
    std::string id, title, artist, image;
    int tracks = 0;
};

}  // namespace wr
