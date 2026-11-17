// PC: UDP for the SSDP discovery of src/discovery.cpp (desktop build), on POSIX sockets.
#pragma once
#include <string>

#include "WiFi.h"

class WiFiUDP {
public:
    ~WiFiUDP() { stop(); }
    bool begin(uint16_t port);
    int beginPacket(const IPAddress& ip, uint16_t port);
    size_t write(const uint8_t* b, size_t n);
    int endPacket();
    int parsePacket();
    int read(uint8_t* b, size_t n);
    int read(char* b, size_t n) { return read(reinterpret_cast<uint8_t*>(b), n); }
    void stop();

private:
    int fd_ = -1;
    std::string out_, in_, to_;
    uint16_t toPort_ = 0;
};
