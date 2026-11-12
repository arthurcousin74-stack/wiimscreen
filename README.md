# wiimscreen

**Open-source touchscreen remote and "now playing" display for WiiM music streamers, running on ESP32.**

*Version française : [LISEZMOI.md](LISEZMOI.md)*

Album art, track info, presets, playback and volume on a color touchscreen next to your WiiM — controlled entirely
over the WiiM's local HTTP API, no cloud and no phone needed. Optional French voice commands.

The firmware is written for ESP32 boards with a touchscreen and PSRAM (Arduino-ESP32 3.x, M5Unified/M5GFX), with a
hardware-free, unit-tested core and a layout that scales with the screen. **So far it has only been built and
tested for the [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5)** (ESP32-P4, 5" 1280×720): that is the
supported board today, and the one the downloads and the guide below are for. Ports to other boards are welcome —
see [Other ESP32 boards](#other-esp32-boards).

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Firmware build](https://github.com/arthurcousin74-stack/wiimscreen/actions/workflows/firmware.yml/badge.svg)](https://github.com/arthurcousin74-stack/wiimscreen/actions/workflows/firmware.yml)
![Platform: ESP32](https://img.shields.io/badge/platform-ESP32-orange)
![Tested on: M5Stack Tab5](https://img.shields.io/badge/tested%20on-M5Stack%20Tab5-black)

![Now playing on the M5Stack Tab5](docs/apercu.png)

> **Status: beta.** The firmware builds, its logic is covered by native unit tests, and every screen above is
> rendered from the real UI code (see [tools/preview](tools/preview/)). Testing on physical hardware (Tab5 only) is
> in progress — please [open an issue](../../issues) with what works and what doesn't on your setup.
> The on-screen text and voice commands are currently in **French** (translations welcome).

## Features

- **Now playing**: large album art, title, artist, album, source (Spotify, TIDAL, AirPlay, Bluetooth…), audio
  quality (*16 bits · 44.1 kHz*), progress bar with seek, live-stream badge for radio.
- **Colors from the cover**: background glow, play button and bars take the album's dominant color.
- **Presets as tiles** with their artwork, fetched automatically from the WiiM (or a custom list), 6 per page,
  swipe for more; the playing preset is highlighted.
- **One volume control** with direct tap-to-set, mute, and ±.
- **Ambient view** after 60 s idle: big cover and text readable across the room; optional (experimental)
  **spectrum visualizer** driven by the device's microphones (processed on-device, nothing recorded or sent).
- **Zero-config WiiM discovery**: finds WiiM players on the network (SSDP + `getStatusEx`), lets you pick one,
  and follows it if its IP address changes.
- **Setup from a phone**: scan a QR code shown on the screen, pick your Wi-Fi, done.
- **Voice commands** (optional, French, via OpenAI speech-to-text): *« Joue FIP »*, *« Mets France Culture »*,
  *« Plus fort »*, *« Volume 30 »*, *« Suivant »*. Matches your presets, playback commands, and any station of the
  open [Radio Browser](https://www.radio-browser.info/) directory.
- **Over-the-air updates** from a browser on your network (6-digit code shown on the device).
- **Pick up to wake** (IMU), screen dimming, click sounds.
- **Security-minded**: the WiiM's self-signed TLS certificate is pinned on first use (SHA-256); album art and
  online services use HTTPS verified against Mozilla's root CAs; secrets are never echoed back or logged.

| Ambient view + visualizer | Radio, loading, notification | Voice |
|---|---|---|
| ![Ambient view](docs/ambiance.png) | ![Radio](docs/radio.png) | ![Voice](docs/voix.png) |
| **Presets without artwork (page 2 of 12)** | **WiiM discovery** | **Over-the-air update** |
| ![Presets](docs/presets.png) | ![Discovery](docs/discovery.png) | ![OTA](docs/ota.png) |

## Hardware

**Players**: WiiM streamers (Mini, Pro, Pro Plus, Amp, Ultra…). Other LinkPlay-based players that expose
`httpapi.asp` will likely work too.

**Boards**:

| Board | Status |
|---|---|
| [M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5) — ESP32-P4, 5" 1280×720 touch IPS, 32 MB PSRAM, ESP32-C6 Wi-Fi (2.4 GHz), microphones, speaker | **Supported, the only board tested so far** (beta). Battery optional. |
| Other ESP32-S3 / ESP32-P4 boards with a touchscreen and PSRAM | Not tested. Should be portable — see [Other ESP32 boards](#other-esp32-boards). |
| M5Stack PaperS3 (e-ink) | An early version of this project ran on it (see git history); not maintained. |

## Quick start (M5Stack Tab5)

1. **Get the firmware**: download `wiim-remote-tab5.bin` from the [latest release](../../releases)
   (or from the newest [Actions](../../actions) run, artifact `wiim-remote-tab5`).
2. **Flash it** from Chrome or Edge, nothing to install:
   - Plug the Tab5 in with a USB-C **data** cable and turn it on (single press on the power button).
   - Hold the power button until the green LED blinks: download mode.
   - Open <https://espressif.github.io/esptool-js/>, **Connect**, choose the Tab5's port (not COM1),
     flash address **`0x0`**, select the `.bin`, **Program**, then reset.
3. **Configure from your phone**: scan the QR code on the screen, pick your Wi-Fi, type its password, save.
   Leave the WiiM address empty: the Tab5 finds it.

Later updates go over Wi-Fi: hold the album art 3 s → *Mise à jour*, then open the address shown on a computer.
Full step-by-step guide in French: [LISEZMOI.md](LISEZMOI.md).

**Voice commands** (optional): create an API key at <https://platform.openai.com>, paste it in the setup page.
Speech is sent to OpenAI only while the microphone is listening (≈ $0.003 per minute of audio at the time of
writing, i.e. a few cents a month).

## How it works

```
 ESP32 board (tested: M5Stack Tab5)                            WiiM streamer
┌──────────────────────────────────────────────┐   HTTPS      ┌──────────────┐
│ main.cpp      loop: touch, drawing, menus,   │  ─────────▶  │ httpapi.asp  │
│               ambient view, voice, OTA       │  pinned TLS  │ getPlayer-   │
│ wiim_worker   background task: polling,      │  keep-alive  │ Status, get- │
│               command queue                  │              │ MetaInfo…    │
│ wiim_client   TLS client, WiiM commands      │              │              │
│ discovery     SSDP + getStatusEx             │ ◀─ SSDP ───  └──────────────┘
│ art           background download of covers  │ ──▶ CDNs (album art, preset logos)
│ ui            off-screen canvas (PSRAM) →    │
│               MIPI-DSI panel, accent color   │
│ voice_service OpenAI STT, Radio Browser      │ ──▶ api.openai.com, radio-browser.info
│ ota / portal  web pages for update & setup   │
│ lib/core      pure C++ logic, unit-tested    │
└──────────────────────────────────────────────┘
```

- **`lib/core/`** contains all logic that does not need hardware: WiiM response parsing, layout, accent-color
  extraction, FFT spectrum, motion detection, voice-intent parsing, firmware-image filtering. It is tested on a PC
  with `pio test -e native`.
- **`src/`** is the device firmware (Arduino-ESP32 3.x on ESP-IDF 5 via pioarduino, M5Unified/M5GFX).
- **`tools/preview/`** compiles the real `src/ui.cpp` for a PC (M5GFX + SDL2) and renders every screen to PNG,
  so the UI can be designed without the device.
- **[docs/WIIM_API_NOTES.md](docs/WIIM_API_NOTES.md)**: practical notes on the WiiM/LinkPlay HTTP API
  (what the official PDF does not make obvious).

## Building from source

```bash
pip install platformio "click<8.2"   # the platform's esptool needs click < 8.2
pio test -e native                   # unit tests on the PC
pio run -e tab5                      # firmware
pio run -e tab5 -t upload            # flash over USB
```

UI preview on a PC (Linux: `sudo apt install libsdl2-dev`; macOS: `brew install sdl2`):

```bash
pio run -e preview
SDL_VIDEODRIVER=dummy .pio/build/preview/program images/ out/   # images/: cover.jpg, preset1..6.jpg|png
python tools/preview/topng.py out/*.ppm
```

## Other ESP32 boards

Only the Tab5 has been tested. The code is organised so that other ESP32 boards need a small amount of glue, not a
rewrite:

- **Portable as is**: `lib/core` (parsing, layout, colors, FFT, voice intents — plain C++17, no Arduino headers),
  `wiim_worker`, `wiim_client`, `discovery`, `art`, `voice_service`, `portal`, `ota`, `settings` (Arduino-ESP32 3.x
  Wi-Fi, TLS, WebServer, NVS).
- **Screen and input**: `ui.cpp` draws with M5GFX and `main.cpp` reads touch, IMU, mic and speaker through
  M5Unified, so boards supported by M5Unified/M5GFX (M5Stack CoreS3, Tab5…) are the easiest targets; others need a
  display driver config (LovyanGFX/M5GFX `LGFX` class). `computeLayout()` scales with any resolution and its tests
  check 1280×720, 720×1280 and 960×540 for overlaps and touch-target sizes; add yours to the tests.
- **Tab5-specific bits to adapt**: the Wi-Fi SDIO pins to the ESP32-C6 (`WiFi.setPins(...)` in `main.cpp`, remove
  it on boards with native Wi-Fi), the microphone and speaker sharing one I2S bus (`sound.cpp`), and the
  `[env:tab5]` board settings in `platformio.ini`.
- **Resources**: the off-screen canvas is 16-bit and full-screen (≈ 1.8 MB at 1280×720, ≈ 300 KB at 480×320), so
  PSRAM is needed; TLS and album art decoding also want PSRAM. Microphone, IMU and speaker are optional features.

To add a board: create a `[env:<board>]` in `platformio.ini`, keep board differences behind small `#if` blocks or a
board header, and say in your pull request what you verified on the hardware.

## Using it as a base

- **Just the WiiM integration**: `wiim_client.*`, `discovery.*`, `wiim_worker.*` and `lib/core` form a small WiiM
  library for ESP32 (pinned HTTPS, keep-alive, discovery, background polling, command queue).
- **Another player with an HTTP API**: replace `wiim_client.*` and the parsers in `lib/core/core.cpp`.
- **Another screen**: see [Other ESP32 boards](#other-esp32-boards).

See [AGENTS.md](AGENTS.md) for architecture, conventions and pitfalls (useful for humans and AI coding agents).

## Security & privacy

Everything runs on your local network. Details and reporting: [SECURITY.md](SECURITY.md). In short: pinned TLS to
the WiiM, CA-verified HTTPS elsewhere, setup and update modes only reachable after an action on the device
(random password / 6-digit code), microphone active only in the ambient view or while listening to a voice command.

## Contributing

Bug reports with your WiiM model, your board and the firmware version (shown in the *Réglages* menu: hold the album art 3 s)
are very welcome, as are hardware test reports, ports to other ESP32 boards, translations and UI improvements. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE) © wiimscreen contributors. Third-party components: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Not affiliated with WiiM / Linkplay, M5Stack or Espressif.
