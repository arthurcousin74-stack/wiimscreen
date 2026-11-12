#include "core.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace wr {

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool validUtf8NoControl(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = s[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + n > s.size()) return false;
        if (n == 1 && (c < 0x20 || c == 0x7f)) return false;
        for (size_t k = 1; k < n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += n;
    }
    return true;
}

// Découpe en mots en gardant les séparateurs simples
std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool isUnknown(const std::string& s) {
    return s.empty() || s == "unknow" || s == "Unknown" || s == "un_known" || s == "unknown";
}

void popUtf8(std::string& s) {
    while (!s.empty()) {
        unsigned char c = s.back();
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}

}  // namespace

bool parsePresets(const std::string& text, std::vector<Preset>& out, std::string& error) {
    out.clear();
    size_t start = 0;
    int lineNo = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = trim(text.substr(start, end - start));
        ++lineNo;
        start = end + 1;
        if (line.empty() || line[0] == '#') {
            if (end == text.size()) break;
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = "ligne " + std::to_string(lineNo) + " : format attendu numéro=nom";
            return false;
        }
        std::string num = trim(line.substr(0, eq));
        std::string label = trim(line.substr(eq + 1));
        char* endp = nullptr;
        long slot = std::strtol(num.c_str(), &endp, 10);
        if (num.empty() || *endp != '\0' || slot < 1 || slot > 20) {
            error = "ligne " + std::to_string(lineNo) + " : numéro de preset entre 1 et 20";
            return false;
        }
        if (label.empty() || label.size() > 60) {
            error = "ligne " + std::to_string(lineNo) + " : nom vide ou trop long";
            return false;
        }
        if (static_cast<int>(out.size()) >= kMaxPresets) {
            error = "12 presets au maximum";
            return false;
        }
        out.push_back(Preset{static_cast<int>(slot), label});
        if (end == text.size()) break;
    }
    if (out.empty()) {
        error = "ajoute au moins un preset";
        return false;
    }
    return true;
}

std::string decodeHexText(const std::string& raw) {
    std::string s = trim(raw);
    if (s.empty() || s.size() % 2 != 0) return s;
    std::string out;
    out.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) {
        int hi = hexVal(s[i]), lo = hexVal(s[i + 1]);
        if (hi < 0 || lo < 0) return s;
        out += static_cast<char>(hi * 16 + lo);
    }
    return validUtf8NoControl(out) ? out : s;
}

bool jsonString(const std::string& json, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\"";
    size_t p = 0;
    while ((p = json.find(needle, p)) != std::string::npos) {
        size_t q = p + needle.size();
        while (q < json.size() && std::isspace(static_cast<unsigned char>(json[q]))) ++q;
        if (q >= json.size() || json[q] != ':') {
            p = q;
            continue;
        }
        ++q;
        while (q < json.size() && std::isspace(static_cast<unsigned char>(json[q]))) ++q;
        if (q >= json.size() || json[q] != '"') return false;
        ++q;
        out.clear();
        while (q < json.size() && json[q] != '"') {
            if (json[q] == '\\' && q + 1 < json.size()) {
                char e = json[++q];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                        if (q + 4 < json.size()) {
                            long cp = std::strtol(json.substr(q + 1, 4).c_str(), nullptr, 16);
                            q += 4;
                            if (cp < 0x80) {
                                out += static_cast<char>(cp);
                            } else if (cp < 0x800) {
                                out += static_cast<char>(0xC0 | (cp >> 6));
                                out += static_cast<char>(0x80 | (cp & 0x3F));
                            } else {
                                out += static_cast<char>(0xE0 | (cp >> 12));
                                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                out += static_cast<char>(0x80 | (cp & 0x3F));
                            }
                        }
                        break;
                    default: out += e;
                }
            } else {
                out += json[q];
            }
            ++q;
        }
        return q < json.size();
    }
    return false;
}

