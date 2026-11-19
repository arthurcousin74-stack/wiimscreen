// PC: UDP for the SSDP discovery of src/net/discovery.cpp (desktop build), on POSIX or Winsock sockets (net_direct.cpp).
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
    intptr_t fd_ = -1;  // socket (POSIX descriptor or Windows SOCKET; net_compat.h)
    std::string out_, in_, to_;
    uint16_t toPort_ = 0;
};
