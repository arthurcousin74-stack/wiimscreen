#include "wiim_client.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <memory>

static constexpr size_t kMaxResponse = 64 * 1024;
static constexpr uint32_t kReadTimeoutMs = 5000;

void WiimClient::configure(const Settings& s) {
    std::string name;
    uint16_t port;
    https_ = s.https;
    wr::splitHostPort(s.host.c_str(), name, port, https_ ? 443 : 80);
    host_ = name.c_str();
    port_ = port;
    hasPin_ = https_ && wr::parseFingerprint(s.fingerprint.c_str(), pin_);
}

WiimError WiimClient::command(const String& cmd, std::string* body) {
    if (WiFi.status() != WL_CONNECTED) return WiimError::Network;

    std::unique_ptr<WiFiClient> client;
    WiFiClientSecure* secure = nullptr;
    if (https_) {
        secure = new WiFiClientSecure();
        // La confiance repose sur l'empreinte vérifiée juste après la connexion
        secure->setInsecure();
        secure->setHandshakeTimeout(6);
        client.reset(secure);
    } else {
        client.reset(new WiFiClient());
    }
    client->setTimeout(4);  // secondes (arduino-esp32 2.x)

    if (!client->connect(host_.c_str(), port_)) return WiimError::Network;

    if (secure) {
        uint8_t fp[32];
        if (!secure->getFingerprintSHA256(fp)) {
            client->stop();
            return WiimError::Network;
        }
        lastSeen_ = wr::toHex(fp, sizeof fp).c_str();
        if (!hasPin_) {
            memcpy(pin_, fp, sizeof fp);
            hasPin_ = true;
            saveFingerprint(lastSeen_);
            justPinned_ = true;
        } else if (!wr::constantTimeEq(fp, pin_, sizeof fp)) {
            client->stop();
            return WiimError::PinMismatch;
        }
    }

    client->printf("GET /httpapi.asp?command=%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                   cmd.c_str(), host_.c_str());

    // Lecture jusqu'à fermeture : les serveurs embarqués ferment souvent sans
    // close_notify ni Content-Length. Certificat épinglé : au pire une réponse
    // tronquée, jamais falsifiée.
    std::string raw;
    uint32_t start = millis();
    uint8_t buf[512];
    while (millis() - start < kReadTimeoutMs && raw.size() < kMaxResponse) {
        int n = client->available();
        if (n > 0) {
            int r = client->read(buf, n < (int)sizeof buf ? n : (int)sizeof buf);
            if (r > 0) raw.append(reinterpret_cast<char*>(buf), r);
            continue;
        }
        if (!client->connected()) break;
        delay(5);
    }
    client->stop();

    wr::HttpResponse resp;
    if (!wr::parseHttpResponse(raw, resp) || resp.status != 200) return WiimError::Http;
    if (resp.body == "Failed" || resp.body.find("\"status\":\"Failed\"") != std::string::npos)
        return WiimError::Refused;
    if (body) *body = resp.body;
    return WiimError::None;
}

WiimError WiimClient::status(wr::PlayerStatus& out) {
    std::string body;
    WiimError e = command("getPlayerStatus", &body);
    if (e != WiimError::None) return e;
    out = wr::parsePlayerStatus(body);
    return out.ok ? WiimError::None : WiimError::Http;
}

WiimError WiimClient::metaInfo(wr::TrackMeta& out) {
    std::string body;
    WiimError e = command("getMetaInfo", &body);
    if (e != WiimError::None) return e;
    out = wr::parseMetaInfo(body);
    return out.ok ? WiimError::None : WiimError::Http;
}
