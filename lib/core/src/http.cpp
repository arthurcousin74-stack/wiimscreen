#include "http.h"

#include "text.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace wr {

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

namespace {

// Valeur (sans espaces autour) de l'en-tête `name` (en minuscules) dans des en-têtes déjà en minuscules
bool headerValue(const std::string& lowerHeaders, const char* name, std::string& value) {
    const std::string needle = std::string("\n") + name + ":";
    size_t p = lowerHeaders.find(needle);
    if (p == std::string::npos) return false;
    p += needle.size();
    size_t e = lowerHeaders.find('\r', p);
    if (e == std::string::npos) e = lowerHeaders.size();
    value = trim(lowerHeaders.substr(p, e - p));
    return true;
}

// Content-Length : chiffres seulement (« abc », « -1 », « 12x » : longueur inconnue)
bool contentLength(const std::string& lowerHeaders, size_t& n) {
    std::string v;
    if (!headerValue(lowerHeaders, "content-length", v) || v.empty() || v.size() > 9) return false;  // < 1 Go, 32 bits
    n = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + static_cast<size_t>(c - '0');
    }
    return true;
}

bool isChunked(const std::string& lowerHeaders) {
    std::string v;
    return headerValue(lowerHeaders, "transfer-encoding", v) && v.find("chunked") != std::string::npos;
}

enum class Chunks { Incomplete, Complete, Malformed };

// Parcourt les morceaux d'un corps "chunked" à partir de `p` (taille en hexadécimal, extensions « ;… »
// ignorées, données, CRLF ; morceau de taille 0 puis en-têtes de fin éventuels). `dec` (optionnel) reçoit
// les données. Coût : proportionnel au nombre de morceaux, pas à leur taille.
Chunks walkChunks(const std::string& raw, size_t p, std::string* dec) {
    while (true) {
        size_t n = 0, k = p, digits = 0;
        int d;
        while (k < raw.size() && (d = hexVal(raw[k])) >= 0) {
            if (n > 0xFFFFFF) return Chunks::Malformed;  // 256 Mo ou plus : hostile (et n + 2 tient en 32 bits)
            ++digits;
            n = n * 16 + static_cast<size_t>(d);
            ++k;
        }
        if (k >= raw.size()) return Chunks::Incomplete;
        if (digits == 0) return Chunks::Malformed;
        size_t le = raw.find("\r\n", k);
        if (le == std::string::npos) return raw.size() - k > 256 ? Chunks::Malformed : Chunks::Incomplete;
        if (le - k > 256 || (le > k && raw[k] != ';' && raw[k] != ' ' && raw[k] != '\t')) return Chunks::Malformed;
        p = le + 2;
        if (n == 0) {
            // Fin : ligne vide tout de suite, ou en-têtes de fin terminés par une ligne vide
            if (raw.size() < p + 2) return Chunks::Incomplete;
            if (raw.compare(p, 2, "\r\n") == 0) return Chunks::Complete;
            return raw.find("\r\n\r\n", p) != std::string::npos ? Chunks::Complete : Chunks::Incomplete;
        }
        if (raw.size() - p < n + 2) return Chunks::Incomplete;
        if (raw.compare(p + n, 2, "\r\n") != 0) return Chunks::Malformed;
        if (dec) dec->append(raw, p, n);
        p += n + 2;
    }
}

std::string lowerCopy(const std::string& s) {
    std::string o = s;
    std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return o;
}

}  // namespace

bool parseHttpResponse(const std::string& raw, HttpResponse& out) {
    size_t eol = raw.find("\r\n");
    if (eol == std::string::npos || raw.compare(0, 5, "HTTP/") != 0) return false;
    size_t sp = raw.find(' ');
    if (sp == std::string::npos || sp > eol) return false;
    out.status = std::atoi(raw.c_str() + sp + 1);
    size_t hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return false;
    const std::string headers = lowerCopy(raw.substr(0, hdrEnd));
    if (isChunked(headers)) {
        // Corps tronqué ou abîmé : refusé (pas de réponse partielle prise pour la réponse entière)
        std::string dec;
        if (walkChunks(raw, hdrEnd + 4, &dec) != Chunks::Complete) return false;
        out.body = dec;
    } else {
        out.body = raw.substr(hdrEnd + 4);
    }
    return true;
}

HttpFrame httpFrame(const std::string& raw) {
    HttpFrame f;
    size_t hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return f;
    const std::string headers = lowerCopy(raw.substr(0, hdrEnd));
    const size_t bodyStart = hdrEnd + 4;
    std::string conn;
    const bool hasConn = headerValue(headers, "connection", conn);
    const bool http10 = headers.compare(0, 8, "http/1.0") == 0;
    const bool closes = (hasConn && conn.find("close") != std::string::npos) ||
                        (http10 && !(hasConn && conn.find("keep-alive") != std::string::npos));
    size_t n = 0;
    if (isChunked(headers)) {  // Transfer-Encoding l'emporte sur Content-Length (RFC 7230 3.3.3)
        const Chunks c = walkChunks(raw, bodyStart, nullptr);
        if (c == Chunks::Incomplete) return f;
        f.complete = true;  // abîmé : inutile d'attendre la suite, parseHttpResponse le refusera
        f.reusable = c == Chunks::Complete && !closes;
        return f;
    }
    if (contentLength(headers, n)) {
        f.complete = raw.size() >= bodyStart + n;
    } else {
        return f;  // longueur inconnue (ou illisible) : lire jusqu'à la fermeture
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

}  // namespace wr
