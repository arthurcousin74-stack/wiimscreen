#pragma once

// Minimal UPnP / SOAP helpers for the WiiM (LinkPlay) "PlayQueue" service.
// Pure C++ (no Arduino headers) so it can be unit-tested on a PC.
//
// The PlayQueue service (urn:schemas-wiimu-com:service:PlayQueue:1) is not documented by
// WiiM; what is known comes from community reverse engineering (see docs/WIIM_API_NOTES.md).

#include <string>
#include <utility>
#include <vector>

namespace wr {

std::string xmlEscape(const std::string& s);
std::string xmlUnescape(const std::string& s);  // &lt; &gt; &amp; &quot; &apos; &#NN; &#xNN;

// Inner text of the first <tag>…</tag> at or after `from` (a namespace prefix such as <s:tag> is
// accepted). Returns false if absent. `end` (optional) receives the position after the closing tag.
bool xmlElement(const std::string& xml, const std::string& tag, std::string& out, size_t from = 0,
                size_t* end = nullptr);
std::vector<std::string> xmlElements(const std::string& xml, const std::string& tag);

struct UpnpService {
    std::string type;        // e.g. urn:schemas-wiimu-com:service:PlayQueue:1
    std::string controlUrl;  // as written in description.xml (often relative)
    std::string scpdUrl;
};
// Services of a device description (description.xml), all embedded devices included.
std::vector<UpnpService> parseUpnpServices(const std::string& descriptionXml);
// Action names of a service description (SCPD).
std::vector<std::string> parseScpdActions(const std::string& scpdXml);
// Arguments of one action, as "name" (in) or "->name" (out), in SCPD order.
std::vector<std::string> parseScpdArguments(const std::string& scpdXml, const std::string& action);
// "http://host:port" + url if url is relative; url unchanged if absolute.
std::string upnpUrl(const std::string& base, const std::string& url);

// SOAP envelope for `action` of `serviceType`; argument values are XML-escaped.
std::string soapBody(const std::string& serviceType, const std::string& action,
                     const std::vector<std::pair<std::string, std::string>>& args);

// One track of a queue, from its DIDL-Lite metadata.
struct QueueTrack {
    std::string title, artist, album, art;
};

// Content of a queue (QueueContext of BrowseQueue / CreateQueue, already unescaped once).
struct QueueInfo {
    bool ok = false;           // a <PlayList> was found
    std::string listName;      // queue name (to pass to PlayQueueWithIndex)
    std::string sourceName;    // e.g. Qobuz, OnlineMusic, Local…
    std::string searchUrl;     // for streaming services: where the player fetches the tracks
    std::string user;          // Login_username (account linked on the player)
    std::string quality;       // requestQuality or Quality
    int declaredTracks = -1;   // TrackNumber, -1 if absent
    int tracks = 0;            // <TrackN> elements present
    std::vector<std::string> titles;  // track titles (from the DIDL-Lite metadata), 200 at most
    std::vector<QueueTrack> items;    // same tracks with artist, album and cover, 200 at most
};
QueueInfo parseQueueContext(const std::string& xml);

// Identity of a queue, to recognise the same album/playlist again: the SearchUrl without its paging
// parameters (offset, limit) for streaming services, otherwise source + list name.
std::string queueKey(const QueueInfo& q);

// A queue saved on the remote ("favorite"), replayed later with CreateQueue + PlayQueueWithIndex.
struct Favorite {
    std::string id;      // file name of the saved QueueContext
    std::string key;     // queueKey()
    std::string title;   // album / playlist name shown on the tile
    std::string artist;
    std::string source;  // SourceName (Qobuz…)
    std::string artUrl;  // cover
};
// Index file: one favorite per line, fields separated by tabs (tabs/newlines removed from values).
std::string favoritesToText(const std::vector<Favorite>& list);
std::vector<Favorite> favoritesFromText(const std::string& text);

// New queue made from a real one (template, e.g. a Qobuz queue started by the WiiM Home app, which carries
// the source name, account and quality the player expects): ListName and SearchUrl replaced, track list
// removed (the player is expected to fetch the tracks from SearchUrl). Empty string if no <PlayList>.
std::string queueFromTemplate(const std::string& templ, const std::string& listName, const std::string& searchUrl);
// "https://host:port" part of a URL (empty if not http/https).
std::string urlOrigin(const std::string& url);
// Percent-encoding for a URL query value (RFC 3986 unreserved characters kept).
std::string urlQueryEncode(const std::string& s);

// Replaces the values of token/secret/password/signature query parameters by "***".
std::string maskSecrets(const std::string& text);
// "arthur@example.com" → "ar***@ex***" ; short names → "***".
std::string maskAccount(const std::string& name);

}  // namespace wr
