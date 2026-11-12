#include "wiim_client.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <memory>

static constexpr size_t kMaxResponse = 64 * 1024;
static constexpr uint32_t kReadTimeoutMs = 5000;

static constexpr uint32_t kIdleMs = 8000;  // au-delà, le WiiM a pu fermer : on reconnecte

void WiimClient::configure(const Settings& s) {
    disconnect();  // nouvelle adresse ou nouveau mode : nouvelle connexion
    std::string name;
    uint16_t port;
    https_ = s.https;
    wr::splitHostPort(s.host.c_str(), name, port, https_ ? 443 : 80);
    host_ = name.c_str();
    port_ = port;
    hasPin_ = https_ && wr::parseFingerprint(s.fingerprint.c_str(), pin_);
}

void WiimClient::disconnect() {
    if (conn_) conn_->stop();
    conn_.reset();
    secure_ = nullptr;
}

// Nouvelle connexion ; en TLS, l'empreinte du certificat est vérifiée (ou enregistrée
// au premier contact) avant tout envoi
WiimError WiimClient::connect() {
    disconnect();
    if (https_) {
        secure_ = new WiFiClientSecure();
        secure_->setInsecure();  // la confiance repose sur l'empreinte vérifiée ci-dessous
        secure_->setHandshakeTimeout(6);
        conn_.reset(secure_);
    } else {
        conn_.reset(new WiFiClient());
    }
    conn_->setTimeout(4000);            // ms (arduino-esp32 3.x)
    conn_->setConnectionTimeout(4000);  // ms
    if (!conn_->connect(host_.c_str(), port_)) {
        disconnect();
        return WiimError::Network;
    }
    if (secure_) {
        uint8_t fp[32];
        if (!secure_->getFingerprintSHA256(fp)) {
            disconnect();
            return WiimError::Network;
        }
        lastSeen_ = wr::toHex(fp, sizeof fp).c_str();
        if (!hasPin_) {
            memcpy(pin_, fp, sizeof fp);
            hasPin_ = true;
            saveFingerprint(lastSeen_);
            justPinned_ = true;
        } else if (!wr::constantTimeEq(fp, pin_, sizeof fp)) {
            disconnect();
            return WiimError::PinMismatch;
        }
    }
    return WiimError::None;
}

bool WiimClient::exchange(const String& cmd, std::string& raw, bool& reusable) {
    raw.clear();
    reusable = false;
    conn_->printf("GET /httpapi.asp?command=%s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\n\r\n",
                  cmd.c_str(), host_.c_str());
    // Lecture jusqu'à la fin de la réponse (longueur connue) ou, à défaut, jusqu'à la
    // fermeture : les serveurs embarqués ferment parfois sans close_notify ni
    // Content-Length. Certificat épinglé : au pire une réponse tronquée, jamais falsifiée.
    const uint32_t start = millis();
    uint8_t buf[512];
    while (millis() - start < kReadTimeoutMs && raw.size() < kMaxResponse) {
        int n = conn_->available();
        if (n > 0) {
            int r = conn_->read(buf, n < (int)sizeof buf ? n : (int)sizeof buf);
            if (r > 0) raw.append(reinterpret_cast<char*>(buf), r);
            wr::HttpFrame f = wr::httpFrame(raw);
            if (f.complete) {
                reusable = f.reusable;
                return true;
            }
            continue;
        }
        if (!conn_->connected()) break;
        delay(2);
    }
    return !raw.empty();
}

WiimError WiimClient::command(const String& cmd, std::string* body) {
    if (WiFi.status() != WL_CONNECTED) {
        disconnect();
        return WiimError::Network;
    }
    std::string raw;
    bool reusable = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool reused = conn_ && conn_->connected() && millis() - lastUse_ < kIdleMs;
        if (!reused) {
            WiimError e = connect();
            if (e != WiimError::None) return e;
        }
        if (exchange(cmd, raw, reusable)) break;
        // Aucun octet : connexion réutilisée fermée entre-temps par le WiiM. La requête
        // ne lui est pas parvenue : une seule nouvelle tentative, sur une connexion neuve.
        disconnect();
        if (!reused) return WiimError::Network;
    }
    lastUse_ = millis();
    if (!reusable) disconnect();

    wr::HttpResponse resp;
    if (!wr::parseHttpResponse(raw, resp) || resp.status != 200) {
        disconnect();
        return WiimError::Http;
    }
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

WiimError WiimClient::presets(std::vector<wr::Preset>& out) {
    std::string body;
    WiimError e = command("getPresetInfo", &body);
    if (e != WiimError::None) return e;
    return wr::parsePresetInfo(body, out) ? WiimError::None : WiimError::Http;
}
