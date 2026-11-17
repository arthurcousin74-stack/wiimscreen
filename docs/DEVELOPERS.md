# wiimscreen for developers

<!-- ASD-STE100 Simplified Technical English, like README.md. -->

This file tells how the firmware operates, how to build it, and how to port it. For the conventions and the known
problems, refer to [AGENTS.md](../AGENTS.md).

## How it works

```
 ESP32 board (tested: M5Stack Tab5)                            WiiM streamer
┌──────────────────────────────────────────────┐   HTTPS      ┌──────────────┐
│ main.cpp      loop: touch, drawing, menus,   │  ─────────▶  │ httpapi.asp  │
│               ambient view, voice, OTA       │  pinned TLS  │ getPlayer-   │
│ wiim_worker   background task: polling,      │  keep-alive  │ Status, get- │
│               commands, queues, search       │              │ MetaInfo…    │
│ wiim_client   TLS client, WiiM commands      │  UPnP SOAP   │ PlayQueue    │
│ favorites     queue copies (LittleFS)        │  port 49152  │ (queues)     │
│ discovery     SSDP + getStatusEx             │ ◀─ SSDP ───  └──────┬───────┘
│ wiim_worker   Qobuz search (WiiM's access)   │ ──▶ qobuz.com       │ WiiM's own
│               Deezer search, profile favs    │ ──▶ api.deezer.com  ▼ access
│                                              │          qobuz.com, airable.io (Deezer)
│ art           background download of covers  │ ──▶ CDNs (album art, preset logos)
│ ui            off-screen canvas (PSRAM) →    │
│               MIPI-DSI panel, accent color   │
│ voice_service OpenAI speech-to-text          │ ──▶ api.openai.com
│ ota / portal  web pages for update & setup   │
│ lib/core      pure C++ logic, unit-tested    │
└──────────────────────────────────────────────┘
```

- **`lib/core/`** contains all the logic that does not use the hardware: WiiM response parsing, UPnP/XML parsing of
  the play queues, Qobuz and Deezer parsing, layout, on-screen keyboard, interface language, accent color, FFT
  spectrum, motion detection and firmware-image filtering. Test it on a PC with `pio test -e native`.
- **`src/`** contains the device firmware (Arduino-ESP32 3.x on ESP-IDF 5 through pioarduino, M5Unified, M5GFX).
- **[WIIM_API_NOTES.md](WIIM_API_NOTES.md)** tells what we verified on a real WiiM, with the dates.

### Play queues: how the Tab5 starts an album

The WiiM has an UPnP service, `PlayQueue`, that WiiM does not document. The WiiM Home app uses it to send a play
queue to the WiiM. A queue for a streaming service contains an address (`SearchUrl`): an address of the Qobuz API,
or of airable (the gateway that the WiiM uses for Deezer). The WiiM reads this address with its own access and
loads the tracks.

1. The Tab5 keeps a model of a Qobuz queue and of a Deezer queue. It copies them from the queues of the WiiM, or
   makes the Qobuz model from the account name that the WiiM gives (`GetUserInfo`).
2. To play an album, the Tab5 copies the model, sets `SearchUrl` to the album and sends the queue (`CreateQueue`).
3. The Tab5 reads the queue again, finds the track by its title and starts the queue there (`PlayQueueWithIndex`).
   The title is necessary because the WiiM shuffles the queue when shuffle is on.

A favorite (heart) is a copy of a queue. A "live" queue (Deezer Flow) is kept without its tracks, so that the WiiM
gives new tracks each time.

### Qobuz

**Default (official parts only).** The WiiM does the Qobuz requests itself. For a search, the Tab5 sends a queue
whose `SearchUrl` is `track/search?query=…`; the WiiM fills it with its own Qobuz login. The **Qobuz favorites**
list is the same with `favorite/getUserFavorites?type=tracks`. Because the WiiM starts a queue at once, the Tab5
mutes the WiiM and then puts back the music that played before (same track, same position). The WiiM does not fill
a queue from an album search, so album search needs the option below.

**Option "Direct Qobuz search" (`Settings::qobuzDirect`, off by default, not official).** `GetUserInfo(Qobuz)` gives
the application ID and a token of the WiiM's Qobuz integration. The Tab5 keeps them in memory only and uses them for
`track/search` and `album/search`: faster, and the music does not change. Qobuz does not approve this use of another
application's ID. Qobuz refuses this token for the favorites (HTTP 401). With the option off, the Tab5 never calls
`GetUserInfo`.

