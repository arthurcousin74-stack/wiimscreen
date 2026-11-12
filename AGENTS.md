# AGENTS.md

Guide for AI coding agents and human contributors working on wiimscreen.
What the project is: [README.md](README.md) — a WiiM remote for ESP32 touchscreen boards; **only the M5Stack Tab5
has been built for and tested**, so "the device" below means the Tab5 unless stated otherwise. WiiM API facts: [docs/WIIM_API_NOTES.md](docs/WIIM_API_NOTES.md).

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
| `lib/core/src/upnp.*` | XML/SOAP helpers, UPnP description/SCPD parsing, WiiM PlayQueue `QueueContext` parsing, secret masking |
| `test/test_core/test_main.cpp` | Unity tests for everything in `lib/core` |
| `src/main.cpp` | Setup, main loop (touch, drawing, applying WiiM state), ambient view, voice flow, menus |
| `src/wiim_worker.*` | Background task that owns all WiiM networking: polling, command queue, presets, discovery |
| `src/ui.*` | All drawing (off-screen M5Canvas in PSRAM → display) |
| `src/wiim_client.*` | HTTPS client with certificate pinning, WiiM commands |
| `src/discovery.*` | SSDP + `getStatusEx` discovery |
| `src/art.*` | Background image downloader (FreeRTOS task, CA-verified HTTPS) |
| `src/voice_service.*` | OpenAI transcription, Radio Browser search |
| `src/portal.*` / `src/ota.*` | Setup portal (Wi-Fi AP + captive DNS) / OTA upload page |
| `src/web_style.h` | Shared self-contained CSS for both web pages (no external resources: the phone is offline on the AP) |
| `src/settings.*` | NVS-backed settings |
| `src/favorites.*` | Favorites: copies of the WiiM play queue (UPnP PlayQueue) in LittleFS, replayed with `CreateQueue` |
| `tools/preview/` | PC preview harness (stubs for Arduino/M5Unified) |

## Conventions

- **Pure logic goes in `lib/core`** (no Arduino headers) with a test in `test/test_core/test_main.cpp`.
  `src/` stays thin glue around hardware and network.
- **Code comments and UI strings are in French** (the original author's language). Keep it consistent within a
  file; English is fine for new files. Non-ASCII UI text is written as UTF-8 escapes in C strings when needed.
- **README.md is written in ASD-STE100 Simplified Technical English** (short sentences, active voice, one
  instruction per numbered step). Keep that style when you edit it.
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
- **Never call the WiiM from the main loop.** Each request opens a TLS connection (1–3 s on the P4); doing it in
  `loop()` froze the screen and dropped taps. Use `g_worker.command()` (non-blocking), update `g_state`
  optimistically, and let `serviceWiim()` apply reports and errors. `WiimClient` keeps the TLS connection open
  (keep-alive, closed after 8 s idle; `httpFrame()` in `lib/core` decides when a response is complete) and retries
  once on a fresh connection if a reused one returns nothing. Reports that started before the latest command
  are discarded (`cmdSeq`) so they don't undo the optimistic state.
- Navigation bar: `Layout::rail/navHome/navSettings/content`; screens that show it call `Ui::navRail()` and lay
  out their content inside `layout_.content`. `hitHome()`/`hitSettings()` only match while it is shown.
- `show()` only marks the screen dirty; `renderNow()` draws once per loop. It must not draw over modal screens
  (voice, menu, touch test, log): guarded by `modalOpen()`. The 1 Hz progress tick redraws only the progress strip.

## Status / not verified on hardware yet

Built and unit-tested; screens verified via the PC preview. No other board has been tried. Not yet confirmed on a physical Tab5: Wi-Fi via the
C6, microphones (visualizer, voice), speaker volume levels, SSDP discovery, `setPlayerCmd:play:<url>` encoding,
OTA end to end, HTTP keep-alive with the WiiM (falls back to a new connection per request). Prefer small, testable changes and note what was verified on real hardware in commit messages.
