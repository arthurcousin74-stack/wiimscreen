# Security

## Reporting a vulnerability

Please **do not open a public issue** for security problems. Use GitHub's
[private vulnerability reporting](../../security/advisories/new) for this repository. You will get an answer as soon
as possible; this is a hobby project maintained on a best-effort basis.

## Threat model in short

wiimscreen runs on a home network and talks to a WiiM player, a few HTTPS services and the user's phone/browser.

| Area | Protection |
|---|---|
| WiiM control | HTTPS; the WiiM's self-signed certificate is pinned (SHA-256) on first contact and checked before every request. Following the WiiM to a new IP (discovery) keeps the pin and moves only after a connection to the new address proves the same certificate. With the *HTTP* option (not recommended) there is no pinning. |
| Album art, OpenAI, Deezer, Qobuz | HTTPS verified against Mozilla's root CAs (`certs/`); downloads size-capped; only JPEG/PNG decoded, after checking the file-type bytes. |
| Setup portal | Only started from the device (first boot, failed Wi-Fi, or the *Réglages* menu); Wi-Fi access point protected by a random 10-character password shown only on screen; the Wi-Fi password and OpenAI key are never sent back to the page. |
| OTA update | Only started from the device; random 6-digit code shown on screen; 5 attempts and 10 minutes max; the image is written to the inactive slot and verified by the ESP32 before activation. Images are **not signed**: anyone who can see the screen and reach the device on the LAN during update mode can install firmware. |
| WiiM play queues (UPnP) | Plain HTTP SOAP on the LAN (port 49152), like the WiiM Home app; read/write of play queues only. The Qobuz account name is masked in logs, but saved favorites and queue models keep the WiiM's queue XML (which contains it) in LittleFS. |
| Qobuz and Deezer access | The WiiM hands out its Qobuz app ID and user token to any LAN client (UPnP `GetUserInfo`, no authentication — a property of the WiiM, not of wiimscreen). wiimscreen reads them only with the opt-in *direct Qobuz search* option, keeps them **in RAM only** (never in NVS, LittleFS, logs or on screen), sends them only to `www.qobuz.com` over verified HTTPS, and asks again when Qobuz refuses them. Deezer needs no token (public API). |
| Simulator relay (`tools/relay`, development only) | Listens on 127.0.0.1 by default; connects only to private addresses and to public hosts on 443 (TLS) or 80 (HTTP), never to this computer, link-local (169.254.x), multicast or reserved addresses; checks the certificate of every host name (only a private IP address — the WiiM — is accepted unchecked, pinned by the app); resolves each name once; logs host and port only. Not part of the firmware. |
| Computer version (`desktop`) | TLS with OpenSSL: public servers checked against the system's certificates and host name; the WiiM pinned as on the Tab5. Settings (including the OpenAI key) and favorites are plain files in `~/.config/wiimscreen`, readable by the user only (umask 077). |
| Deezer profile number | Optional setting (NVS and the LittleFS settings copy). It is not a secret: it only reads favorites that the profile already makes public on `api.deezer.com`. |
| Microphone | Active only in the ambient view (visualizer, processed on-device) or while dictating a search (audio sent to OpenAI only then). |
| Secrets | Stored in the ESP32's NVS (not encrypted), with a backup copy in LittleFS (`/settings.bak`, not encrypted) so that a full USB flash does not lose them; never logged. An OpenAI key with spending limits is recommended. "Erase flash" removes both. |

Known limitations worth improving: trust-on-first-use for the WiiM certificate, unsigned OTA images, NVS encryption. Cover URLs come from the playing stream's metadata, so the device may fetch an address of your network chosen by an AirPlay/DLNA sender (a blind GET; only JPEG/PNG answers are shown).
