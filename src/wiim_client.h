#pragma once

#include <Arduino.h>

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
    String host_;
    uint16_t port_ = 443;
    bool https_ = true;
    uint8_t pin_[32] = {};
    bool hasPin_ = false;
    String lastSeen_;
    bool justPinned_ = false;
};