### Deezer

The Tab5 searches the public Deezer catalog (`api.deezer.com/search`, no key). An album plays as an airable queue
(`<airable address>/deezer/album/<id>`). The favorites come from a public profile (`api.deezer.com/user/<number>`).
The Deezer access of the WiiM is an airable access, not a Deezer token. The Tab5 does not ask for it.

## Build from source

```bash
pip install platformio "click<8.2"   # the esptool of the platform needs click < 8.2
pio test -e native                   # unit tests on the PC
pio run -e tab5                      # firmware
pio run -e tab5 -t upload            # flash through USB
```

The OTA file is `.pio/build/tab5/firmware.bin`. The flashable image for address `0x0` comes from the merge step in
[.github/workflows/firmware.yml](../.github/workflows/firmware.yml).

## See the UI on a PC

Install SDL2 first (Linux: `sudo apt install libsdl2-dev`; macOS: `brew install sdl2`).

```bash
pio run -e preview
python tools/preview/make_images.py images/                  # cover and preset images (no real artwork)
SDL_VIDEODRIVER=dummy .pio/build/preview/program images/ out/
python tools/preview/topng.py out/*.ppm
```

`PREVIEW_LANG=fr` makes the screens in French. The images in `docs/` come from this tool.

The simulator runs the full app on a PC, with a fake WiiM or with your real WiiM through a relay. Refer to
[tools/sim/README.md](../tools/sim/README.md).

The `desktop` build runs the app on a Raspberry Pi or a computer, directly on the network. Refer to
[DESKTOP.md](DESKTOP.md). Simulator and desktop share the computer layer in `platform/pc/`.

## Other ESP32 boards

We tested only the Tab5. Other ESP32 boards need a small quantity of new code.

- **Code that you can use as is**: `lib/core` (plain C++17, no Arduino headers), `wiim_worker`, `wiim_client`,
  `discovery`, `art`, `voice_service`, `portal`, `ota` and `settings`. These files use the Arduino-ESP32 3.x Wi-Fi,
  TLS, WebServer and NVS functions.
- **Screen and input**: `ui.cpp` uses M5GFX to draw. `main.cpp` uses M5Unified for the touchscreen, the IMU, the
  microphone and the speaker. Boards that M5Unified and M5GFX support (for example the M5Stack CoreS3) are the
  easiest to port. For other boards, write a display configuration (LovyanGFX or M5GFX `LGFX` class).
- **Layout**: `computeLayout()` fits all screen sizes. Its tests check 1280×720, 720×1280 and 960×540. Add the size
  of your screen to these tests.
- **Parts that are specific to the Tab5**:
  - The SDIO pins to the ESP32-C6 Wi-Fi chip (`WiFi.setPins(...)` in `main.cpp`). Remove this line on boards with
    built-in Wi-Fi.
  - The microphone and the speaker use the same I2S bus.
  - The `[env:tab5]` settings in `platformio.ini`.
- **Memory**: the off-screen canvas uses 16 bits for each pixel. It needs approximately 1.8 MB at 1280×720 and
  300 KB at 480×320. Thus the board must have PSRAM.

To add a board:

1. Add an `[env:<board>]` section to `platformio.ini`.
2. Put the differences between boards in small `#if` blocks or in a board header file.
3. In your pull request, tell us what you tested on the real hardware.

## Use the project as a base

- **Only the WiiM integration**: `wiim_client.*`, `discovery.*`, `wiim_worker.*` and `lib/core` are a small WiiM
  library for ESP32: pinned HTTPS, keep-alive, discovery, background polling, a command queue, and play queues
  (`lib/core/src/upnp.*`).
- **A different player with an HTTP API**: replace `wiim_client.*` and the parsers in `lib/core/src/core.cpp`.
- **A different screen**: refer to [Other ESP32 boards](#other-esp32-boards).

## Root certificates

The Tab5 checks HTTPS servers with the Mozilla roots of ESP-IDF, plus the roots in `certs/extra_roots.pem`. After a
change, rebuild the bundle and check the servers:

```bash
python tools/make_ca_bundle.py certs/cacrt_all.pem certs/extra_roots.pem certs/x509_crt_bundle.bin
python tools/check_ca_bundle.py certs/cacrt_all.pem certs/extra_roots.pem
```
