#include "upnp.h"

#include "core.h"
#include "text.h"  // trim, hexVal, appendUtf8

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

std::string xmlUnescape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            o += s[i];
            continue;
        }
        // ';' cherché sur 10 caractères au plus : pas de parcours jusqu'à la fin pour chaque '&'
        size_t semi = std::string::npos;
        for (size_t k = i + 1; k < s.size() && k <= i + 10; ++k)
            if (s[k] == ';') {
                semi = k;
                break;
            }
        if (semi == std::string::npos) {
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
            const bool hex = ent[1] == 'x' || ent[1] == 'X';
            const size_t start = hex ? 2 : 1;
            // Chiffres seulement (strtoul accepterait espaces et signes)
            bool ok = start < ent.size();
            unsigned long cp = 0;
            for (size_t k = start; ok && k < ent.size(); ++k) {
                const char c = ent[k];
                const int d = hex ? hexVal(c) : c >= '0' && c <= '9' ? c - '0' : -1;
                ok = d >= 0;
                cp = cp * (hex ? 16 : 10) + static_cast<unsigned long>(d < 0 ? 0 : d);
            }
            // Refusés : NUL et caractères de contrôle (sauf tabulation et retour à la ligne), moitiés de paire
            // UTF-16, au-delà de U+10FFFF ; l'entité reste alors telle quelle
            ok = ok && (cp >= 0x20 || cp == '\t' || cp == '\n') && !(cp >= 0xD800 && cp <= 0xDFFF) && cp <= 0x10FFFF;
            if (ok) appendUtf8(o, cp);
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
        // Nom lu sur 64 caractères au plus et comparé en place : pas de copie ni de parcours jusqu'à la fin
        // pour chaque '<' (200 Ko de '<' prenaient 52 s)
        const size_t limit = std::min(xml.size(), q + 64);
        size_t nameEnd = q, colon = std::string::npos;
        while (nameEnd < limit && !std::isspace(static_cast<unsigned char>(xml[nameEnd])) && xml[nameEnd] != '>' &&
               xml[nameEnd] != '/' && xml[nameEnd] != '<') {
            if (xml[nameEnd] == ':' && colon == std::string::npos) colon = nameEnd;
            ++nameEnd;
        }
        const size_t localStart = colon == std::string::npos ? q : colon + 1;
        // Trop long, ou coupé par un autre '<' : pas une balise (chaque '<' coûte au plus la distance au suivant)
        const bool broken = (nameEnd == limit && limit < xml.size()) || (nameEnd < xml.size() && xml[nameEnd] == '<');
        if (!broken && nameEnd - localStart == tag.size() && xml.compare(localStart, tag.size(), tag) == 0) {
            size_t gt = xml.find('>', nameEnd);
            if (gt == std::string::npos) return std::string::npos;
            fullName = xml.substr(q, nameEnd - q);
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

std::vector<std::string> parseScpdArguments(const std::string& scpdXml, const std::string& action) {
    std::vector<std::string> out;
    for (const std::string& a : xmlElements(scpdXml, "action")) {
        std::string name;
        if (!xmlElement(a, "name", name) || trim(name) != action) continue;
        for (const std::string& arg : xmlElements(a, "argument")) {
            std::string n, dir;
            if (!xmlElement(arg, "name", n)) continue;
            xmlElement(arg, "direction", dir);
            out.push_back((trim(dir) == "out" ? "->" : "") + trim(n));
        }
        break;
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
        if (xmlElement(info, "PicUrl", v)) q.picUrl = trim(xmlUnescape(v));
        if (xmlElement(info, "ContentType", v)) q.contentType = trim(xmlUnescape(v));
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
                    if (xmlElement(didl, "albumid", v)) item.albumId = trim(xmlUnescape(v));
                }
                if (!item.title.empty()) q.titles.push_back(item.title);
                q.items.push_back(item);
            }
        }
    }
    return q;
}