PlayerStatus parsePlayerStatus(const std::string& json) {
    PlayerStatus st;
    std::string v;
    if (!jsonString(json, "status", v)) return st;
    st.ok = true;
    st.playing = (v == "play" || v == "loading");
    st.loading = (v == "loading");
    if (jsonString(json, "vol", v)) st.volume = std::max(0, std::min(100, std::atoi(v.c_str())));
    if (jsonString(json, "Title", v)) st.title = decodeHexText(v);
    if (jsonString(json, "Artist", v)) st.artist = decodeHexText(v);
    if (jsonString(json, "Album", v)) st.album = decodeHexText(v);
    long n;
    if (jsonInt(json, "mode", n)) st.mode = static_cast<int>(n);
    if (jsonInt(json, "mute", n)) st.muted = n == 1;
    if (jsonInt(json, "curpos", n) && n > 0) st.positionMs = static_cast<uint32_t>(n);
    if (jsonInt(json, "totlen", n) && n > 0) st.durationMs = static_cast<uint32_t>(n);
    for (std::string* f : {&st.title, &st.artist, &st.album})
        if (isUnknown(*f)) f->clear();
    return st;
}

// Valeurs du champ "mode" d'après la documentation WiiM (HTTP API v1.2)
std::string sourceName(int mode) {
    switch (mode) {
        case 1: return "AirPlay";
        case 2: return "DLNA";
        case 11: return "USB";
        case 16: return "Carte SD";
        case 31: return "Spotify";
        case 32: return "TIDAL";
        case 40: return "Entrée ligne";
        case 41: return "Bluetooth";
        case 42: return "Stockage externe";
        case 43: return "Optique";
        case 99: return "Multiroom";
        default: return "";
    }
}

bool jsonInt(const std::string& json, const std::string& key, long& out) {
    std::string needle = "\"" + key + "\"";
    size_t p = 0;
    while ((p = json.find(needle, p)) != std::string::npos) {
        size_t q = p + needle.size();
        while (q < json.size() && std::isspace(static_cast<unsigned char>(json[q]))) ++q;
        if (q >= json.size() || json[q] != ':') {
            p = q;
            continue;
        }
        ++q;
        while (q < json.size() && std::isspace(static_cast<unsigned char>(json[q]))) ++q;
        if (q < json.size() && json[q] == '"') ++q;
        char* end = nullptr;
        long v = std::strtol(json.c_str() + q, &end, 10);
        if (end == json.c_str() + q) return false;
        out = v;
        return true;
    }
    return false;
}

bool ssdpHost(const std::string& response, std::string& ip) {
    std::string lower = response;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    size_t p = lower.find("\nlocation:");
    if (p == std::string::npos) return false;
    p = lower.find("://", p);
    if (p == std::string::npos) return false;
    p += 3;
    size_t e = p;
    while (e < response.size() && (std::isdigit(static_cast<unsigned char>(response[e])) || response[e] == '.')) ++e;
    std::string host = response.substr(p, e - p);
    // IPv4 complète : quatre nombres de 0 à 255
    int parts = 0;
    size_t s = 0;
    while (s <= host.size()) {
        size_t d = host.find('.', s);
        if (d == std::string::npos) d = host.size();
        std::string n = host.substr(s, d - s);
        if (n.empty() || n.size() > 3 || std::atoi(n.c_str()) > 255) return false;
        ++parts;
        s = d + 1;
    }
    if (parts != 4) return false;
    ip = host;
    return true;
}

WiimIdentity parseStatusEx(const std::string& json) {
    WiimIdentity id;
    std::string v;
    if (!jsonString(json, "uuid", id.uuid) && !jsonString(json, "project", v)) return id;
    id.ok = true;
    if (!jsonString(json, "DeviceName", id.name) || id.name.empty()) jsonString(json, "ssid", id.name);
    id.name = decodeHexText(id.name);
    if (id.name.size() > 60) id.name.resize(60);
    if (id.name.empty()) id.name = "WiiM";
    return id;
}

