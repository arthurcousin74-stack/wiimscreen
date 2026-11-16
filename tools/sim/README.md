# Simulator

The real application (`src/main.cpp`, `src/ui.cpp`, `src/log.cpp`, `src/settings.cpp` and `lib/core`) running on
a PC, with a **fake WiiM**. The window is the Tab5 screen; the mouse is the finger.

```bash
sudo apt install libsdl2-dev          # macOS: brew install sdl2
pio run -e sim
.pio/build/sim/program --scale 0.75   # window at 75 %; S = screenshot in .pio/sim-out/
```

Options: `--reset` (first start again: settings back to the defaults below), `--state <dir>` (default
`.pio/sim-state`, holds `settings.txt`, the simulated NVS), `--out <dir>` (screenshots, default `.pio/sim-out`),
`--headless` (no window), `--quiet` (no Journal on the terminal), `--script <file>`.

## What is real, what is fake

| Real (same code as the Tab5) | Replaced (`tools/sim/`) |
|---|---|
| Main loop, screens, navigation, keyboard, results, tiles, Journal, menus, pending rings, messages | `fake_wiim.cpp`: the WiiM behind the real `WiimWorker` interface (catalog of 7 albums and 2 radios, 6 presets, Qobuz/Deezer favorites, search, favorites, playback with position) |
| Settings code (`settings.cpp`), backup fields, Journal | `fake_services.cpp`: covers drawn as PNG per album, favorites in memory, silent sound, setup portal and update screens (no web server), dictation returns « Miles Davis » (`SIM_DICTEE=…` to change) |
| `lib/core` (layout, parsing, text) | `stubs/`: Arduino `String`, FreeRTOS on `std::thread`, Wi-Fi always connected, M5Unified (display = canvas, mouse = touch, microphone = silence) |

Default settings on the first start: configured (no setup portal), a Deezer profile number (Deezer tiles), a fake
OpenAI key (the microphone shows; nothing is sent). Searches « erreur » and « rien » show the error and the
empty cases.

**Not covered:** the real WiiM API, Qobuz/Deezer/OpenAI, TLS, discovery, the setup and update web pages, memory
limits, timing of the ESP32-P4, the microphone and the speaker. Test those on the Tab5 (`docs/HARDWARE_TEST.md`).

## Scripts (tests without a window)

One step per line: `wait <ms>`, `tap <x> <y>`, `press <x> <y>`, `move <x> <y>`, `release`, `shot <name>`, `quit`.
Coordinates are screen pixels (1280×720 in landscape).

```bash
.pio/build/sim/program --headless --quiet --reset --script tools/sim/scripts/tour.txt
python tools/preview/topng.py .pio/sim-out/*.ppm
```

CI runs `tools/sim/scripts/tour.txt` on every push (artifact `sim-screens`): a crash or a hang fails the build.

## Next steps

- Browser version (Emscripten: SDL2 runs in the browser; the main loop needs `emscripten_set_main_loop`).
- "Real WiiM" mode: the same `WiimWorker` interface backed by a small relay on the home network (a browser page
  cannot reach the WiiM directly: self-signed certificate, CORS, no SSDP).
