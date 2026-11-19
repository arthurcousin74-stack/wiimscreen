// PC: Wi-Fi is always connected. Connections: through the relay in simreal (tools/sim/net_relay.cpp), direct
// with sockets and OpenSSL in desktop (net_direct.cpp); never opened in the fake-WiiM simulator.
#pragma once
#include "Arduino.h"

#define WL_CONNECTED 3
#define WIFI_STA 1
#define WIFI_AP_STA 3

struct IPAddress {
    String text = "192.168.1.42";
    IPAddress() = default;
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        char s[16];
        snprintf(s, sizeof s, "%u.%u.%u.%u", a, b, c, d);
        text = s;
    }
    String toString() const { return text; }
};

struct WiFiSim {
    void setPins(int, int, int, int, int, int, int) {}
    void begin(const char*, const char*) {}
    void mode(int) {}
    void setAutoReconnect(bool) {}
    void disconnect(bool = false) {}
    int status() const { return WL_CONNECTED; }
    IPAddress localIP() const;  // desktop: the computer's address on the network
};
extern WiFiSim WiFi;

// A TCP connection, opened by the relay (plain TCP, or TLS when the object is a WiFiClientSecure)
class WiFiClient {
public:
    WiFiClient() = default;
    WiFiClient(const WiFiClient&) = delete;
    WiFiClient& operator=(const WiFiClient&) = delete;
    virtual ~WiFiClient() { stop(); }
    int connect(const char* host, uint16_t port);
    size_t write(const uint8_t* b, size_t n);
    size_t print(const char* s) { return write(reinterpret_cast<const uint8_t*>(s), std::strlen(s)); }
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    int available();
    int read();
    int read(uint8_t* b, size_t n);
    bool connected();
    void stop();
    void setTimeout(uint32_t ms) { timeoutMs_ = ms; }
    void setConnectionTimeout(uint32_t ms) { connectMs_ = ms; }
    bool isSecure() const { return tls_; }

protected:
    bool tls_ = false;
    intptr_t fd_ = -1;  // socket (POSIX descriptor or Windows SOCKET; net_compat.h)
    bool eof_ = false;
    uint32_t timeoutMs_ = 5000, connectMs_ = 5000;
    uint8_t fingerprint_[32] = {};
    bool hasFingerprint_ = false;
    bool insecure_ = false;  // desktop build: no certificate check (the WiiM, pinned by the app)
    void* ssl_ = nullptr;    // desktop build: OpenSSL connection
};

class WiFiClientSecure : public WiFiClient {
public:
    WiFiClientSecure() { tls_ = true; }
    void setInsecure() { insecure_ = true; }      // the relay/OpenSSL checks public servers; the WiiM is pinned by the app
    void setCACertBundle(const uint8_t*, size_t) {}
    void setHandshakeTimeout(int) {}
    bool getFingerprintSHA256(uint8_t out[32]) const {
        if (hasFingerprint_) std::memcpy(out, fingerprint_, 32);
        return hasFingerprint_;
    }
};