std::string formatTime(uint32_t ms) {
    uint32_t s = ms / 1000, h = s / 3600, m = (s / 60) % 60;
    s %= 60;
    char buf[16];
    if (h) snprintf(buf, sizeof buf, "%u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    else snprintf(buf, sizeof buf, "%u:%02u", (unsigned)m, (unsigned)s);
    return buf;
}

bool parsePresetInfo(const std::string& json, std::vector<Preset>& out) {
    out.clear();
    size_t p = json.find("\"preset_list\"");
    if (p == std::string::npos) return false;
    p = json.find('[', p);
    if (p == std::string::npos) return false;
    // Objets plats successifs { ... } jusqu'au ']' de fin
    while (true) {
        size_t open = json.find_first_of("{]", p);
        if (open == std::string::npos || json[open] == ']') break;
        size_t close = json.find('}', open);
        if (close == std::string::npos) return false;
        std::string item = json.substr(open, close - open + 1);
        p = close + 1;
        long number = 0;
        std::string name, pic;
        if (!jsonInt(item, "number", number) || number < 1 || number > 20) continue;
        jsonString(item, "name", name);
        name = decodeHexText(name);
        if (isUnknown(name)) name = "Preset " + std::to_string(number);
        if (name.size() > 60) name.resize(60);
        if (jsonString(item, "picurl", pic) && !isHttpUrl(pic)) pic.clear();
        out.push_back(Preset{static_cast<int>(number), name, pic});
        if (static_cast<int>(out.size()) >= kMaxPresets) break;
    }
    std::sort(out.begin(), out.end(), [](const Preset& a, const Preset& b) { return a.slot < b.slot; });
    return true;
}

TrackMeta parseMetaInfo(const std::string& json) {
    TrackMeta m;
    if (json.find("\"metaData\"") == std::string::npos) return m;
    m.ok = true;
    // La documentation WiiM écrit certaines clés avec une espace finale ("albumArtURI ") :
    // les deux formes sont acceptées
    auto get = [&](const char* key, std::string& v) {
        return jsonString(json, key, v) || jsonString(json, std::string(key) + " ", v);
    };
    std::string v;
    if (get("title", v) && !isUnknown(v)) m.title = v;
    if (get("artist", v) && !isUnknown(v)) m.artist = v;
    if (get("album", v) && !isUnknown(v)) m.album = v;
    if (get("albumArtURI", v) && isHttpUrl(v)) m.artUrl = v;
    if (get("sampleRate", v)) m.sampleRate = std::max(0, std::atoi(v.c_str()));
    if (get("bitDepth", v)) m.bitDepth = std::max(0, std::atoi(v.c_str()));
    if (m.sampleRate > 768000) m.sampleRate = 0;
    if (m.bitDepth > 64) m.bitDepth = 0;
    return m;
}

std::string formatQuality(int sampleRate, int bitDepth) {
    std::string bits = bitDepth > 0 ? std::to_string(bitDepth) + " bits" : "";
    std::string rate;
    if (sampleRate >= 1000) {
        int tenths = (sampleRate + 50) / 100;  // en dixièmes de kHz
        rate = std::to_string(tenths / 10);
        if (tenths % 10) rate += "," + std::to_string(tenths % 10);
        rate += " kHz";
    }
    if (bits.empty()) return rate;
    if (rate.empty()) return bits;
    return bits + " \xC2\xB7 " + rate;  // ·
}

bool isHttpUrl(const std::string& url) {
    size_t hostStart;
    if (url.compare(0, 7, "http://") == 0) hostStart = 7;
    else if (url.compare(0, 8, "https://") == 0) hostStart = 8;
    else return false;
    if (url.size() > 1024 || url.size() == hostStart) return false;
    if (url[hostStart] == '/' || url[hostStart] == ':' || url[hostStart] == '?') return false;
    for (unsigned char c : url)
        if (c <= 0x20 || c == 0x7f) return false;
    return true;
}

ImageType sniffImage(const uint8_t* d, size_t n) {
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return ImageType::Jpeg;
    static const uint8_t png[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (n >= 8 && std::equal(png, png + 8, d)) return ImageType::Png;
    return ImageType::Unknown;
}

bool parseHttpResponse(const std::string& raw, HttpResponse& out) {
    size_t eol = raw.find("\r\n");
    if (eol == std::string::npos || raw.compare(0, 5, "HTTP/") != 0) return false;
    size_t sp = raw.find(' ');
    if (sp == std::string::npos || sp > eol) return false;
    out.status = std::atoi(raw.c_str() + sp + 1);
    size_t hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return false;
    std::string headers = raw.substr(0, hdrEnd);
    std::transform(headers.begin(), headers.end(), headers.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string body = raw.substr(hdrEnd + 4);
    if (headers.find("transfer-encoding: chunked") != std::string::npos) {
        std::string dec;
        size_t p = 0;
        while (p < body.size()) {
            size_t le = body.find("\r\n", p);
            if (le == std::string::npos) break;
            long n = std::strtol(body.substr(p, le - p).c_str(), nullptr, 16);
            if (n <= 0) break;
            p = le + 2;
            dec += body.substr(p, static_cast<size_t>(n));
            p += static_cast<size_t>(n) + 2;
        }
        body = dec;
    }
    out.body = body;
    return true;
}

HttpFrame httpFrame(const std::string& raw) {
    HttpFrame f;
    size_t hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return f;
    std::string headers = raw.substr(0, hdrEnd);
    std::transform(headers.begin(), headers.end(), headers.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const size_t bodyStart = hdrEnd + 4;
    const bool http10 = headers.compare(0, 8, "http/1.0") == 0;
    const bool closes = headers.find("\nconnection: close") != std::string::npos ||
                        (http10 && headers.find("\nconnection: keep-alive") == std::string::npos);
    size_t p = headers.find("\ncontent-length:");
    if (p != std::string::npos) {
        long n = std::strtol(headers.c_str() + p + 16, nullptr, 10);
        if (n < 0) return f;
        f.complete = raw.size() >= bodyStart + static_cast<size_t>(n);
    } else if (headers.find("\ntransfer-encoding: chunked") != std::string::npos) {
        f.complete = raw.find("\r\n0\r\n\r\n", bodyStart - 2) != std::string::npos;
    } else {
        return f;  // longueur inconnue : lire jusqu'à la fermeture
    }
    f.reusable = f.complete && !closes;
    return f;
}

std::string toHex(const uint8_t* data, size_t len) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        s += d[data[i] >> 4];
        s += d[data[i] & 15];
    }
    return s;
}

bool parseFingerprint(const std::string& s, uint8_t out[32]) {
    std::string clean;
    for (char c : s)
        if (c != ':' && c != ' ' && c != '-') clean += c;
    if (clean.size() != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
        int hi = hexVal(clean[2 * i]), lo = hexVal(clean[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>(hi * 16 + lo);
    }
    return true;
}

bool constantTimeEq(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t acc = 0;
    for (size_t i = 0; i < len; ++i) acc |= a[i] ^ b[i];
    return acc == 0;
}

bool isValidHost(const std::string& host) {
    if (host.empty() || host.size() > 253) return false;
    std::string name;
    uint16_t port = 0;
    size_t colon = host.rfind(':');
    if (colon != std::string::npos) {
        std::string p = host.substr(colon + 1);
        if (p.empty() || p.size() > 5 || !std::all_of(p.begin(), p.end(), ::isdigit)) return false;
        long v = std::atol(p.c_str());
        if (v < 1 || v > 65535) return false;
    }
    splitHostPort(host, name, port, 0);
    if (name.empty()) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '-';
    });
}

void splitHostPort(const std::string& host, std::string& name, uint16_t& port, uint16_t defaultPort) {
    size_t colon = host.rfind(':');
    if (colon == std::string::npos) {
        name = host;
        port = defaultPort;
    } else {
        name = host.substr(0, colon);
        port = static_cast<uint16_t>(std::atoi(host.c_str() + colon + 1));
    }
}

std::string htmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
        }
    }
    return o;
}

