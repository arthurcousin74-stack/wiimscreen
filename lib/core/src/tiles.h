// Tile history and order without hardware: when each tile was last played, how often, which ones are pinned.
// Stored as text in LittleFS (/fav/tiles.txt, src/storage/favorites.cpp); the app sorts the strip with tileOrder().
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "wiim_api.h"

namespace wr {

// One tile seen in the history. last: value of TileStats::seq at its last play (0 = never played).
// pin: 0 = not pinned, else its place among the pinned tiles (smaller first).
struct TileUse {
    std::string key;
    uint32_t last = 0, count = 0, pin = 0;
};

struct TileStats {
    static constexpr size_t kMax = 200;  // entries kept; the oldest unpinned one goes first
    uint32_t seq = 0;                    // play counter: no clock needed, only the order of the plays
    std::vector<TileUse> uses;

    const TileUse* find(const std::string& key) const;
    void played(const std::string& key);            // last = ++seq, count + 1
    void setPin(const std::string& key, bool on);   // on: after the other pinned tiles
    bool pinned(const std::string& key) const;
};

// Key of a tile in the history: the favorite id ("qobuz-album:…", a saved queue, a list), else "preset:<slot>"
// (a WiiM preset keeps its number). Empty when the key cannot be stored (tab or new line in it).
std::string tileKey(const Preset& p);

// Text file: "seq\t<n>" then one "key\tlast\tcount\tpin" line per tile. Bad lines are skipped.
std::string tileStatsToText(const TileStats& s);
TileStats tileStatsFromText(const std::string& text);

// Display order of the tiles (indexes into keys): pinned tiles first (by pin), then, with recentFirst, the played
// ones (most recent first), then the others in their given order. Every index appears once.
std::vector<size_t> tileOrder(const std::vector<std::string>& keys, const TileStats& s, bool recentFirst);

}  // namespace wr
