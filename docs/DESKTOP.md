# wiimscreen on a computer (Raspberry Pi, Linux, macOS)

The `desktop` build runs the same application as the Tab5 on a computer: the same screens, and the same code for
the WiiM, Qobuz, Deezer, covers and favorites. It connects directly to your network: no relay, no Tab5.

**Status:** built and tested on Linux (x86-64) against a mock WiiM (`platform/pc/selftest.sh`, run by CI). **Not yet
tried on a Raspberry Pi or a Mac.** Please report what works.

## What works, what does not

| Works | Not on a computer (yet) |
|---|---|
| Now playing, covers, presets and tiles, search (Qobuz, Deezer), favorites, Journal, settings menu | Microphone (voice search) |
| WiiM discovery on the network (SSDP) and certificate pinning | Sound effects (taps, beeps) |
| Window or full screen, mouse or touch screen, any screen size (800×480 and larger) | Setup page and update page (use the settings file and `git pull`) |
| Settings and favorites in `~/.config/wiimscreen` | Battery level (not shown) |

Small screens show less text: the fonts do not scale with the screen (at 800×480 the artist line can be hidden).

## Raspberry Pi

Tested hardware: none yet. Expected to work on a Raspberry Pi 4 or 5 (and 3 B+, slower) with Raspberry Pi OS
Bookworm (64-bit), on any HDMI screen or the official 7" touch display (800×480).

1. Install the tools:

   ```bash
   sudo apt update && sudo apt install -y git build-essential libsdl2-dev libssl-dev python3-venv
   ```

2. Install PlatformIO and get the project:

   ```bash
   python3 -m venv ~/pio && ~/pio/bin/pip install platformio
   ```

   ```bash
   cd ~ && git clone https://github.com/arthurcousin74-stack/wiimscreen.git && cd wiimscreen
   ```

3. Build (the first build takes several minutes on a Pi):

   ```bash
   ~/pio/bin/pio run -e desktop
   ```

4. Start it full screen (on the official 7" display, add `--size 800x480`):

   ```bash
   .pio/build/desktop/program --fullscreen
   ```

   It finds the WiiM on the network. If it does not, give the address: `--wiim 192.168.1.50`.

### Start at boot

- **Raspberry Pi OS with desktop:** copy the launcher to the autostart folder.

  ```bash
  mkdir -p ~/.config/autostart && cp platform/pc/wiimscreen.desktop ~/.config/autostart/
  ```

- **Raspberry Pi OS Lite (no desktop):** use the system service. It draws directly on the screen (SDL `kmsdrm`).
  If your user is not `pi`, edit `User` and the paths in the file first.

  ```bash
  sudo cp platform/pc/wiimscreen.service /etc/systemd/system/ && sudo systemctl enable --now wiimscreen
  ```

  Logs: `journalctl -u wiimscreen -f`.

## Linux desktop

Same as the Raspberry Pi steps 1 to 3. Then start it in a window:

```bash
.pio/build/desktop/program --scale 0.75
```

`platform/pc/wiimscreen.desktop` is a launcher for the applications menu (copy it to `~/.local/share/applications/`).

## Windows

Use WSL (Ubuntu), as for the simulator (`tools/sim/README.md`, section Windows), then the Linux steps. In the
default WSL network mode, discovery cannot find the WiiM: start with `--wiim <WiiM address>`. A native Windows
build is not available yet.

## macOS

Not tried yet. With Homebrew:

```bash
brew install sdl2 openssl@3 && pip3 install platformio
```

```bash
export PLATFORMIO_BUILD_FLAGS="-I$(brew --prefix)/include -L$(brew --prefix)/lib -I$(brew --prefix openssl@3)/include -L$(brew --prefix openssl@3)/lib"
```

```bash
pio run -e desktop && .pio/build/desktop/program
```

## Options

| Option | Effect |
|---|---|
| `--fullscreen` | Full screen, no mouse pointer (touch screens) |
| `--size 800x480` | Screen size of the app (default 1280×720). Taller than wide: portrait |
| `--scale 0.75` | Window size on the computer screen (the picture is scaled) |
| `--wiim 192.168.1.50` | Use this WiiM (when discovery does not find it) |
| `--state <folder>` | Settings and favorites folder (default `~/.config/wiimscreen`) |
| `--quiet` | No Journal on the terminal |
| `S` key | Screenshot in `~/.config/wiimscreen/captures` |

## Settings

The setup page of the Tab5 (Wi-Fi access point for a phone) does not exist on a computer. The settings are in
`~/.config/wiimscreen/settings.txt`: one line per setting, the name, a tab, the value. Examples: `lang` (`en` or
`fr`), `deezeruser` (Deezer profile number), `qobuzdirect` (`1` for the direct Qobuz search), `rotation`, `sleep`.
Stop wiimscreen before you edit the file, then start it again. The names are the keys used in `src/storage/settings.cpp`.

## How it works

`platform/pc/` holds the computer side of the Arduino and M5Stack interfaces that `src/` uses:

| File | Role |
|---|---|
| `stubs/` | Arduino `String`, FreeRTOS tasks on threads, M5Unified (screen = memory canvas, touch = mouse/touch), Wi-Fi, HTTP, LittleFS, Preferences |
| `pc_platform.cpp` | Options, state and output folders, `main()` |
| `pc_platform_rtos.cpp` | Time, random numbers, FreeRTOS tasks on threads |
| `pc_platform_prefs.cpp` | Settings file (`settings.txt`), settings of a first start |
| `pc_platform_devices.cpp` | M5, Serial, ESP and WiFi objects, network address, microphone, battery |
| `pc_platform_screen.cpp` | SDL window, mouse/touch input, screenshots, screen size |
| `pc_platform_script.cpp` | `--script` runner |
| `pc_platform_exit.cpp` | Restart and power off |
| `pc_internal.h` | What the `pc_platform*.cpp` files share |
| `pc_http_fs.cpp` | HTTP/1.1 client, LittleFS on a folder |
| `net_direct.cpp` | TCP sockets, TLS with OpenSSL (system certificates for public servers; the app pins the WiiM certificate), UDP for SSDP |
| `desktop_services.cpp` | No microphone; settings and update messages |
| `pc_sound.cpp` | Silent taps and beeps (shared with the simulator) |

The simulator (`tools/sim`) uses the same `platform/pc` layer with a fake WiiM or a relay.
