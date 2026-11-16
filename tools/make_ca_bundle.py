#!/usr/bin/env python3
"""Génère le paquet de certificats racines (format esp_crt_bundle d'ESP-IDF 5.4).

Sert à vérifier les serveurs HTTPS des pochettes d'album (Spotify, Qobuz,
Deezer, TuneIn…), dont les adresses varient selon le service.

Usage :
    curl -o certs/cacrt_all.pem https://raw.githubusercontent.com/espressif/esp-idf/master/components/mbedtls/esp_crt_bundle/cacrt_all.pem
    python tools/make_ca_bundle.py certs/cacrt_all.pem certs/extra_roots.pem certs/x509_crt_bundle.bin

Plusieurs fichiers PEM d'entrée : le dernier argument est la sortie. certs/extra_roots.pem contient les
racines ajoutées (avec la raison) ; tools/check_ca_bundle.py vérifie les serveurs utilisés.

Format (petit-boutiste) : table des positions de chaque certificat (u32,
une par certificat, le nombre se déduit de la première), puis pour chacun,
trié par nom : longueur du nom (u16), longueur de la clé (u16), nom du sujet
(DER), clé publique SubjectPublicKeyInfo (DER).
"""
import struct
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization


def main(srcs: list, dst: str) -> None:
    certs = []
    for src in srcs:
        with open(src, "rb") as f:
            certs += x509.load_pem_x509_certificates(f.read())
    unique = {c.fingerprint(hashes.SHA256()): c for c in certs}  # même certificat dans deux fichiers : une entrée
    certs = list(unique.values())
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
    body = b""
    offsets = []
    for name, key in entries:
        offsets.append(4 * len(entries) + len(body))
        body += struct.pack("<HH", len(name), len(key)) + name + key
    out = struct.pack("<%dL" % len(offsets), *offsets) + body
    with open(dst, "wb") as f:
        f.write(out)
    print(f"{len(entries)} certificats, {len(out)} octets -> {dst}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1:-1], sys.argv[-1])
