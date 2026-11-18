#pragma once

#include <stddef.h>
#include <stdint.h>

// Paquet de certificats racines Mozilla embarqué (board_build.embed_files dans
// platformio.ini, généré par tools/make_ca_bundle.py) : vérifie les serveurs HTTPS
// (images, reconnaissance vocale, annuaire de radios).
extern const uint8_t ca_bundle_start[] asm("_binary_certs_x509_crt_bundle_bin_start");
extern const uint8_t ca_bundle_end[] asm("_binary_certs_x509_crt_bundle_bin_end");

inline size_t caBundleSize() { return static_cast<size_t>(ca_bundle_end - ca_bundle_start); }