std::vector<std::string> wrapText(const std::string& text, int maxWidth, int maxLines,
                                  const std::function<int(const std::string&)>& measure) {
    std::vector<std::string> lines;
    std::vector<std::string> ws = words(text);
    size_t i = 0;
    const std::string ell = "\xE2\x80\xA6";  // …
    while (i < ws.size() && static_cast<int>(lines.size()) < maxLines) {
        std::string line = ws[i++];
        while (i < ws.size() && measure(line + " " + ws[i]) <= maxWidth) line += " " + ws[i++];
        bool last = static_cast<int>(lines.size()) == maxLines - 1;
        if (last && i < ws.size()) {
            // Texte restant : on coupe au mot si possible, sinon au caractère
            i = ws.size();
            if (measure(line + ell) > maxWidth) {
                while (!line.empty() && measure(line + ell) > maxWidth) popUtf8(line);
                line = trim(line);
            }
            line += ell;
        } else if (measure(line) > maxWidth) {
            while (!line.empty() && measure(line + ell) > maxWidth) popUtf8(line);
            line += ell;
        }
        lines.push_back(line);
    }
    return lines;
}

uint32_t hslToRgbPublic(float h, float s, float l);

Layout computeLayout(int width, int height, int presetCount) {
    Layout L;
    const int ref = std::min(width, height);  // 720 sur le Tab5 (référence de mise en page : 540)
    auto sc = [ref](int v) { return v * ref / 540; };
    const bool landscape = width >= height;
    const int m = sc(18), gap = sc(12);
    const int volH = sc(48), playD = sc(76), sideD = sc(60), progH = sc(30);

    // --- Barre de navigation : à gauche (paysage) ou en haut (portrait)
    const int railW = sc(76);
    if (landscape) {
        L.rail = Rect{0, 0, railW, height};
        L.navHome = Rect{(railW - sideD) / 2, m, sideD, sideD};
        L.navSettings = Rect{(railW - sideD) / 2, height - m - sideD, sideD, sideD};
        L.content = Rect{railW, 0, width - railW, height};
    } else {
        L.rail = Rect{0, 0, width, railW};
        L.navHome = Rect{m, (railW - sideD) / 2, sideD, sideD};
        L.navSettings = Rect{width - m - sideD, (railW - sideD) / 2, sideD, sideD};
        L.content = Rect{0, railW, width, height - railW};
    }
    const int X0 = landscape ? L.content.x - sc(4) : 0;  // la barre a déjà sa marge
    const int Y0 = L.content.y;
    const int W = width - X0;

    // --- Bande des presets : tuiles (image + nom), une ou deux rangées par page.
    // Toujours réservée, même sans preset (l'écran y explique comment en ajouter) : sinon
    // l'écran principal ressemble à la vue ambiance.
    const int tileH = sc(136), dotsH = sc(16);
    const int rows = landscape || presetCount <= 0 ? 1 : 2;
    const int minTileW = sc(130);
    const int cols = std::max(2, std::min(6, (W - 2 * m + gap) / (minTileW + gap)));
    L.perPage = presetCount > 0 ? cols * rows : 0;
    const int stripH = rows * tileH + (rows - 1) * gap;
    const int dotsY = height - m - dotsH;
    L.strip = Rect{X0 + m, dotsY - stripH, W - 2 * m, stripH};
    if (presetCount > 0) {
        L.dots = Rect{X0 + m, dotsY, W - 2 * m, dotsH};
        const int tileW = (W - 2 * m - (cols - 1) * gap) / cols;
        for (int i = 0; i < std::min(presetCount, L.perPage); ++i) {
            int r = i / cols, c = i % cols;
            L.presets.push_back(Rect{X0 + m + c * (tileW + gap), L.strip.y + r * (tileH + gap), tileW, tileH});
        }
    }
    const int topY = Y0 + m;
    const int topBottom = L.strip.y - gap;

    // --- Pile de droite (paysage) ou du bas (portrait) : hauteurs fixes
    const int infoMinH = sc(150);
    const int stackH = infoMinH + sc(6) + progH + sc(12) + playD + sc(12) + volH;

    int colX, colW, colY;
    if (landscape) {
        int side = std::min(topBottom - topY, W * 40 / 100);
        L.art = Rect{X0 + m, topY + (topBottom - topY - side) / 2, side, side};
        colX = L.art.right() + sc(28);
        colW = width - m - colX;
        colY = topY;
    } else {
        int side = std::min(W - 2 * m, topBottom - topY - gap - stackH);
        L.art = Rect{X0 + (W - side) / 2, topY, side, side};
        colX = X0 + m;
        colW = W - 2 * m;
        colY = L.art.bottom() + gap;
    }
    // Du bas vers le haut : volume, commandes, progression ; le reste pour les textes
    L.volume = Rect{colX, topBottom - volH, colW, volH};
    const int playCy = L.volume.y - sc(12) - playD / 2;
    const int cx = colX + colW / 2;
    L.transport[kPlayPause] = Rect{cx - playD / 2, playCy - playD / 2, playD, playD};
    L.transport[kPrev] = Rect{cx - playD / 2 - sc(30) - sideD, playCy - sideD / 2, sideD, sideD};
    L.transport[kNext] = Rect{cx + playD / 2 + sc(30), playCy - sideD / 2, sideD, sideD};
    L.mic = Rect{colX + colW - sideD, playCy - sideD / 2, sideD, sideD};
    L.fav = Rect{colX, playCy - sideD / 2, sideD, sideD};
    L.progress = Rect{colX, L.transport[kPlayPause].y - sc(12) - progH, colW, progH};
    L.info = Rect{colX, colY, colW, L.progress.y - sc(6) - colY};

    // Pilule de volume : [ − ] 🔈 ━━━━━●──── 42 [ + ]
    const int b = volH - sc(8);
    L.transport[kVolDown] = Rect{L.volume.x + sc(4), L.volume.y + sc(4), b, b};
    L.transport[kVolUp] = Rect{L.volume.right() - sc(4) - b, L.volume.y + sc(4), b, b};
    L.mute = Rect{L.transport[kVolDown].right() + sc(6), L.volume.y, sc(40), volH};
    const int valueW = sc(62);  // « Muet » doit tenir
    L.volBar = Rect{L.mute.right() + sc(10), L.volume.y,
                    L.transport[kVolUp].x - sc(10) - valueW - (L.mute.right() + sc(10)), volH};

    return L;
}

