// PC desktop build (Raspberry Pi, Linux, macOS): the Arduino network classes on the computer's own network.
//
// WiFiClient = a TCP socket; WiFiClientSecure = TLS with OpenSSL: public servers (Qobuz, Deezer, covers, OpenAI)
// are checked against the system's certificate store and host name; the WiiM has a self-signed certificate, so
// wiim_client.cpp calls setInsecure() and checks the SHA-256 fingerprint itself (pinning, as on the Tab5).
// WiFiUDP = a UDP socket for the SSDP discovery in src/net/discovery.cpp.
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <mutex>

#ifndef MSG_NOSIGNAL  // macOS: SIGPIPE is ignored for the whole program instead (pc_platform.cpp)
#define MSG_NOSIGNAL 0
#endif
#if OPENSSL_VERSION_NUMBER < 0x30000000L  // OpenSSL 1.1 (older Raspberry Pi OS / Debian)
#define SSL_get1_peer_certificate SSL_get_peer_certificate
#endif

#include "WiFi.h"
#include "WiFiUdp.h"

namespace {

SSL_CTX* sslContext() {
    static std::once_flag once;
    static SSL_CTX* ctx = nullptr;
    std::call_once(once, [] {
        ctx = SSL_CTX_new(TLS_client_method());
        if (ctx) {
            SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
            SSL_CTX_set_default_verify_paths(ctx);  // the system's root certificates
        }
    });
    return ctx;
}

bool isIp(const char* host) {
    in6_addr a;
    return inet_pton(AF_INET, host, &a) == 1 || inet_pton(AF_INET6, host, &a) == 1;
}

// TCP connection with a time limit (non-blocking connect), then blocking reads with SO_RCVTIMEO
int tcpConnect(const char* host, uint16_t port, uint32_t connectMs) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &res) != 0 || !res) return -1;
    int fd = -1;
    for (addrinfo* r = res; r && fd < 0; r = r->ai_next) {
        fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (fd < 0) continue;
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = ::connect(fd, r->ai_addr, r->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            pollfd p{fd, POLLOUT, 0};
            int err = 0;
            socklen_t len = sizeof err;
            rc = poll(&p, 1, (int)connectMs) == 1 && getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && !err ? 0 : -1;
        }
        fcntl(fd, F_SETFL, flags);
        if (rc != 0) {
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(res);
    return fd;
}

void setRecvTimeout(int fd, uint32_t ms) {
    timeval tv{(time_t)(ms / 1000), (suseconds_t)(ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

}  // namespace

// ------------------------------------------------------------------ TCP / TLS client

int WiFiClient::connect(const char* host, uint16_t port) {
    stop();
    eof_ = false;
    hasFingerprint_ = false;
    fd_ = tcpConnect(host, port, connectMs_);
    if (fd_ < 0) return 0;
    setRecvTimeout(fd_, std::max(timeoutMs_, connectMs_));
    if (!tls_) {
        setRecvTimeout(fd_, timeoutMs_);
        return 1;
    }
    SSL* ssl = sslContext() ? SSL_new(sslContext()) : nullptr;
    if (!ssl) {
        stop();
        return 0;
    }
    SSL_set_fd(ssl, fd_);
    if (!isIp(host)) SSL_set_tlsext_host_name(ssl, host);  // SNI
    if (insecure_) {
        SSL_set_verify(ssl, SSL_VERIFY_NONE, nullptr);
    } else {
        SSL_set_verify(ssl, SSL_VERIFY_PEER, nullptr);
        if (isIp(host)) X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), host);
        else SSL_set1_host(ssl, host);
    }
    ssl_ = ssl;
    if (SSL_connect(ssl) != 1) {
        fprintf(stderr, "[W] TLS %s:%u : %s\n", host, port,
                insecure_ ? "\xC3\xA9" "chec" : X509_verify_cert_error_string(SSL_get_verify_result(ssl)));
        stop();
        return 0;
    }
    if (X509* cert = SSL_get1_peer_certificate(ssl)) {
        unsigned len = 0;
        hasFingerprint_ = X509_digest(cert, EVP_sha256(), fingerprint_, &len) == 1 && len == 32;
        X509_free(cert);
    }
    setRecvTimeout(fd_, timeoutMs_);
    return 1;
}

size_t WiFiClient::write(const uint8_t* b, size_t n) {
    if (fd_ < 0) return 0;
    if (ssl_) {
        const int k = SSL_write(static_cast<SSL*>(ssl_), b, (int)n);
        return k > 0 ? (size_t)k : 0;
    }
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
    if (ssl_) {
        const int p = SSL_pending(static_cast<SSL*>(ssl_));
        if (p > 0) return p;
    }
    int n = 0;
    ioctl(fd_, FIONREAD, &n);  // TLS: encrypted bytes waiting; read() then decrypts them
    return n;
}

int WiFiClient::read(uint8_t* b, size_t n) {
    if (fd_ < 0) return -1;
    if (ssl_) {
        SSL* ssl = static_cast<SSL*>(ssl_);
        const int k = SSL_read(ssl, b, (int)n);
        if (k <= 0) {
            const int e = SSL_get_error(ssl, k);
            if (e == SSL_ERROR_ZERO_RETURN || (e == SSL_ERROR_SYSCALL && errno == 0)) eof_ = true;
            return -1;
        }
        return k;
    }
    const ssize_t k = recv(fd_, b, n, 0);
    if (k == 0) eof_ = true;
    return k > 0 ? (int)k : -1;
}

int WiFiClient::read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

bool WiFiClient::connected() {
    if (fd_ < 0) return false;
    if (ssl_ && SSL_pending(static_cast<SSL*>(ssl_)) > 0) return true;
    if (eof_) return available() > 0;
    pollfd p{fd_, POLLIN, 0};
    if (poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP))) {
        char c;
        if (recv(fd_, &c, 1, MSG_PEEK | MSG_DONTWAIT) == 0) eof_ = true;
    }
    return !eof_ || available() > 0;
}

