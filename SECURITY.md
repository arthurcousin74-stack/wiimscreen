# Security

## Reporting a vulnerability

Please **do not open a public issue** for security problems. Use GitHub's
[private vulnerability reporting](../../security/advisories/new) for this repository. You will get an answer as soon
as possible; this is a hobby project maintained on a best-effort basis.

## Threat model in short

wiimscreen runs on a home network and talks to a WiiM player, a few HTTPS services and the user's phone/browser.

| Area | Protection |
|---|---|
| WiiM control | HTTPS; the WiiM's self-signed certificate is pinned (SHA-256) on first contact and checked before every request. Following the WiiM to a new IP (discovery) still requires the same certificate. |
| Album art, OpenAI, Deezer, Qobuz | HTTPS verified against Mozilla's root CAs (`certs/`); downloads size-capped; only JPEG/PNG decoded, after checking the file-type bytes. |
| Setup portal | Only started from the device (first boot, failed Wi-Fi, or the *Réglages* menu); Wi-Fi access point protected by a random 10-character password shown only on screen; the Wi-Fi password and OpenAI key are never sent back to the page. |
| OTA update | Only started from the device; random 6-digit code shown on screen; 5 attempts and 10 minutes max; the image is written to the inactive slot and verified by the ESP32 before activation. Images are **not signed**: anyone who can see the screen and reach the device on the LAN during update mode can install firmware. |
| WiiM play queues (UPnP) | Plain HTTP SOAP on the LAN (port 49152), like the WiiM Home app; read/write of play queues only. The Qobuz account name is masked in logs. |
| Qobuz access | The WiiM hands out its Qobuz app ID and user token to any LAN client (UPnP `GetUserInfo`, no authentication — a property of the WiiM, not of wiimscreen). wiimscreen keeps them **in RAM only** (never in NVS, LittleFS, logs or on screen), sends them only to `www.qobuz.com` over verified HTTPS, and asks again when Qobuz refuses them. |
| Microphone | Active only in the ambient view (visualizer, processed on-device) or while dictating a search (audio sent to OpenAI only then). |
| Secrets | Stored in the ESP32's NVS (not encrypted); never logged. An OpenAI key with spending limits is recommended. |

Known limitations worth improving: trust-on-first-use for the WiiM certificate, unsigned OTA images, NVS encryption.
