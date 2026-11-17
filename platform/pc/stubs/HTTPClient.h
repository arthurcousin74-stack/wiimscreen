// PC (simulator and desktop): a small HTTP/1.1 client over WiFiClient (relay in simreal, direct in desktop).
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "WiFi.h"

#define HTTP_CODE_OK 200
#define HTTP_CODE_MOVED_PERMANENTLY 301
#define HTTP_CODE_FOUND 302

enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS };

class HTTPClient {
public:
    bool begin(WiFiClient& client, const char* url);
    bool begin(WiFiClient& client, const String& url) { return begin(client, url.c_str()); }
    void end();
    void addHeader(const char* name, const char* value) { headers_.push_back({name, value}); }
    void setConnectTimeout(int ms) { connectMs_ = ms; }
    void setTimeout(uint32_t ms) { timeoutMs_ = ms; }
    void setReuse(bool) {}
    void setUserAgent(const char* ua) { agent_ = ua; }
    void setFollowRedirects(followRedirects_t f) { follow_ = f != HTTPC_DISABLE_FOLLOW_REDIRECTS; }
    void setRedirectLimit(int n) { redirects_ = n; }
    int GET() { return request("GET", nullptr, 0); }
    int POST(uint8_t* body, size_t n) { return request("POST", body, n); }
    int POST(const String& body) { return request("POST", reinterpret_cast<const uint8_t*>(body.c_str()), body.length()); }
    int getSize() const { return size_; }
    String getString() const { return String(body_); }
    int writeToStream(Stream* s);

private:
    int request(const char* method, const uint8_t* body, size_t n);
    WiFiClient* client_ = nullptr;
    std::string url_, agent_ = "ESP32HTTPClient", body_;
    std::vector<std::pair<std::string, std::string>> headers_;
    int connectMs_ = 5000, size_ = -1, redirects_ = 3;
    uint32_t timeoutMs_ = 5000;
    bool follow_ = false;
};
