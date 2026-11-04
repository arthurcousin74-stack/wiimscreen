#pragma once

#include <Arduino.h>

#include <deque>
#include <string>

// Image de pochette téléchargée (JPEG ou PNG), stockée en PSRAM.
struct ArtImage {
    std::string url;
    uint8_t* data = nullptr;
    size_t len = 0;

    ArtImage() = default;
    ArtImage(const ArtImage&) = delete;
    ArtImage& operator=(const ArtImage&) = delete;
    ArtImage(ArtImage&& o) noexcept { *this = std::move(o); }
    ArtImage& operator=(ArtImage&& o) noexcept;
    ~ArtImage() { reset(); }

    bool ok() const { return data && len; }
    void reset();
};

// Téléchargement des images (pochette en cours, images des presets) dans une
// tâche de fond : l'écran reste réactif pendant les transferts.
//
// HTTPS : les certificats des serveurs (Spotify, Qobuz, Deezer, TuneIn…) sont
// vérifiés avec le paquet de certificats racines Mozilla embarqué
// (certs/x509_crt_bundle.bin, voir tools/make_ca_bundle.py).
class ArtFetcher {
public:
    void begin();

    // Thread principal : ajoute une image à télécharger (ignorée si déjà en attente).
    // urgent : passe devant les autres (pochette de la piste en cours).
    void request(const std::string& url, bool urgent = false);

    // Thread principal : prochain téléchargement terminé. En cas d'échec, out.url
    // est renseignée mais out.ok() est faux : afficher l'image par défaut.
    bool poll(ArtImage& out);

private:
    static void taskMain(void* self);
    void run();

    SemaphoreHandle_t lock_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::deque<std::string> queue_;
    std::deque<ArtImage> done_;  // résultats non encore lus (les plus anciens sont jetés)
};
