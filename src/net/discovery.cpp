#include "net/discovery.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>

#include <algorithm>

#include "core.h"

// Corps de réponse gardé jusqu'à `cap` octets (« chunked » compris : getSize() vaut alors -1 et getString() n'aurait
// pas de limite). N'importe quel appareil du réseau peut répondre à la recherche SSDP.
class CappedBody : public Stream {
public:
    explicit CappedBody(size_t cap) : cap_(cap) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* b, size_t n) override {
        if (data.size() < cap_) data.append(reinterpret_cast<const char*>(b), std::min(n, cap_ - data.size()));
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    std::string data;

private:
    size_t cap_;
};

// getStatusEx en HTTPS (WiiM) puis en HTTP (anciens LinkPlay) : identité du lecteur
static bool identify(const std::string& ip, wr::WiimIdentity& out) {
    for (bool https : {true, false}) {
        WiFiClientSecure secure;
        WiFiClient plain;
        if (https) {
            secure.setInsecure();  // identification seulement ; le contrôle vérifie l'empreinte
            secure.setHandshakeTimeout(4);
        }
        HTTPClient http;
        http.setConnectTimeout(2500);
        http.setTimeout(3000);
        http.setReuse(false);
        String url = String(https ? "https://" : "http://") + ip.c_str() + "/httpapi.asp?command=getStatusEx";
        if (!http.begin(https ? static_cast<WiFiClient&>(secure) : plain, url)) continue;
        const int code = http.GET();
        const int size = http.getSize();  // -1 : inconnue (« chunked »)
        CappedBody body(16 * 1024);
        if (code == 200 && size < 16 * 1024) http.writeToStream(&body);
        http.end();
        if (!body.data.empty()) {
            out = wr::parseStatusEx(body.data);
            if (out.ok) return true;
        }
    }
    return false;
}

std::vector<WiimDevice> discoverWiims(uint32_t waitMs) {
    std::vector<WiimDevice> found;
    if (WiFi.status() != WL_CONNECTED) return found;

    WiFiUDP udp;
    if (!udp.begin(0)) return found;  // port local quelconque : les réponses arrivent en direct
    const IPAddress group(239, 255, 255, 250);
    const char* search =
        "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n";

    std::vector<std::string> ips;
    const uint32_t start = millis();
    uint32_t lastSend = 0;
    int sent = 0;
    char buf[1024];
    while (millis() - start < waitMs) {
        if (sent < 3 && millis() - lastSend > 700) {  // UDP : la requête peut se perdre, on la répète
            udp.beginPacket(group, 1900);
            udp.write(reinterpret_cast<const uint8_t*>(search), strlen(search));
            udp.endPacket();
            lastSend = millis();
            ++sent;
        }
        int n = udp.parsePacket();
        if (n > 0) {
            int len = udp.read(buf, sizeof buf - 1);
            if (len > 0) {
                buf[len] = 0;
                std::string ip;
                if (wr::ssdpHost(buf, ip) && std::find(ips.begin(), ips.end(), ip) == ips.end() && ips.size() < 12)
                    ips.push_back(ip);
            }
        }
        delay(10);
    }
    udp.stop();

    for (auto& ip : ips) {
        wr::WiimIdentity id;
        if (identify(ip, id)) found.push_back(WiimDevice{ip, id.name, id.uuid});
    }
    std::sort(found.begin(), found.end(), [](const WiimDevice& a, const WiimDevice& b) { return a.name < b.name; });
    return found;
}
