#!/usr/bin/env python3
"""Génère le paquet de certificats racines (format esp_crt_bundle d'ESP-IDF 4.4).

Sert à vérifier les serveurs HTTPS des pochettes d'album (Spotify, Qobuz,
Deezer, TuneIn…), dont les adresses varient selon le service.

Usage :
    curl -o certs/cacrt_all.pem https://raw.githubusercontent.com/espressif/esp-idf/master/components/mbedtls/esp_crt_bundle/cacrt_all.pem
    python tools/make_ca_bundle.py certs/cacrt_all.pem certs/x509_crt_bundle.bin

Format : nombre de certificats (u16 BE), puis pour chacun, trié par nom :
longueur du nom (u16 BE), longueur de la clé (u16 BE), nom du sujet (DER),
clé publique SubjectPublicKeyInfo (DER).
"""
import struct
import sys

from cryptography import x509
from cryptography.hazmat.primitives import serialization


def main(src: str, dst: str) -> None:
    with open(src, "rb") as f:
        certs = x509.load_pem_x509_certificates(f.read())
    if not certs:
        sys.exit("aucun certificat trouvé")
    entries = []
    for c in certs:
        name = c.subject.public_bytes()
        key = c.public_key().public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
        )
        entries.append((name, key))
    entries.sort(key=lambda e: e[0])  # recherche dichotomique côté ESP32
    out = struct.pack(">H", len(entries))
    for name, key in entries:
        out += struct.pack(">HH", len(name), len(key)) + name + key
    with open(dst, "wb") as f:
        f.write(out)
    print(f"{len(entries)} certificats, {len(out)} octets -> {dst}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
