#pragma once

#include <Arduino.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <memory>
#include <string>

#include "core.h"
#include "settings.h"

enum class WiimError {
    None,
    Network,      // WiiM injoignable
    PinMismatch,  // certificat différent de celui enregistré
    Http,         // réponse inattendue
    Refused,      // le WiiM a répondu "Failed"
};

// Client de l'API HTTP locale du WiiM (httpapi.asp).
//
// Connexion gardée ouverte (keep-alive) : la poignée de main TLS (1 à 3 s sur
// l'ESP32-P4) n'a lieu qu'à la première requête ; les suivantes réutilisent la
// connexion (~0,1 s). Si le WiiM ne la garde pas, retour automatique à une
// connexion par requête. Utilisé par une seule tâche (WiimWorker).
//
// TLS : le WiiM présente un certificat auto-signé, impossible à valider par une
// chaîne de confiance. La connexion est établie sans validation de chaîne, puis
// l'empreinte SHA-256 du certificat est comparée à celle enregistrée AVANT
// l'envoi de toute requête. Sans empreinte enregistrée, la première connexion
// l'enregistre (trust on first use).
class WiimClient {
public:
    void configure(const Settings& s);

    WiimError command(const String& cmd, std::string* body = nullptr);
    WiimError status(wr::PlayerStatus& out);
    // Album, adresse de la pochette, qualité audio de la piste en cours
    WiimError metaInfo(wr::TrackMeta& out);
    // Presets enregistrés sur le WiiM : numéro, nom, image
    WiimError presets(std::vector<wr::Preset>& out);

    // Empreinte vue lors de la dernière poignée de main (utile en cas de changement)
    const String& lastSeenFingerprint() const { return lastSeen_; }
    // Vrai une seule fois, juste après l'enregistrement d'une nouvelle empreinte
    bool takeJustPinned() {
        bool v = justPinned_;
        justPinned_ = false;
        return v;
    }

private:
    WiimError connect();
    void disconnect();
    // Envoie la requête et lit la réponse ; false si aucun octet n'est revenu
    bool exchange(const String& cmd, std::string& raw, bool& reusable);

    std::unique_ptr<WiFiClient> conn_;
    WiFiClientSecure* secure_ = nullptr;  // conn_ vu comme client TLS (même objet)
    uint32_t lastUse_ = 0;
    String host_;
    uint16_t port_ = 443;
    bool https_ = true;
    uint8_t pin_[32] = {};
    bool hasPin_ = false;
    String lastSeen_;
    bool justPinned_ = false;
};