void WiFiClient::stop() {
    if (ssl_) SSL_free(static_cast<SSL*>(ssl_));
    ssl_ = nullptr;
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
}

// ------------------------------------------------------------------ UDP (SSDP discovery)

bool WiFiUDP::begin(uint16_t port) {
    stop();
    fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ < 0) return false;
    sockaddr_in me{};
    me.sin_family = AF_INET;
    me.sin_port = htons(port);
    me.sin_addr.s_addr = htonl(INADDR_ANY);
    const unsigned char ttl = 2;
    setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    if (bind(fd_, (sockaddr*)&me, sizeof me) != 0) {
        stop();
        return false;
    }
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    return true;
}

int WiFiUDP::beginPacket(const IPAddress& ip, uint16_t port) {
    out_.clear();
    to_ = ip.toString().c_str();
    toPort_ = port;
    return 1;
}

size_t WiFiUDP::write(const uint8_t* b, size_t n) {
    out_.append(reinterpret_cast<const char*>(b), n);
    return n;
}

int WiFiUDP::endPacket() {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(toPort_);
    if (fd_ < 0 || inet_pton(AF_INET, to_.c_str(), &to.sin_addr) != 1) return 0;
    return sendto(fd_, out_.data(), out_.size(), 0, (sockaddr*)&to, sizeof to) == (ssize_t)out_.size() ? 1 : 0;
}

int WiFiUDP::parsePacket() {
    if (fd_ < 0) return 0;
    char buf[2048];
    const ssize_t n = recv(fd_, buf, sizeof buf, 0);
    if (n <= 0) return 0;
    in_.assign(buf, n);
    return (int)n;
}

int WiFiUDP::read(uint8_t* b, size_t n) {
    const size_t k = std::min(n, in_.size());
    std::memcpy(b, in_.data(), k);
    in_.erase(0, k);
    return (int)k;
}

void WiFiUDP::stop() {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
}
