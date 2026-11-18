# Simulator

Two builds: `sim` (fake WiiM, works anywhere) and `simreal` (your real WiiM through a relay, see below).

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
| Settings code (`settings.cpp`), backup fields, Journal | `services.cpp` and `fake_data.cpp`: covers drawn as PNG per album, favorites in memory, silent sound, setup portal and update screens (no web server), dictation returns « Miles Davis » (`SIM_DICTEE=…` to change) |
| `lib/core` (layout, parsing, text) | `platform/pc/stubs/` (shared with the desktop build): Arduino `String`, FreeRTOS on `std::thread`, Wi-Fi always connected, M5Unified (display = canvas, mouse = touch, microphone = silence) |

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

## Real WiiM mode (relay)

`pio run -e simreal` builds the simulator with the **real network code of the Tab5** (`src/wiim_worker.cpp`,
`wiim_client.cpp`, `art.cpp`, `favorites.cpp`): polling, commands, presets, Qobuz/Deezer search and favorites,
covers, certificate pinning. Only the lowest layer changes: each connection goes through the relay
(`tools/relay/wiim_relay.py`), which opens it on your home network and does the TLS. Favorites are files in
`.pio/simreal-state/fs`.

1. On a computer of your home network (Python 3.8 or later, nothing to install), start the relay:

   ```bash
   python3 tools/relay/wiim_relay.py
   ```

   It shows the WiiMs that it finds. If it finds none, give the address: `--wiim 192.168.1.50`.
2. On the same computer, start the simulator:

   ```bash
   pio run -e simreal && .pio/build/simreal/program --scale 0.75
   ```

   On the first start, it looks for the WiiM (like a new Tab5). If it finds several, touch yours.
3. To use the relay of another computer: `--relay <address>:8787` on the simulator and `--listen 0.0.0.0:8787`
   on the relay (then anyone on your network can use the relay: only on a trusted network).

The relay listens on 127.0.0.1 by default. It connects only to private addresses (your WiiM), and to public
hosts only with TLS on port 443 (certificate checked) or HTTP on port 80. It logs host and port only, never the
data (the Qobuz token is in the HTTP headers). `tools/relay/selftest.sh` checks the chain, and the search through a WiiM play queue (shuffle, restore), with a mock
WiiM (CI runs it).

Checked with a real WiiM (2026-11, Windows 11 + WSL in NAT mode, relay with `--wiim`): the simulator finds the
WiiM through the relay and shows what plays.

Dictation stays simulated (no OpenAI call). The setup and update web pages are not available.

### Windows

The simulator builds in WSL (Ubuntu in Windows). The relay runs in WSL or directly in Windows (`py
tools\relay\wiim_relay.py`).

1. In PowerShell as administrator: `wsl --install`, then restart.
2. So that WSL sees your home network (and `127.0.0.1` is shared with Windows), create
   `%UserProfile%\.wslconfig` with:

   ```ini
   [wsl2]
   networkingMode=mirrored
   ```

   Then run `wsl --shutdown` and open Ubuntu again. (Windows 11 22H2 or later. On older builds WSL says that
   mirrored mode is not supported and keeps its NAT network: that also works if the relay and the simulator both
   run in WSL, but discovery cannot find the WiiM: start the relay with `--wiim <WiiM address>`.)
3. In Ubuntu: `sudo apt install -y build-essential libsdl2-dev python3-venv git`, then
   `python3 -m venv ~/pio && ~/pio/bin/pip install platformio "click<8.2"`, then clone the repository and use
   `~/pio/bin/pio` in the commands above.
4. If Windows asks whether Python may use the network: allow private networks.

## Next steps

- Browser version (Emscripten: SDL2 runs in the browser; the main loop needs `emscripten_set_main_loop`). It can
  use the same relay through a WebSocket (a browser page cannot reach the WiiM directly: self-signed certificate,
  CORS, no SSDP).
