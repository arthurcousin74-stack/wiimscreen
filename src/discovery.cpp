#include "discovery.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>

#include <algorithm>

#include "core.h"

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
        int code = http.GET();
        int size = http.getSize();
        String body = (code == 200 && size < 16 * 1024) ? http.getString() : String();
        http.end();
        if (body.length()) {
            out = wr::parseStatusEx(body.c_str());
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
