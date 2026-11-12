# AGENTS.md

Guide for AI coding agents and human contributors working on wiimscreen.
What the project is: [README.md](README.md). WiiM API facts: [docs/WIIM_API_NOTES.md](docs/WIIM_API_NOTES.md).

## Commands

```bash
pip install platformio "click<8.2"   # click >= 8.2 breaks the platform's esptool (bootloader.bin step)
pio test -e native                   # unit tests of lib/core on the PC — run after any logic change
pio run -e tab5                      # firmware for the M5Stack Tab5
pio run -e preview                   # PC build of the real UI (needs SDL2 headers: libsdl2-dev)
SDL_VIDEODRIVER=dummy .pio/build/preview/program <images_dir> <out_dir> && python tools/preview/topng.py <out_dir>/*.ppm
```

Firmware version: CI passes `-DFW_VERSION="<run>-<sha>"` through `PLATFORMIO_BUILD_FLAGS`; local builds say `dev`.
Flashable image for USB (address `0x0`): see the merge step in `.github/workflows/firmware.yml`
(ESP32-P4 bootloader at **0x2000**, partitions 0x8000, boot_app0 0xe000, app 0x10000).

## Layout

| Path | Role |
|---|---|
| `lib/core/src/core.*` | WiiM response parsing, JSON helpers, layout (`computeLayout`), accent color, text wrapping |
| `lib/core/src/dsp.*` | FFT spectrum bands, visualizer smoothing, motion detector |
| `lib/core/src/voice.*` | Voice intent parsing (French), speech endpointing, Radio Browser parsing, WAV header |
| `lib/core/src/firmware.*` | OTA stream filter (accepts app-only or merged images) |
| `test/test_core/test_main.cpp` | Unity tests for everything in `lib/core` |
| `src/main.cpp` | Setup, main loop, polling, touch handling, ambient view, voice flow, menus |
| `src/ui.*` | All drawing (off-screen M5Canvas in PSRAM → display) |
| `src/wiim_client.*` | HTTPS client with certificate pinning, WiiM commands |
| `src/discovery.*` | SSDP + `getStatusEx` discovery |
| `src/art.*` | Background image downloader (FreeRTOS task, CA-verified HTTPS) |
| `src/voice_service.*` | OpenAI transcription, Radio Browser search |
| `src/portal.*` / `src/ota.*` | Setup portal (Wi-Fi AP + captive DNS) / OTA upload page |
| `src/settings.*` | NVS-backed settings |
| `tools/preview/` | PC preview harness (stubs for Arduino/M5Unified) |

## Conventions

- **Pure logic goes in `lib/core`** (no Arduino headers) with a test in `test/test_core/test_main.cpp`.
  `src/` stays thin glue around hardware and network.
- **Code comments and UI strings are in French** (the original author's language). Keep it consistent within a
  file; English is fine for new files. Non-ASCII UI text is written as UTF-8 escapes in C strings when needed.
- Colors are RGB888 `uint32_t` (an `uint16_t` would be read as RGB565 by M5GFX).
- UI changes: render them with `tools/preview` and look at the PNGs before committing; `docs/*.png` come from it.
- Layout dimensions scale with `sc(v) = v * min(width, height) / 540`; `checkLayout` tests guard overlaps and
  touch-target sizes for 1280×720, 720×1280 and 960×540.
- Never log or echo secrets (Wi-Fi password, OpenAI key). Validate anything that ends up in an HTTP header.

## Pitfalls already solved (don't reintroduce)

- **Arduino-ESP32 3.x**: `WiFiClient::setTimeout()` is in **milliseconds** (it was seconds in 2.x).
  `setCACertBundle()` needs the bundle size.
- **CA bundle format** changed in ESP-IDF 5.x (offset table + little-endian lengths): regenerate with
  `tools/make_ca_bundle.py`, never with an ESP-IDF 4.4 script.
- **Tab5 Wi-Fi** goes through an ESP32-C6 over SDIO: call `WiFi.setPins(12, 13, 11, 10, 9, 8, 15)` before any
  Wi-Fi call. WPS, DPP and Bluetooth are not available through this path in the current toolchain.
- **Microphone and speaker share I2S 0** on the Tab5: `sound::suspend()` before `M5.Mic.begin()`,
  `sound::resume()` after `M5.Mic.end()`.
- **M5GFX canvases with a loaded VLW font crash when destroyed** (seen on PC): the `Ui` object is never destroyed on
  the device; the preview exits with `_exit()`.
- 16-bit canvases are byte-swapped RGB565; the background gradient is Bayer-dithered to avoid banding.
- WiiM `getPlayerStatus` has no track info; `getMetaInfo` keys may carry a trailing space; preset `number` is an
  integer; `seek` takes seconds. Details in `docs/WIIM_API_NOTES.md`.
- `show()` must not draw over modal screens (voice listening, menu): guarded by `g_listening` / `g_menuOpen`.

## Status / not verified on hardware yet

Built and unit-tested; screens verified via the PC preview. Not yet confirmed on a physical Tab5: Wi-Fi via the
C6, microphones (visualizer, voice), speaker volume levels, SSDP discovery, `setPlayerCmd:play:<url>` encoding,
OTA end to end. Prefer small, testable changes and note what was verified on real hardware in commit messages.
