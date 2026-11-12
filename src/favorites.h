#pragma once

// Favorites saved on the remote: a copy of the WiiM play queue (UPnP PlayQueue QueueContext)
// taken while an album or playlist plays, replayed later. Stored in LittleFS (partition "spiffs"):
// /fav/index.txt (wr::favoritesToText) and one /fav/<id>.xml per favorite.
// Thread-safe: the WiiM task adds, the main loop lists and removes.

#include <string>
#include <vector>

#include "upnp.h"

namespace fav {

constexpr size_t kMax = 24;

bool begin();                                   // mounts (formats on first use); false if unavailable
std::vector<wr::Favorite> list();
// Adds or replaces (same key) a favorite; `f.id` is assigned. False on storage error or when full.
bool add(wr::Favorite& f, const std::string& queueContext, std::string& error);
bool remove(const std::string& id);
bool context(const std::string& id, std::string& out);
uint32_t revision();                            // changes after each add/remove
// Model of a Qobuz queue (account, quality, source as the WiiM expects them), without tracks:
// kept from the last Qobuz queue read on the WiiM, used to build search queues.
bool saveQobuzTemplate(const std::string& queueContext);
bool qobuzTemplate(std::string& out);
// Same for another source (Deezer…): /fav/model_<source>.xml, without tracks
bool saveSourceModel(const std::string& source, const std::string& queueContext);
bool sourceModel(const std::string& source, std::string& out);

}  // namespace fav
