#pragma once

#include <Arduino.h>

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

// Téléchargement des pochettes dans une tâche de fond : l'écran reste réactif
// pendant le transfert (souvent 50 à 300 Ko).
//
// HTTPS : les certificats des serveurs (Spotify, Qobuz, Deezer, TuneIn…) sont
// vérifiés avec le paquet de certificats racines Mozilla embarqué
// (certs/x509_crt_bundle.bin, voir tools/make_ca_bundle.py).
class ArtFetcher {
public:
    void begin();

    // Thread principal : demande une pochette (remplace toute demande en attente).
    void request(const std::string& url);

    // Thread principal : vrai si un résultat est prêt pour `url`. En cas d'échec,
    // `out` est vide (out.ok() faux) : afficher l'image par défaut.
    bool take(const std::string& url, ArtImage& out);

private:
    static void taskMain(void* self);
    void run();

    SemaphoreHandle_t lock_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::string pending_;
    bool hasPending_ = false;
    ArtImage result_;
    bool hasResult_ = false;
};
