#include "upnp.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace wr {

std::string xmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '&': o += "&amp;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&apos;"; break;
            default: o += c;
        }
    }
    return o;
}

static void appendUtf8(std::string& o, unsigned long cp) {
    if (cp < 0x80) {
        o += static_cast<char>(cp);
    } else if (cp < 0x800) {
        o += static_cast<char>(0xC0 | (cp >> 6));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        o += static_cast<char>(0xE0 | (cp >> 12));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        o += static_cast<char>(0xF0 | (cp >> 18));
        o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string xmlUnescape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            o += s[i];
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            o += s[i];
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "lt") o += '<';
        else if (ent == "gt") o += '>';
        else if (ent == "amp") o += '&';
        else if (ent == "quot") o += '"';
        else if (ent == "apos") o += '\'';
        else if (ent.size() > 1 && ent[0] == '#') {
            bool hex = ent[1] == 'x' || ent[1] == 'X';
            char* endp = nullptr;
            const char* digits = ent.c_str() + (hex ? 2 : 1);
            unsigned long cp = std::strtoul(digits, &endp, hex ? 16 : 10);
            if (endp && *endp == 0 && *digits) appendUtf8(o, cp);
            else o += s.substr(i, semi - i + 1);
        } else {
            o += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return o;
}

// Finds "<tag" or "<prefix:tag" followed by '>', ' ' or '/'
static size_t findOpen(const std::string& xml, const std::string& tag, size_t from, size_t& contentStart,
                       std::string& fullName) {
    size_t p = from;
    while ((p = xml.find('<', p)) != std::string::npos) {
        size_t q = p + 1;
        if (q < xml.size() && (xml[q] == '/' || xml[q] == '?' || xml[q] == '!')) {
            ++p;
            continue;
        }
        size_t nameEnd = q;
        while (nameEnd < xml.size() && !std::isspace(static_cast<unsigned char>(xml[nameEnd])) && xml[nameEnd] != '>' &&
               xml[nameEnd] != '/')
            ++nameEnd;
        std::string name = xml.substr(q, nameEnd - q);
        size_t colon = name.find(':');
        std::string local = colon == std::string::npos ? name : name.substr(colon + 1);
        if (local == tag) {
            size_t gt = xml.find('>', nameEnd);
            if (gt == std::string::npos) return std::string::npos;
            fullName = name;
            contentStart = xml[gt - 1] == '/' ? std::string::npos : gt + 1;  // <tag/> : vide
            return p;
        }
        ++p;
    }
    return std::string::npos;
}

bool xmlElement(const std::string& xml, const std::string& tag, std::string& out, size_t from, size_t* end) {
    size_t contentStart = 0;
    std::string fullName;
    size_t p = findOpen(xml, tag, from, contentStart, fullName);
    if (p == std::string::npos) return false;
    if (contentStart == std::string::npos) {  // élément vide
        out.clear();
        if (end) *end = xml.find('>', p) + 1;
        return true;
    }
    const std::string close = "</" + fullName + ">";
    size_t c = xml.find(close, contentStart);
    if (c == std::string::npos) return false;
    out = xml.substr(contentStart, c - contentStart);
    if (end) *end = c + close.size();
    return true;
}

std::vector<std::string> xmlElements(const std::string& xml, const std::string& tag) {
    std::vector<std::string> out;
    size_t pos = 0, end = 0;
    std::string v;
    while (xmlElement(xml, tag, v, pos, &end)) {
        out.push_back(v);
        pos = end;
    }
    return out;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<UpnpService> parseUpnpServices(const std::string& descriptionXml) {
    std::vector<UpnpService> out;
    for (const std::string& block : xmlElements(descriptionXml, "service")) {
        UpnpService s;
        std::string v;
        if (xmlElement(block, "serviceType", v)) s.type = trim(xmlUnescape(v));
        if (xmlElement(block, "controlURL", v)) s.controlUrl = trim(xmlUnescape(v));
        if (xmlElement(block, "SCPDURL", v)) s.scpdUrl = trim(xmlUnescape(v));
        if (!s.type.empty()) out.push_back(s);
    }
    return out;
}

std::vector<std::string> parseScpdActions(const std::string& scpdXml) {
    std::vector<std::string> out;
    for (const std::string& action : xmlElements(scpdXml, "action")) {
        std::string name;
        if (xmlElement(action, "name", name)) out.push_back(trim(name));  // le premier <name> : celui de l'action
    }
    return out;
}

std::string upnpUrl(const std::string& base, const std::string& url) {
    if (url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0) return url;
    if (url.empty() || url[0] != '/') return base + "/" + url;
    return base + url;
}

std::string soapBody(const std::string& serviceType, const std::string& action,
                     const std::vector<std::pair<std::string, std::string>>& args) {
    std::string b =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
        action + " xmlns:u=\"" + serviceType + "\">";
    for (auto& a : args) b += "<" + a.first + ">" + xmlEscape(a.second) + "</" + a.first + ">";
    b += "</u:" + action + "></s:Body></s:Envelope>";
    return b;
}

QueueInfo parseQueueContext(const std::string& xml) {
    QueueInfo q;
    std::string list;
    if (!xmlElement(xml, "PlayList", list)) return q;
    q.ok = true;
    std::string v, info;
    if (xmlElement(list, "ListName", v)) q.listName = trim(xmlUnescape(v));
    if (xmlElement(list, "ListInfo", info)) {
        if (xmlElement(info, "SourceName", v)) q.sourceName = trim(xmlUnescape(v));
        if (xmlElement(info, "SearchUrl", v)) q.searchUrl = trim(xmlUnescape(v));
        if (xmlElement(info, "Login_username", v)) q.user = trim(xmlUnescape(v));
        if (xmlElement(info, "requestQuality", v) || xmlElement(info, "Quality", v)) q.quality = trim(v);
        if (xmlElement(info, "TrackNumber", v)) q.declaredTracks = std::atoi(trim(v).c_str());
    }
    std::string tracks;
    if (xmlElement(list, "Tracks", tracks)) {
        // <Track1>…</Track1><Track2>…
        size_t pos = 0;  // chaque piste est cherchée après la précédente (files de centaines de Ko)
        for (int n = 1; n < 10000; ++n) {
            std::string t;
            size_t end = 0;
            if (!xmlElement(tracks, "Track" + std::to_string(n), t, pos, &end)) break;
            pos = end;
            ++q.tracks;
            if (q.items.size() < 200) {
                QueueTrack item;
                std::string meta;
                if (xmlElement(t, "Metadata", meta)) {
                    // DIDL-Lite échappé une ou deux fois selon l'émetteur
                    std::string didl = xmlUnescape(meta);
                    if (didl.find("&lt;") != std::string::npos) didl = xmlUnescape(didl);
                    std::string v;
                    if (xmlElement(didl, "title", v)) item.title = trim(xmlUnescape(v));
                    if (xmlElement(didl, "artist", v) || xmlElement(didl, "creator", v)) item.artist = trim(xmlUnescape(v));
                    if (xmlElement(didl, "album", v)) item.album = trim(xmlUnescape(v));
                    if (xmlElement(didl, "albumArtURI", v)) item.art = trim(xmlUnescape(v));
                }
                if (!item.title.empty()) q.titles.push_back(item.title);
                q.items.push_back(item);
            }
        }
    }
    return q;
}

std::string queueKey(const QueueInfo& q) {
    if (!q.searchUrl.empty()) {
        std::string u = q.searchUrl;
        size_t qm = u.find('?');
        if (qm == std::string::npos) return u;
        std::string base = u.substr(0, qm + 1), kept;
        size_t p = qm + 1;
        while (p <= u.size()) {
            size_t e = u.find('&', p);
            if (e == std::string::npos) e = u.size();
            std::string param = u.substr(p, e - p);
            std::string name = param.substr(0, param.find('='));
            if (!param.empty() && name != "offset" && name != "limit") kept += (kept.empty() ? "" : "&") + param;
            p = e + 1;
        }
        return base + kept;
    }
    return q.sourceName + "|" + q.listName;
}

static std::string clean(const std::string& s) {
    std::string o;
    for (char c : s) o += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    return o;
}

std::string favoritesToText(const std::vector<Favorite>& list) {
    std::string o;
    for (auto& f : list)
        o += clean(f.id) + "\t" + clean(f.key) + "\t" + clean(f.title) + "\t" + clean(f.artist) + "\t" +
             clean(f.source) + "\t" + clean(f.artUrl) + "\n";
    return o;
}

std::vector<Favorite> favoritesFromText(const std::string& text) {
    std::vector<Favorite> out;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> f;
        size_t a = 0;
        while (true) {
            size_t t = line.find('\t', a);
            f.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
            if (t == std::string::npos) break;
            a = t + 1;
        }
        if (f.size() < 6 || f[0].empty()) continue;  // ligne abîmée : ignorée
        out.push_back(Favorite{f[0], f[1], f[2], f[3], f[4], f[5]});
    }
    return out;
}

// Remplace le contenu du premier <tag>…</tag> (ou l'ajoute à la fin de `parentTag`)
static void setElement(std::string& xml, const std::string& tag, const std::string& escapedValue,
                       const std::string& parentTag) {
    const std::string open = "<" + tag + ">", close = "</" + tag + ">";
    size_t a = xml.find(open);
    if (a != std::string::npos) {
        size_t b = xml.find(close, a);
        if (b != std::string::npos) {
            xml.replace(a + open.size(), b - a - open.size(), escapedValue);
            return;
        }
    }
    size_t e = xml.find("</" + parentTag + ">");
    if (e != std::string::npos) xml.insert(e, open + escapedValue + close);
}

std::string queueFromTemplate(const std::string& templ, const std::string& listName, const std::string& searchUrl) {
    size_t a = templ.find("<PlayList>");
    size_t b = templ.find("</PlayList>");
    if (a == std::string::npos || b == std::string::npos || b < a) return "";
    std::string x = templ.substr(a, b + 11 - a);
    // Pistes retirées : le lecteur les lit lui-même depuis SearchUrl
    size_t t = x.find("<Tracks>");
    size_t te = x.find("</Tracks>");
    if (t != std::string::npos && te != std::string::npos && te > t) x.erase(t, te + 9 - t);
    else if ((t = x.find("<Tracks/>")) != std::string::npos) x.erase(t, 9);
    setElement(x, "ListName", xmlEscape(listName), "PlayList");
    if (x.find("<ListInfo>") == std::string::npos) x.insert(x.find("</PlayList>"), "<ListInfo></ListInfo>");
    setElement(x, "SearchUrl", xmlEscape(searchUrl), "ListInfo");
    size_t n = x.find("<TrackNumber>");
    if (n != std::string::npos) {
        size_t ne = x.find("</TrackNumber>", n);
        if (ne != std::string::npos) x.erase(n, ne + 14 - n);
    }
    return "<?xml version=\"1.0\"?>" + x;
}

std::string urlQueryEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

std::string maskSecrets(const std::string& text) {
    static const char* kKeys[] = {"user_auth_token", "token", "secret", "password", "request_sig", "sig"};
    std::string o = text;
    for (const char* key : kKeys) {
        const std::string k = std::string(key) + "=";
        size_t p = 0;
        while ((p = o.find(k, p)) != std::string::npos) {
            bool boundary = p == 0 || o[p - 1] == '?' || o[p - 1] == '&' || o[p - 1] == ';' || o[p - 1] == ' ';
            size_t v = p + k.size();
            if (!boundary) {
                p = v;
                continue;
            }
            size_t e = v;
            while (e < o.size() && o[e] != '&' && o[e] != '<' && o[e] != '"' && o[e] != ' ' && o[e] != '\n') ++e;
            if (e > v && o.compare(v, e - v, "***") != 0) o.replace(v, e - v, "***");
            p = v + 3;
        }
    }
    return o;
}

std::string maskAccount(const std::string& name) {
    if (name.size() < 4) return name.empty() ? "" : "***";
    size_t at = name.find('@');
    if (at == std::string::npos) return name.substr(0, 2) + "***";
    std::string domain = name.substr(at + 1);
    return name.substr(0, std::min<size_t>(2, at)) + "***@" + domain.substr(0, std::min<size_t>(2, domain.size())) +
           "***";
}

}  // namespace wr
