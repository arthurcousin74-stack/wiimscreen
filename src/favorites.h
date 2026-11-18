#pragma once

// Favorites saved on the remote: a copy of the WiiM play queue (UPnP PlayQueue QueueContext)
// taken while an album or playlist plays, replayed later. Stored in LittleFS (partition "spiffs"):
// /fav/index.txt (wr::favoritesToText) and one /fav/<id>.xml per favorite.
// Thread-safe: the WiiM task adds, the main loop lists and removes.

#include <cstdint>
#include <string>
#include <vector>

#include "upnp.h"

namespace fav {

constexpr size_t kMax = 24;
// Largest queue copy kept (~190 tracks with their metadata; the partition holds a few MB for all favorites). Queues
// with a SearchUrl are kept without their tracks (the WiiM fetches them again), so only the others can reach it.
constexpr size_t kMaxContext = 256 * 1024;

bool begin();                                   // mounts (formats on first use); false if unavailable
std::vector<wr::Favorite> list();
// Adds or replaces (same key) a favorite; `f.id` is assigned (a new one also on replace: the old file stays whole until
// the new one and the index are written). False on storage error, when full, or above kMaxContext.
bool add(wr::Favorite& f, const std::string& queueContext, std::string& error);
bool remove(const std::string& id);
bool context(const std::string& id, std::string& out);
// Model of a Qobuz queue (account, quality, source as the WiiM expects them), without tracks:
// kept from the last Qobuz queue read on the WiiM, used to build search queues.
bool saveQobuzTemplate(const std::string& queueContext);
bool qobuzTemplate(std::string& out);
// Same for another source (Deezer…): /fav/model_<source>.xml, without tracks
bool saveSourceModel(const std::string& source, const std::string& queueContext);
bool sourceModel(const std::string& source, std::string& out);
bool hasDeezerModel();  // kept in memory: no storage access, the main loop calls these
bool hasQobuzModel();
// Note kept while a search through a WiiM queue runs (wr::searchUndoText: sound and shuffle to put back), read at
// the next start if the remote restarted in the middle. No secret in it. An empty text removes the note.
bool saveSearchUndo(const std::string& text);
bool searchUndo(std::string& out);
// Action automatique faite une seule fois (tuile Flow) : doneOnce() vrai si markOnce() a déjà été appelé pour cette
// étiquette (petit fichier marqueur), ou sans stockage. Marquer seulement après la réussite : retirée, la tuile ne
// revient pas ; refusée (favoris pleins), elle pourra être ajoutée plus tard.
bool doneOnce(const std::string& tag);
bool markOnce(const std::string& tag);
// Copie de secours des réglages (/settings.bak) : LittleFS est loin après le firmware, un flash USB complet
// (image fusionnée à 0x0, qui réécrit la NVS) ne l'efface pas. Mêmes données que la NVS, non chiffrées.
bool saveSettingsBackup(const std::string& text);
bool settingsBackup(std::string& out);
// Remise à zéro : formate la partition (favoris, modèles de files, copie des réglages). Redémarrer ensuite.
bool eraseAll();

}  // namespace fav