bool isLiveQueue(const QueueInfo& q) {
    if (q.contentType == "station") return true;
    const std::string origin = urlOrigin(q.searchUrl);
    if (origin.empty()) return false;
    const std::string path = q.searchUrl.substr(origin.size(), q.searchUrl.find('?') - origin.size());  // sans requête
    return path.find("/mix/") != std::string::npos;
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
    // Élément vide <tag/> ou <tag /> : remplacé (sinon il resterait avant le nouveau et serait lu en premier)
    for (const std::string& empty : {"<" + tag + "/>", "<" + tag + " />"}) {
        size_t z = xml.find(empty);
        if (z != std::string::npos) {
            xml.replace(z, empty.size(), open + escapedValue + close);
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
    if (x.find("<ListInfo>") == std::string::npos) {
        const size_t z = x.find("<ListInfo/>");
        if (z != std::string::npos) x.replace(z, 11, "<ListInfo></ListInfo>");
        else x.insert(x.find("</PlayList>"), "<ListInfo></ListInfo>");
    }
    setElement(x, "SearchUrl", xmlEscape(searchUrl), "ListInfo");
    size_t n = x.find("<TrackNumber>");
    if (n != std::string::npos) {
        size_t ne = x.find("</TrackNumber>", n);
        if (ne != std::string::npos) x.erase(n, ne + 14 - n);
    }
    return "<?xml version=\"1.0\"?>" + x;
}

std::string urlOrigin(const std::string& url) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos || (url.compare(0, scheme, "http") != 0 && url.compare(0, scheme, "https") != 0))
        return "";
    size_t end = url.find_first_of("/?#", scheme + 3);
    return url.substr(0, end);
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

namespace {

bool startsWith(const std::string& s, size_t at, const char* lit) {
    const size_t n = std::char_traits<char>::length(lit);
    return at <= s.size() && s.size() - at >= n && s.compare(at, n, lit) == 0;
}

bool startsWithNoCase(const std::string& s, size_t at, const char* lit) {
    const size_t n = std::char_traits<char>::length(lit);
    if (at > s.size() || s.size() - at < n) return false;
    for (size_t k = 0; k < n; ++k)
        if (std::tolower(static_cast<unsigned char>(s[at + k])) != std::tolower(static_cast<unsigned char>(lit[k])))
            return false;
    return true;
}

// Tous les blancs que les fonctions JSON sautent (std::isspace, \v et \f compris) : sinon une valeur que jsonString
// lirait resterait visible dans maskSecrets
size_t skipBlank(const std::string& s, size_t i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return i;
}

// Fin d'une valeur de paramètre d'URL commençant en v
size_t queryValueEnd(const std::string& o, size_t v, bool percentForm) {
    size_t e = v;
    while (e < o.size() && o[e] != '&' && o[e] != '<' && o[e] != '"' && o[e] != ' ' && o[e] != '\n' && o[e] != '\r' &&
           o[e] != '\t' && o[e] != '\'' && !(percentForm && startsWithNoCase(o, e, "%26")))
        ++e;
    return e;
}

// Valeur JSON (chaîne, chaîne échappée \"…\" ou &quot;…&quot;, nombre, littéral) commençant en v : [vs, ve) à masquer
bool jsonValueSpan(const std::string& o, size_t v, size_t& vs, size_t& ve) {
    if (v >= o.size()) return false;
    if (startsWith(o, v, "&quot;")) {
        vs = v + 6;
        ve = o.find("&quot;", vs);
        if (ve == std::string::npos) ve = o.size();
        return true;
    }
    if (startsWith(o, v, "\\\"")) {
        vs = v + 2;
        ve = o.find("\\\"", vs);
        if (ve == std::string::npos) ve = o.size();
        return true;
    }
    if (o[v] == '"') {
        vs = ve = v + 1;
        while (ve < o.size() && o[ve] != '"') ve += o[ve] == '\\' ? 2 : 1;
        ve = std::min(ve, o.size());
        return true;
    }
    if (o[v] == '{' || o[v] == '[') return false;  // objet ou tableau : pas un secret simple
    vs = ve = v;  // nombre ou littéral
    while (ve < o.size() && o[ve] != ',' && o[ve] != '}' && o[ve] != ']' && o[ve] != '<' && o[ve] != '&' &&
           o[ve] != '"' && !std::isspace(static_cast<unsigned char>(o[ve])))
        ++ve;
    return true;
}

}  // namespace

std::string maskSecrets(const std::string& text) {
    static const char* kKeys[] = {"user_auth_token", "access_token", "token",       "secret",   "password",
                                  "request_sig",     "sig",          "hmac",        "uid",      "eid",
                                  "app_id",          "accessToken",  "clientId",    "client_secret",
                                  "refresh_token",   "refreshToken", "api_key",     "apiKey"};
    std::string o = text;
    auto mask = [&](size_t vs, size_t ve) {
        ve = std::min(ve, o.size());
        if (ve > vs && o.compare(vs, ve - vs, "***") != 0) o.replace(vs, ve - vs, "***");
    };
    for (const char* key : kKeys) {
        const std::string k = key;
        size_t p = 0;
        while ((p = o.find(k, p)) != std::string::npos) {
            const size_t ke = p + k.size();
            // Début de nom : rien avant, ou un séparateur (pas une lettre, un chiffre ou '_' : « mytoken » n'est pas
            // « token »), ou un caractère encodé %XX (« %26token%3D… »)
            const size_t ks = p;
            p = ke;
            const unsigned char before = ks ? static_cast<unsigned char>(o[ks - 1]) : ' ';
            const bool boundary = ks == 0 || !(std::isalnum(before) || before == '_') ||
                                  (ks >= 3 && o[ks - 3] == '%' && std::isxdigit(before) &&
                                   std::isxdigit(static_cast<unsigned char>(o[ks - 2])));
            if (!boundary) continue;
            if (startsWith(o, ke, "=")) {  // paramètre d'URL : key=value
                mask(ke + 1, queryValueEnd(o, ke + 1, false));
            } else if (startsWithNoCase(o, ke, "%3D")) {  // paramètre encodé : key%3Dvalue%26…
                mask(ke + 3, queryValueEnd(o, ke + 3, true));
            } else if (startsWith(o, ke, ">") && ks > 0 && (before == '<' || before == ':')) {
                // élément XML <key>value</key> (ou <ns:key>)
                const size_t e = o.find('<', ke + 1);
                mask(ke + 1, e == std::string::npos ? o.size() : e);
            } else if (startsWith(o, ke, "&gt;") && (ks >= 4 && startsWith(o, ks - 4, "&lt;"))) {
                // élément XML échappé &lt;key&gt;value&lt;/key&gt;
                const size_t e = o.find("&lt;", ke + 4);
                mask(ke + 4, e == std::string::npos ? o.size() : e);
            } else {
                // Clé JSON : "key", \"key\" ou &quot;key&quot;, puis ':' (blancs et retours à la ligne permis)
                size_t c;
                if (startsWith(o, ke, "\"")) c = ke + 1;
                else if (startsWith(o, ke, "\\\"")) c = ke + 2;
                else if (startsWith(o, ke, "&quot;")) c = ke + 6;
                else continue;
                c = skipBlank(o, c);
                if (c >= o.size() || o[c] != ':') continue;
                size_t vs, ve;
                if (jsonValueSpan(o, skipBlank(o, c + 1), vs, ve)) mask(vs, ve);
            }
            // La recherche reprend après le nom : une valeur masquée vaut « *** », sans nom de clé dedans
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
