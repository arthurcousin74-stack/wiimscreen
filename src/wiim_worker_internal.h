#pragma once

// Interne aux fichiers wiim_worker*.cpp (pas l'interface de WiimWorker) : transport UPnP PlayQueue partagé
// entre eux. Définitions : wiim_worker_playqueue.cpp, sauf errorKey (wiim_worker.cpp).

#include <Arduino.h>

#include <algorithm>
#include <string>
#include <vector>

#include "wiim_client.h"

namespace wiimq {

extern const char* const kPlayQueue;  // "urn:schemas-wiimu-com:service:PlayQueue:1"
extern const size_t kMaxBody;         // taille maximale d'un corps de réponse gardé

// Flux qui garde au plus `cap` octets (HTTPClient gère « chunked » et Content-Length)
class CapStream : public Stream {
public:
    explicit CapStream(size_t cap) : cap_(cap) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* b, size_t n) override {
        size_t room = data.size() < cap_ ? cap_ - data.size() : 0;
        data.append(reinterpret_cast<const char*>(b), std::min(room, n));
        truncated |= n > room;
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    std::string data;
    bool truncated = false;

private:
    size_t cap_;
};

// GET ou POST SOAP en HTTP simple (l'UPnP du WiiM n'est pas chiffré)
int upnpRequest(const std::string& url, const char* soapAction, const std::string& body, std::string& out,
                bool* truncated = nullptr);
// Journal : lignes courtes, découpées si besoin ; `tag` : première ligne étiquetée (logTagged)
void logLong(const char* label, const std::string& text, const char* tag = nullptr, const char* kv = "");
// Contenu (QueueContext) de la file `name` ; code HTTP
int browseQueue(const std::string& control, const std::string& name, std::string& ctx);
std::vector<std::string> queueNames(const std::string& control, bool verbose);
int createQueue(const std::string& control, const std::string& ctx);
int playQueueAt(const std::string& control, const std::string& name, int index);
int deleteQueue(const std::string& control, const std::string& name);
int sendQueue(const std::string& control, const std::string& ctx, const std::string& listName, int& create);
// Valeur ASCII stable pour les lignes étiquetées du journal (logTagged, src/log.h)
const char* errorKey(WiimError e);

}  // namespace wiimq
