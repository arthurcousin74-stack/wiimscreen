#pragma once

#include <Arduino.h>

#include <string>
#include <vector>

// Recherche des lecteurs WiiM sur le Wi-Fi.
//
// 1. Requête SSDP (UPnP) : chaque lecteur multimédia du réseau répond avec son adresse.
// 2. Chaque réponse est interrogée avec getStatusEx (API du WiiM) : seuls les WiiM /
//    LinkPlay répondent, avec leur nom et leur identifiant unique (uuid).
// Lecture seule : rien n'est commandé ici. Le contrôle passe ensuite par le client
// habituel, avec l'empreinte TLS enregistrée.
struct WiimDevice {
    std::string ip;
    std::string name;
    std::string uuid;
};

std::vector<WiimDevice> discoverWiims(uint32_t waitMs = 2500);
