// PC platform (simulator "real WiiM" and desktop builds): HTTP/1.1 client over WiFiClient, LittleFS on a directory
// of the PC (<state>/fs), and the CA bundle symbols of the firmware (on a PC, the system or the relay checks
// certificates).
#include <cstdio>
#include <filesystem>
#include <string>

#include "HTTPClient.h"
#include "LittleFS.h"
#include "ca_bundle.h"
#include "core.h"
#include "pc.h"
#include "upnp.h"

// The firmware embeds Mozilla's root certificates; the relay checks public servers instead
// (definitions of the symbols declared in ca_bundle.h: extern, so they link from other files)
const uint8_t ca_bundle_start[1] = {0};
const uint8_t ca_bundle_end[1] = {0};

LittleFSSim LittleFS;

// ------------------------------------------------------------------ HTTP/1.1 (Connection: close)

bool HTTPClient::begin(WiFiClient& client, const char* url) {
    client_ = &client;
    url_ = url;
    headers_.clear();
    body_.clear();
    size_ = -1;
    return url_.compare(0, 7, "http://") == 0 || url_.compare(0, 8, "https://") == 0;
}

void HTTPClient::end() {
    if (client_) client_->stop();
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

int HTTPClient::request(const char* method, const uint8_t* body, size_t n) {
    for (int hop = 0; hop <= redirects_; ++hop) {
        const bool https = url_.compare(0, 8, "https://") == 0;
        const size_t hs = https ? 8 : 7, slash = url_.find('/', hs);
        std::string hostPort = url_.substr(hs, slash == std::string::npos ? std::string::npos : slash - hs);
        const std::string path = slash == std::string::npos ? "/" : url_.substr(slash);
        std::string host = hostPort;
        int port = https ? 443 : 80;
        const size_t colon = hostPort.rfind(':');
        if (colon != std::string::npos) host = hostPort.substr(0, colon), port = std::atoi(hostPort.c_str() + colon + 1);
        client_->setConnectionTimeout(connectMs_);
        client_->setTimeout(timeoutMs_);
        if (!client_->connect(host.c_str(), (uint16_t)port)) return -1;  // HTTPC_ERROR_CONNECTION_REFUSED
        std::string req = std::string(method) + " " + path + " HTTP/1.1\r\nHost: " + hostPort + "\r\nUser-Agent: " +
                          agent_ + "\r\nConnection: close\r\nAccept-Encoding: identity\r\n";
        for (auto& h : headers_) req += h.first + ": " + h.second + "\r\n";
        if (body) req += "Content-Length: " + std::to_string(n) + "\r\n";
        req += "\r\n";
        client_->write(reinterpret_cast<const uint8_t*>(req.data()), req.size());
        if (body && n) client_->write(body, n);
        // Whole answer, until the server closes
        std::string raw;
        uint8_t buf[4096];
        const uint32_t start = millis();
        for (;;) {
            const int k = client_->read(buf, sizeof buf);
            if (k > 0) raw.append(reinterpret_cast<char*>(buf), k);
            else break;
            if (raw.size() > 8u * 1024 * 1024 || millis() - start > timeoutMs_ + 10000) break;
        }
        client_->stop();
        const size_t he = raw.find("\r\n\r\n");
        if (raw.compare(0, 5, "HTTP/") != 0 || he == std::string::npos) return -11;  // HTTPC_ERROR_READ_TIMEOUT
        const int code = std::atoi(raw.c_str() + raw.find(' ') + 1);
        const std::string head = lower(raw.substr(0, he));
        std::string location;
        bool chunked = false;
        size_ = -1;
        size_t p = 0;
        while ((p = head.find("\r\n", p)) != std::string::npos) {
            p += 2;
            const size_t e = head.find("\r\n", p), c = head.find(':', p);
            if (c == std::string::npos || c > e) continue;
            const std::string k = head.substr(p, c - p);
            std::string v = raw.substr(c + 1, (e == std::string::npos ? he : e) - c - 1);
            while (!v.empty() && v[0] == ' ') v.erase(0, 1);
            if (k == "content-length") size_ = std::atoi(v.c_str());
            else if (k == "transfer-encoding" && lower(v).find("chunked") != std::string::npos) chunked = true;
            else if (k == "location") location = v;
        }
        // Body (Content-Length or "chunked"): the firmware's own parser, unit-tested in lib/core
        wr::HttpResponse parsed;
        body_ = wr::parseHttpResponse(raw, parsed) ? parsed.body : std::string();
        if (chunked) size_ = -1;
        if (follow_ && !location.empty() && (code == 301 || code == 302 || code == 303 || code == 307 || code == 308)) {
            const std::string next = location.compare(0, 4, "http") == 0 ? location : url_.substr(0, slash) + location;
            if (wr::urlOrigin(next) != wr::urlOrigin(url_)) headers_.clear();  // no app header to another server
            url_ = next;
            continue;
        }
        return code;
    }
    return -1;
}

int HTTPClient::writeToStream(Stream* s) {
    return (int)s->write(reinterpret_cast<const uint8_t*>(body_.data()), body_.size());
}

// ------------------------------------------------------------------ LittleFS on a directory

namespace fs = std::filesystem;
static std::string fsPath(const char* p) { return pc::stateDir() + "/fs" + p; }

size_t File::size() const {
    if (!f_) return 0;
    const long cur = ftell(f_);
    fseek(f_, 0, SEEK_END);
    const long n = ftell(f_);
    fseek(f_, cur, SEEK_SET);
    return (size_t)n;
}
int File::available() const { return f_ ? (int)(size() - (size_t)ftell(f_)) : 0; }
int File::read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

bool LittleFSSim::begin(bool) {
    std::error_code e;
    fs::create_directories(fsPath(""), e);
    return fs::is_directory(fsPath(""));
}
bool LittleFSSim::exists(const char* p) { return fs::exists(fsPath(p)); }
bool LittleFSSim::mkdir(const char* p) {
    std::error_code e;
    return fs::create_directories(fsPath(p), e) || fs::is_directory(fsPath(p));
}
bool LittleFSSim::remove(const char* p) { return std::remove(fsPath(p).c_str()) == 0; }
bool LittleFSSim::format() {
    std::error_code e;
    fs::remove_all(fsPath(""), e);
    return begin();
}
File LittleFSSim::open(const char* p, const char* mode) {
    const bool write = mode && mode[0] == 'w';
    return File(fopen(fsPath(p).c_str(), write ? "wb" : "rb"));
}

