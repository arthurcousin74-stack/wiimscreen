# wiimscreen

**Touchscreen remote and "now playing" display for WiiM music streamers, running on the M5Stack Tab5 (ESP32-P4).**

*Version française : [LISEZMOI.md](LISEZMOI.md)*

Album art, track info, presets, playback and volume on a 5" color screen next to your WiiM — controlled entirely
over the WiiM's local HTTP API, no cloud and no phone needed. Optional French voice commands.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Firmware build](https://github.com/arthurcousin74-stack/wiimscreen/actions/workflows/firmware.yml/badge.svg)](https://github.com/arthurcousin74-stack/wiimscreen/actions/workflows/firmware.yml)
![Platform: ESP32-P4](https://img.shields.io/badge/platform-ESP32--P4-orange)
![Device: M5Stack Tab5](https://img.shields.io/badge/device-M5Stack%20Tab5-black)

![Now playing on the M5Stack Tab5](docs/apercu.png)

> **Status: beta.** The firmware builds, its logic is covered by native unit tests, and every screen above is
> rendered from the real UI code (see [tools/preview](tools/preview/)). First tests on physical hardware are in
> progress — please [open an issue](../../issues) with what works and what doesn't on your setup.
> The on-screen text and voice commands are currently in **French** (translations welcome).

## Features

- **Now playing**: large album art, title, artist, album, source (Spotify, TIDAL, AirPlay, Bluetooth…), audio
  quality (*16 bits · 44.1 kHz*), progress bar with seek, live-stream badge for radio.
- **Colors from the cover**: background glow, play button and bars take the album's dominant color.
- **Presets as tiles** with their artwork, fetched automatically from the WiiM (or a custom list), 6 per page,
  swipe for more; the playing preset is highlighted.
- **One volume control** with direct tap-to-set, mute, and ±.
- **Ambient view** after 60 s idle: big cover and text readable across the room; optional (experimental)
  **spectrum visualizer** driven by the Tab5 microphones (processed on-device, nothing recorded or sent).
- **Zero-config WiiM discovery**: finds WiiM players on the network (SSDP + `getStatusEx`), lets you pick one,
  and follows it if its IP address changes.
- **Setup from a phone**: scan a QR code shown on the Tab5, pick your Wi-Fi, done.
- **Voice commands** (optional, French, via OpenAI speech-to-text): *« Joue FIP »*, *« Mets France Culture »*,
  *« Plus fort »*, *« Volume 30 »*, *« Suivant »*. Matches your presets, playback commands, and any station of the
  open [Radio Browser](https://www.radio-browser.info/) directory.
- **Over-the-air updates** from a browser on your network (6-digit code shown on the device).
- **Pick up to wake**, screen dimming, click sounds.
- **Security-minded**: the WiiM's self-signed TLS certificate is pinned on first use (SHA-256); album art and
  online services use HTTPS verified against Mozilla's root CAs; secrets are never echoed back or logged.

| Ambient view + visualizer | Radio, loading, notification | Voice |
|---|---|---|
| ![Ambient view](docs/ambiance.png) | ![Radio](docs/radio.png) | ![Voice](docs/voix.png) |
| **Presets without artwork (page 2 of 12)** | **WiiM discovery** | **Over-the-air update** |
| ![Presets](docs/presets.png) | ![Discovery](docs/discovery.png) | ![OTA](docs/ota.png) |

## Hardware

- **[M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5)**: ESP32-P4, 5" 1280×720 touch IPS, 32 MB PSRAM,
  ESP32-C6 for Wi-Fi 6 (2.4 GHz), dual microphones, speaker. The battery is optional (USB-C power is fine).
- **A WiiM streamer** (Mini, Pro, Pro Plus, Amp, Ultra…). Other LinkPlay-based players that expose
  `httpapi.asp` will likely work too.

## Quick start

1. **Get the firmware**: download `wiim-remote-tab5.bin` from the [latest release](../../releases)
   (or from the newest [Actions](../../actions) run, artifact `wiim-remote-tab5`).
2. **Flash it** from Chrome or Edge, nothing to install:
   - Plug the Tab5 in with a USB-C **data** cable and turn it on (single press on the power button).
   - Hold the power button until the green LED blinks: download mode.
   - Open <https://espressif.github.io/esptool-js/>, **Connect**, choose the Tab5's port (not COM1),
     flash address **`0x0`**, select the `.bin`, **Program**, then reset.
3. **Configure from your phone**: scan the QR code on the screen, pick your Wi-Fi, type its password, save.
   Leave the WiiM address empty: the Tab5 finds it.

Later updates go over Wi-Fi: hold the cover 3 s → *Mise à jour*, then open the address shown on a computer.
Full step-by-step guide in French: [LISEZMOI.md](LISEZMOI.md).

**Voice commands** (optional): create an API key at <https://platform.openai.com>, paste it in the setup page.
Speech is sent to OpenAI only while the microphone is listening (≈ $0.003 per minute of audio at the time of
writing, i.e. a few cents a month).

## How it works

```
 M5Stack Tab5 (ESP32-P4)                                       WiiM streamer
┌──────────────────────────────────────────────┐   HTTPS      ┌──────────────┐
│ main.cpp      loop: touch, poll every 5 s,   │ ───────────▶ │ httpapi.asp  │
│               ambient view, voice, OTA       │  pinned TLS  │ getPlayer-   │
│ wiim_client   getPlayerStatus / getMetaInfo /│              │ Status, get- │
│               getPresetInfo / commands       │              │ MetaInfo…    │
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
- **`src/`** is the device firmware (Arduino on ESP-IDF via pioarduino, M5Unified/M5GFX).
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

## Using it as a base

The project is meant to be reused. Typical starting points:

- **Another screen or board** (e.g. CoreS3, ESP32-S3 displays): `lib/core` is hardware-free; `computeLayout()`
  scales with the screen; Tab5-specific bits are the Wi-Fi SDIO pins (`main.cpp`), the shared mic/speaker bus
  (`sound.cpp`) and the panel size. An earlier version of this project ran on the e-ink M5PaperS3 (see git history).
- **Another player with an HTTP API**: replace `wiim_client.*` and the parsers in `lib/core/core.cpp`.
- **Just the WiiM integration**: `wiim_client.*`, `discovery.*` and `lib/core` form a small WiiM library for ESP32.

See [AGENTS.md](AGENTS.md) for architecture, conventions and pitfalls (useful for humans and AI coding agents).

## Security & privacy

Everything runs on your local network. Details and reporting: [SECURITY.md](SECURITY.md). In short: pinned TLS to
the WiiM, CA-verified HTTPS elsewhere, setup and update modes only reachable after an action on the device
(random password / 6-digit code), microphone active only in the ambient view or while listening to a voice command.

## Contributing

Bug reports with your WiiM model and the firmware version (shown in the *Réglages* menu) are very welcome, as are
translations, ports to other boards and UI improvements. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

[MIT](LICENSE) © wiimscreen contributors. Third-party components: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Not affiliated with WiiM / Linkplay or M5Stack.
