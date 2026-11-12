// Mise à jour par le réseau : tri des octets reçus (sans Arduino, testable sur PC).
#pragma once

#include <cstddef>
#include <cstdint>

namespace wr {

// Accepte les deux fichiers possibles :
//  - l'application seule (firmware.bin), qui commence par l'octet magique 0xE9
//  - l'image complète produite par la CI pour le flash USB (wiim-remote-tab5.bin) :
//    remplissage 0xFF, chargeur de démarrage à 0x2000, … application à 0x10000.
//    Seule la partie application (à partir de 0x10000) est gardée.
class FirmwareStream {
public:
    enum Kind { Unknown, App, Merged, Invalid };
    static constexpr size_t kAppOffset = 0x10000;

    // Morceau reçu → partie à écrire dans la partition (*out, renvoie sa longueur ; 0 : rien)
    size_t feed(const uint8_t* data, size_t len, const uint8_t** out);
    Kind kind() const { return kind_; }
    size_t received() const { return offset_; }
    size_t written() const { return written_; }

private:
    Kind kind_ = Unknown;
    size_t offset_ = 0, written_ = 0;
};

}  // namespace wr
