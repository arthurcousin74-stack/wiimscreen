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
// "http://host:port" + url if url is relative; url unchanged if absolute.
std::string upnpUrl(const std::string& base, const std::string& url);

// SOAP envelope for `action` of `serviceType`; argument values are XML-escaped.
std::string soapBody(const std::string& serviceType, const std::string& action,
                     const std::vector<std::pair<std::string, std::string>>& args);

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
    std::vector<std::string> titles;  // first track titles (from the DIDL-Lite metadata)
};
QueueInfo parseQueueContext(const std::string& xml);

// Replaces the values of token/secret/password/signature query parameters by "***".
std::string maskSecrets(const std::string& text);
// "arthur@example.com" → "ar***@ex***" ; short names → "***".
std::string maskAccount(const std::string& name);

}  // namespace wr
