// PC desktop build (Raspberry Pi, Linux, macOS, Windows): the Arduino network classes on the computer's own network.
//
// WiFiClient = a TCP socket; WiFiClientSecure = TLS with OpenSSL: public servers (Qobuz, Deezer, covers, OpenAI)
// are checked against the root certificates and the host name; the WiiM has a self-signed certificate, so
// wiim_client.cpp calls setInsecure() and checks the SHA-256 fingerprint itself (pinning, as on the Tab5).
// Root certificates: the system's store, or with PC_BUNDLED_CA (Windows) the PEM text that the platform gives
// (pc::bundledRootCerts: cacert.pem beside wiimscreen.exe, made from certs/ like the Tab5's bundle).
// WiFiUDP = a UDP socket for the SSDP discovery in src/net/discovery.cpp.
// Sockets: POSIX or Winsock through net_compat.h (one code path).
#include "net_compat.h"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <mutex>
#include <set>
#include <string>

#if OPENSSL_VERSION_NUMBER < 0x30000000L  // OpenSSL 1.1 (older Raspberry Pi OS / Debian)
#define SSL_get1_peer_certificate SSL_get_peer_certificate
#endif

#include "WiFi.h"
#include "WiFiUdp.h"
#include "pc.h"

namespace {

#ifdef PC_BUNDLED_CA
// Root certificates of a PEM text (certificates one after the other) into the store; returns how many
int addPemRoots(X509_STORE* store, const std::string& pem) {
    BIO* bio = pem.empty() ? nullptr : BIO_new_mem_buf(pem.data(), (int)pem.size());
    int n = 0;
    while (X509* cert = bio ? PEM_read_bio_X509(bio, nullptr, nullptr, nullptr) : nullptr) {
        if (X509_STORE_add_cert(store, cert) == 1) ++n;
        X509_free(cert);
    }
    ERR_clear_error();  // the end of the text reads as a "no start line" error
    if (bio) BIO_free(bio);
    return n;
}
#endif

SSL_CTX* sslContext() {
    static std::once_flag once;
    static SSL_CTX* ctx = nullptr;
    std::call_once(once, [] {
#ifdef PC_BUNDLED_CA
        // No openssl.cnf: the library's built-in folder (/ucrt64/etc/ssl with MSYS2) is C:\ucrt64\... on Windows,
        // a folder that any user can create. Must come before any other OpenSSL call.
        OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG, nullptr);
#endif
        ctx = SSL_CTX_new(TLS_client_method());
        if (ctx) {
            SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
#ifdef PC_BUNDLED_CA
            std::string where;
            const std::string pem = pc::bundledRootCerts(where);
            const int n = addPemRoots(SSL_CTX_get_cert_store(ctx), pem);
            if (n == 0) fprintf(stderr, "[W] aucun certificat racine dans %s : HTTPS public impossible\n", where.c_str());
            else Serial.printf("Certificats racines : %d (%s)\n", n, where.c_str());
#else
            SSL_CTX_set_default_verify_paths(ctx);  // the system's root certificates
#endif
        }
    });
    return ctx;
}

bool isIp(const char* host) {
    in6_addr a;
    return inet_pton(AF_INET, host, &a) == 1 || inet_pton(AF_INET6, host, &a) == 1;
}

