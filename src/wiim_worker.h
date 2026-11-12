#pragma once

#include <Arduino.h>

#include <deque>
#include <vector>

#include "core.h"
#include "settings.h"
#include "wiim_client.h"

// Tout le réseau avec le WiiM passe par une tâche de fond (cœur du Wi-Fi) : chaque
// requête ouvre une connexion TLS (1 à 3 s sur l'ESP32-P4), ce qui figeait l'écran
// et faisait manquer des appuis quand c'était fait dans la boucle principale.
//
// La boucle principale ne bloque jamais : elle envoie des commandes (file d'attente),
// lit le dernier état relevé et les évènements (erreurs, presets, certificat…).

// État relevé
struct WiimReport {
    WiimError error = WiimError::None;
    bool online = false;
    wr::PlayerStatus player;  // titre/artiste/album complétés par getMetaInfo si metaOk
    wr::TrackMeta meta;
    bool metaOk = false;      // getMetaInfo lu à ce relevé (sinon garder le précédent)
    uint32_t at = 0;          // millis() à la fin du relevé
    uint32_t cmdSeq = 0;      // commandes déjà exécutées quand le relevé a commencé
};

struct WiimEvent {
    enum Kind { CommandDone, Presets, Pinned, PinMismatch, Moved, Identity };
    Kind kind = CommandDone;
    WiimError error = WiimError::None;
    String text;  // commande ; empreinte vue (PinMismatch) ; nouvelle adresse (Moved)
    bool ok = false;
    std::vector<wr::Preset> presets;
    String uuid, name;  // Identity
};

class WiimWorker {
public:
    // Démarre la tâche (copie de l'adresse, de l'uuid et de l'empreinte du WiiM)
    void begin(const Settings& s);

    // Commande à envoyer (non bloquant). Les changements de volume successifs encore
    // en attente sont fusionnés : seul le dernier part.
    void command(const String& cmd);
    void refreshPresets();
    void pollSoon();

    // Thread principal : dernier relevé (s'il est nouveau) et évènements
    bool takeReport(WiimReport& out);
    bool takeEvent(WiimEvent& out);
    uint32_t queued() const { return queued_; }  // commandes envoyées depuis le début

private:
    static void taskMain(void* self);
    void run();
    void execute(const String& cmd);
    void pollOnce();
    bool rediscover();
    void learnIdentity();
    void push(WiimEvent&& e);
    void notePinning(WiimError e);
    bool commandsWaiting();

    WiimClient client_;
    Settings cfg_;  // copie propre à la tâche (hôte, uuid, https, empreinte)
    SemaphoreHandle_t lock_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::deque<String> cmds_;
    uint32_t queued_ = 0;    // écrit par le thread principal seulement
    uint32_t executed_ = 0;  // sous verrou
    bool presetsWanted_ = false;
    bool pollWanted_ = false;
    bool hasReport_ = false;
    WiimReport report_;
    std::deque<WiimEvent> events_;
    uint32_t nextPoll_ = 0;
    int netFailures_ = 0;
    uint32_t lastRediscover_ = 0;
    bool identityChecked_ = false;
};
