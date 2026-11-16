#!/usr/bin/env python3
"""Checks that the Tab5 can verify the HTTPS servers it uses, with the roots of the firmware bundle.

ESP-IDF 5.4 (esp_crt_bundle) looks up the issuer of the TOP certificate of the chain sent by the server in the
bundle. A chain that ends with a cross-signed root whose issuer is missing fails on the Tab5 even if a desktop
browser accepts it (api.openai.com, 2026-11: "GTS Root R4" cross-signed by "GlobalSign Root CA").

Usage (needs openssl and the Python "cryptography" package, like make_ca_bundle.py):
    python tools/check_ca_bundle.py certs/cacrt_all.pem certs/extra_roots.pem
    python tools/check_ca_bundle.py certs/cacrt_all.pem certs/extra_roots.pem --hosts api.openai.com
Exit code 1 if a host fails.
"""
import subprocess
import sys

from cryptography import x509

HOSTS = ["api.openai.com", "www.qobuz.com", "static.qobuz.com", "api.deezer.com", "cdn-images.dzcdn.net",
         "e-cdns-images.dzcdn.net", "i.scdn.co", "cdn-profiles.tunein.com"]


def chain_of(host):
    out = subprocess.run(["openssl", "s_client", "-connect", f"{host}:443", "-servername", host, "-showcerts"],
                         input=b"", capture_output=True, timeout=20).stdout
    parts = out.split(b"-----BEGIN CERTIFICATE-----")[1:]
    return [x509.load_pem_x509_certificate(b"-----BEGIN CERTIFICATE-----" + p.split(b"-----END CERTIFICATE-----")[0] +
                                           b"-----END CERTIFICATE-----") for p in parts]


def main(args):
    if "--hosts" in args:
        i = args.index("--hosts")
        pems, hosts = args[:i], args[i + 1:]
    else:
        pems, hosts = args, HOSTS
    if not pems:
        sys.exit(__doc__)
    names = set()
    for p in pems:
        with open(p, "rb") as f:
            names |= {c.subject.public_bytes() for c in x509.load_pem_x509_certificates(f.read())}
    failed = 0
    for host in hosts:
        try:
            chain = chain_of(host)
        except Exception as e:  # pas de réseau, nom inconnu…
            print(f"SKIP {host}: {e.__class__.__name__}")
            continue
        if not chain:
            print(f"SKIP {host}: no certificate received")
            continue
        top = chain[-1]
        ok = top.issuer.public_bytes() in names
        failed += not ok
        print(("OK  " if ok else "FAIL"), host.ljust(26), "top issuer:", top.issuer.rfc4514_string()[:70])
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main(sys.argv[1:])