// TCP connection with a time limit (non-blocking connect), then blocking reads with SO_RCVTIMEO
netc::sock tcpConnect(const char* host, uint16_t port, uint32_t connectMs) {
    netc::init();
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, std::to_string(port).c_str(), &hints, &res) != 0 || !res) return -1;
    netc::sock fd = -1;
    for (addrinfo* r = res; r && fd < 0; r = r->ai_next) {
        fd = netc::open(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (fd < 0) continue;
        netc::setBlocking(fd, false);
        int rc = ::connect(fd, r->ai_addr, (int)r->ai_addrlen);
        if (rc != 0 && netc::connectPending()) rc = netc::connected(fd, connectMs) ? 0 : -1;
        netc::setBlocking(fd, true);
        if (rc != 0) {
            netc::close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(res);
    return fd;
}

}  // namespace

// ------------------------------------------------------------------ TCP / TLS client

int WiFiClient::connect(const char* host, uint16_t port) {
    stop();
    eof_ = false;
    hasFingerprint_ = false;
    fd_ = tcpConnect(host, port, connectMs_);
    if (fd_ < 0) return 0;
    netc::setTimeouts(fd_, std::max(timeoutMs_, connectMs_));
    if (!tls_) {
        netc::setTimeouts(fd_, timeoutMs_);
        return 1;
    }
    SSL* ssl = sslContext() ? SSL_new(sslContext()) : nullptr;
    if (!ssl) {
        stop();
        return 0;
    }
    SSL_set_fd(ssl, (int)fd_);  // Windows: OpenSSL takes the SOCKET as an int as well
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
    if (!insecure_) {  // once per server: shows that the root certificates work (platform/pc/selftest.sh checks it)
        static std::mutex m;
        static std::set<std::string> seen;
        std::lock_guard<std::mutex> lock(m);
        if (seen.insert(host).second) Serial.printf("[tls] host=%s verified=1 | Certificat v\xC3\xA9rifi\xC3\xA9 : %s\n", host, host);
    }
    if (X509* cert = SSL_get1_peer_certificate(ssl)) {
        unsigned len = 0;
        hasFingerprint_ = X509_digest(cert, EVP_sha256(), fingerprint_, &len) == 1 && len == 32;
        X509_free(cert);
    }
    netc::setTimeouts(fd_, timeoutMs_);
    return 1;
}

size_t WiFiClient::write(const uint8_t* b, size_t n) {
    if (fd_ < 0) return 0;
    if (ssl_) {
        const int k = SSL_write(static_cast<SSL*>(ssl_), b, (int)n);
        return k > 0 ? (size_t)k : 0;
    }
    const int k = netc::sendBytes(fd_, b, n);
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
    return netc::pending(fd_);  // TLS: encrypted bytes waiting; read() then decrypts them
}

int WiFiClient::read(uint8_t* b, size_t n) {
    if (fd_ < 0) return -1;
    if (ssl_) {
        SSL* ssl = static_cast<SSL*>(ssl_);
        const int k = SSL_read(ssl, b, (int)n);
        if (k <= 0) {
            const int e = SSL_get_error(ssl, k);
            if (e == SSL_ERROR_ZERO_RETURN || (e == SSL_ERROR_SYSCALL && netc::lastError() == 0)) eof_ = true;
            return -1;
        }
        return k;
    }
    const int k = netc::recvBytes(fd_, b, n);
    if (k == 0) eof_ = true;
    return k > 0 ? k : -1;
}

int WiFiClient::read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

bool WiFiClient::connected() {
    if (fd_ < 0) return false;
    if (ssl_ && SSL_pending(static_cast<SSL*>(ssl_)) > 0) return true;
    if (eof_) return available() > 0;
    if (netc::readable(fd_, 0)) {
        char c;
        if (netc::recvBytes(fd_, &c, 1, MSG_PEEK) == 0) eof_ = true;  // readable: this peek does not wait
    }
    return !eof_ || available() > 0;
}

void WiFiClient::stop() {
    if (ssl_) SSL_free(static_cast<SSL*>(ssl_));
    ssl_ = nullptr;
    if (fd_ >= 0) netc::close(fd_);
    fd_ = -1;
}

// ------------------------------------------------------------------ UDP (SSDP discovery)

bool WiFiUDP::begin(uint16_t port) {
    stop();
    fd_ = netc::open(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ < 0) return false;
    sockaddr_in me{};
    me.sin_family = AF_INET;
    me.sin_port = htons(port);
    me.sin_addr.s_addr = htonl(INADDR_ANY);
#ifdef _WIN32
    const DWORD ttl = 2;  // Windows: a DWORD
    setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof ttl);
    // Windows often has virtual adapters (WSL, Hyper-V, VPN): send the search on the interface of the home
    // network (the one that reaches the Internet, as WiFi.localIP()), not on the first one Windows picks
    in_addr ifAddr{};
    if (inet_pton(AF_INET, WiFi.localIP().toString().c_str(), &ifAddr) == 1)
        setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&ifAddr), sizeof ifAddr);
#else
    const unsigned char ttl = 2;
    setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
#endif
    if (bind(fd_, (sockaddr*)&me, sizeof me) != 0) {
        stop();
        return false;
    }
    netc::setBlocking(fd_, false);
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
    return sendto(fd_, out_.data(), (int)out_.size(), 0, (sockaddr*)&to, sizeof to) == (int)out_.size() ? 1 : 0;
}

int WiFiUDP::parsePacket() {
    if (fd_ < 0) return 0;
    char buf[2048];
    const int n = netc::recvBytes(fd_, buf, sizeof buf);
    if (n <= 0) return 0;
    in_.assign(buf, n);
    return n;
}

int WiFiUDP::read(uint8_t* b, size_t n) {
    const size_t k = std::min(n, in_.size());
    std::memcpy(b, in_.data(), k);
    in_.erase(0, k);
    return (int)k;
}

void WiFiUDP::stop() {
    if (fd_ >= 0) netc::close(fd_);
    fd_ = -1;
}
