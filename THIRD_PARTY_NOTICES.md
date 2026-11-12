# Third-party notices

wiimscreen's own code is released under the [MIT License](LICENSE).
It includes or builds with the following third-party components.

## Files in this repository

| Path | Component | License |
|---|---|---|
| `fonts/DejaVuSans.ttf` (and the fonts generated from it in `src/fonts/`) | DejaVu Sans | Bitstream Vera license + public-domain changes, see [`fonts/LICENSE-DejaVu.txt`](fonts/LICENSE-DejaVu.txt) |
| `certs/cacrt_all.pem` (and `certs/x509_crt_bundle.bin` generated from it) | Mozilla's root CA list (via ESP-IDF's `cacrt_all.pem`) | [Mozilla Public License 2.0](https://www.mozilla.org/MPL/2.0/) |

## Libraries compiled into the firmware

These are downloaded by PlatformIO at build time; they are not stored in this repository.

| Component | License |
|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified) | MIT |
| [M5GFX](https://github.com/m5stack/M5GFX) (includes pngle, QR code generator, TJpgDec) | MIT; pngle MIT; QR code MIT; TJpgDec: ChaN's permissive license |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) (via [pioarduino](https://github.com/pioarduino/platform-espressif32)) | LGPL-2.1-or-later |
| [ESP-IDF](https://github.com/espressif/esp-idf) (including mbedTLS, ESP-Hosted) | Apache-2.0 |

The firmware binaries published in Releases link the LGPL Arduino core. The complete source of this project
and of every library above is public, so anyone can rebuild and relink the firmware (`pio run -e tab5`).

## Online services used at runtime (optional)

- **OpenAI speech-to-text** (voice commands, only when an API key is configured): subject to OpenAI's terms.
- **[Radio Browser](https://www.radio-browser.info/)** (station search by voice): community radio directory, free API.
- **Album art and preset images** are downloaded from the URLs the WiiM reports (streaming services' CDNs).

## Trademarks

WiiM is a trademark of Linkplay Technology Inc. M5Stack is a trademark of M5Stack Technology Co., Ltd.
This project is independent and not affiliated with or endorsed by either company.
