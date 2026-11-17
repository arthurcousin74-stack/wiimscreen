// Simulator, "real WiiM" build (env simreal): network and storage for the real src/ code.
//
// Every connection that the app opens (WiiM API over HTTPS, UPnP on port 49152, Qobuz, Deezer, covers) goes to
// the relay (tools/relay/wiim_relay.py), which opens it on the home network and passes the bytes both ways. The
// first line tells the relay where to go:
//   WIIMSIM/1 TCP <host> <port>   plain TCP           answer: OK
//   WIIMSIM/1 TLS <host> <port>   TLS by the relay    answer: OK <SHA-256 of the server certificate, hex>
//   WIIMSIM/1 DISCOVER            WiiMs on the LAN    answer: lines "ip<TAB>name<TAB>uuid", then the end
// or: ERR <reason>. So the app's own code (certificate pinning included) runs unchanged; only TLS is done by the
// relay. HTTP and files: platform/pc/pc_http_fs.cpp (shared with the desktop build).
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "WiFi.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#include "discovery.h"
#include "pc.h"

// ------------------------------------------------------------------ connection to the relay

static int openRelay(const std::string& firstLine, std::string& answer, uint32_t timeoutMs) {
    const std::string& addr = pc::relay();
    const size_t colon = addr.rfind(':');
    const std::string host = addr.substr(0, colon), port = colon == std::string::npos ? "8787" : addr.substr(colon + 1);
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) return -1;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd >= 0 && ::connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        static uint32_t warned = 0;
        if (!warned || millis() - warned > 30000) {
            fprintf(stderr, "Simulateur : relais injoignable sur %s (lance tools/relay/wiim_relay.py)\n", addr.c_str());
            warned = millis() | 1;
        }
        return -1;
    }
    timeval tv{(time_t)(timeoutMs / 1000), (suseconds_t)(timeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    const std::string line = firstLine + "\n";
    if (send(fd, line.data(), line.size(), MSG_NOSIGNAL) != (ssize_t)line.size()) {
        close(fd);
        return -1;
    }
    answer.clear();
    char c;
    while (recv(fd, &c, 1, 0) == 1 && c != '\n') answer += c;
    if (answer.compare(0, 2, "OK") != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int WiFiClient::connect(const char* host, uint16_t port) {
    stop();
    eof_ = false;
    hasFingerprint_ = false;
    std::string answer;
    fd_ = openRelay(std::string("WIIMSIM/1 ") + (tls_ ? "TLS " : "TCP ") + host + " " + std::to_string(port), answer,
                    std::max(timeoutMs_, connectMs_));
    if (fd_ < 0) return 0;
    if (tls_ && answer.size() >= 3 + 64) {
        for (int i = 0; i < 32; ++i) fingerprint_[i] = (uint8_t)std::strtoul(answer.substr(3 + i * 2, 2).c_str(), nullptr, 16);
        hasFingerprint_ = true;
    }
    timeval tv{(time_t)(timeoutMs_ / 1000), (suseconds_t)(timeoutMs_ % 1000) * 1000};
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    return 1;
}

size_t WiFiClient::write(const uint8_t* b, size_t n) {
    if (fd_ < 0) return 0;
    const ssize_t k = send(fd_, b, n, MSG_NOSIGNAL);
    return k > 0 ? (size_t)k : 0;
}

int WiFiClient::printf(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return (int)write(reinterpret_cast<const uint8_t*>(buf), std::min<size_t>(n, sizeof buf - 1));
}

int WiFiClient::available() {
    if (fd_ < 0) return 0;
    int n = 0;
    ioctl(fd_, FIONREAD, &n);
    return n;
}

int WiFiClient::read(uint8_t* b, size_t n) {
    if (fd_ < 0) return -1;
    const ssize_t k = recv(fd_, b, n, 0);
    if (k == 0) eof_ = true;
    return k > 0 ? (int)k : -1;
}

int WiFiClient::read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

bool WiFiClient::connected() {
    if (fd_ < 0 || eof_) return available() > 0;
    pollfd p{fd_, POLLIN, 0};
    if (poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP))) {
        char c;
        if (recv(fd_, &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0) eof_ = true;
    }
    return !eof_ || available() > 0;
}

void WiFiClient::stop() {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
}

// ------------------------------------------------------------------ discovery by the relay (SSDP + getStatusEx)

std::vector<WiimDevice> discoverWiims(uint32_t waitMs) {
    std::vector<WiimDevice> out;
    std::string answer;
    const int fd = openRelay("WIIMSIM/1 DISCOVER " + std::to_string(waitMs), answer, waitMs + 8000);
    if (fd < 0) return out;
    std::string data;
    char buf[1024];
    ssize_t k;
    while ((k = recv(fd, buf, sizeof buf, 0)) > 0) data.append(buf, k);
    close(fd);
    size_t p = 0;
    while (p < data.size()) {
        size_t e = data.find('\n', p);
        if (e == std::string::npos) e = data.size();
        const std::string line = data.substr(p, e - p);
        p = e + 1;
        const size_t t1 = line.find('\t'), t2 = line.find('\t', t1 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos) continue;
        out.push_back({line.substr(0, t1), line.substr(t1 + 1, t2 - t1 - 1), line.substr(t2 + 1)});
    }
    return out;
}
