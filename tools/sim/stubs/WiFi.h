// Simulator: Wi-Fi is always connected (the fake WiiM is in the same process).
#pragma once
#include "Arduino.h"

#define WL_CONNECTED 3
#define WIFI_STA 1
#define WIFI_AP_STA 3

struct IPAddress {
    String text = "192.168.1.42";
    String toString() const { return text; }
};

struct WiFiSim {
    void setPins(int, int, int, int, int, int, int) {}
    void begin(const char*, const char*) {}
    void mode(int) {}
    void setAutoReconnect(bool) {}
    void disconnect(bool = false) {}
    int status() const { return WL_CONNECTED; }
    IPAddress localIP() const { return IPAddress(); }
};
extern WiFiSim WiFi;

class WiFiClient {
public:
    virtual ~WiFiClient() = default;
};
class WiFiClientSecure : public WiFiClient {};
