#include "core.h"

#include <algorithm>
#include <cctype>
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
    if (jsonString(json, "vol", v)) st.volume = std::max(0, std::min(100, std::atoi(v.c_str())));
    if (jsonString(json, "Title", v)) st.title = decodeHexText(v);
    if (jsonString(json, "Artist", v)) st.artist = decodeHexText(v);
    if (jsonString(json, "Album", v)) st.album = decodeHexText(v);
    if (jsonString(json, "mode", v)) st.mode = std::atoi(v.c_str());
    for (std::string* f : {&st.title, &st.artist, &st.album})
        if (isUnknown(*f)) f->clear();
    return st;
}

std::string sourceName(int mode) {
    switch (mode) {
        case 1: return "AirPlay";
        case 2: return "DLNA";
        case 31: return "Spotify";
        case 40: return "Entrée ligne";
        case 41: return "Bluetooth";
        case 43: return "Optique";
        default: return "";
    }
}

TrackMeta parseMetaInfo(const std::string& json) {
    TrackMeta m;
    if (json.find("\"metaData\"") == std::string::npos) return m;
    m.ok = true;
    std::string v;
    if (jsonString(json, "album", v) && !isUnknown(v)) m.album = v;
    if (jsonString(json, "albumArtURI", v) && isHttpUrl(v)) m.artUrl = v;
    if (jsonString(json, "sampleRate", v)) m.sampleRate = std::max(0, std::atoi(v.c_str()));
    if (jsonString(json, "bitDepth", v)) m.bitDepth = std::max(0, std::atoi(v.c_str()));
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

Layout computeLayout(int width, int height, int columns, int presetCount) {
    Layout L;
    const int m = 16, gap = 12;
    const int transportH = std::max(90, height / 6);
    const int minCellH = 64;
    columns = std::max(1, columns);
    int rows = std::max(1, (presetCount + columns - 1) / columns);

    // Bandeau : tout ce qui reste une fois les presets servis, entre 112 et 200 px
    int avail = height - 2 * m - transportH - 2 * gap;
    int headerH = avail - rows * minCellH - (rows - 1) * gap;
    headerH = std::max(112, std::min(std::min(200, width / 3), headerH));
    L.header = Rect{m, m, width - 2 * m, headerH};
    L.art = Rect{m, m, headerH, headerH};
    const int artGap = 20;
    L.info = Rect{m + headerH + artGap, m, width - 2 * m - headerH - artGap, headerH};

    int gridTop = m + headerH + gap;
    int gridBottom = height - m - transportH - gap;
    int cellW = (width - 2 * m - (columns - 1) * gap) / columns;
    int cellH = (gridBottom - gridTop - (rows - 1) * gap) / rows;
    for (int i = 0; i < presetCount; ++i) {
        int r = i / columns, c = i % columns;
        L.presets.push_back(Rect{m + c * (cellW + gap), gridTop + r * (cellH + gap), cellW, cellH});
    }

    int tW = (width - 2 * m - (kTransportCount - 1) * gap) / kTransportCount;
    for (int i = 0; i < kTransportCount; ++i)
        L.transport[i] = Rect{m + i * (tW + gap), height - m - transportH, tW, transportH};
    return L;
}

}  // namespace wr