uint32_t nameColor(const std::string& name) {
    uint32_t h = 2166136261u;  // FNV-1a
    for (unsigned char c : name) h = (h ^ c) * 16777619u;
    // Teintes saturées mais sombres : texte blanc lisible
    float hue = (h % 360) / 360.0f;
    return hslToRgbPublic(hue, 0.50f, 0.40f);
}

namespace {

void rgbToHsl(float r, float g, float b, float& h, float& s, float& l) {
    float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    l = (mx + mn) / 2;
    if (mx == mn) {
        h = s = 0;
        return;
    }
    float d = mx - mn;
    s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
    if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
    else if (mx == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h /= 6;
}

float hueToRgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f / 6) return p + (q - p) * 6 * t;
    if (t < 1.0f / 2) return q;
    if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
    return p;
}

uint32_t hslToRgb(float h, float s, float l) {
    float r, g, b;
    if (s == 0) {
        r = g = b = l;
    } else {
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
        r = hueToRgb(p, q, h + 1.0f / 3);
        g = hueToRgb(p, q, h);
        b = hueToRgb(p, q, h - 1.0f / 3);
    }
    auto c = [](float v) { return static_cast<uint32_t>(std::lround(std::max(0.f, std::min(1.f, v)) * 255)); };
    return (c(r) << 16) | (c(g) << 8) | c(b);
}

}  // namespace

uint32_t hslToRgbPublic(float h, float s, float l) { return hslToRgb(h, s, l); }

uint32_t accentColor(const uint8_t* rgb, size_t n, uint32_t fallback) {
    double sr = 0, sg = 0, sb = 0, sw = 0;
    for (size_t i = 0; i < n; ++i) {
        int r = rgb[3 * i], g = rgb[3 * i + 1], b = rgb[3 * i + 2];
        int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
        if (mx < 40) continue;  // presque noir : ne dit rien de la teinte
        double sat = double(mx - mn) / mx, val = mx / 255.0;
        double w = sat * sat * val;  // les couleurs vives comptent le plus
        sr += w * r;
        sg += w * g;
        sb += w * b;
        sw += w;
    }
    if (n == 0 || sw < 0.02 * n) return fallback;
    float h, s, l;
    rgbToHsl(float(sr / sw / 255), float(sg / sw / 255), float(sb / sw / 255), h, s, l);
    s = std::max(s, 0.45f);
    l = std::max(0.55f, std::min(0.68f, l));
    return hslToRgb(h, s, l);
}

}  // namespace wr
