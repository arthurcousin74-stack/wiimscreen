# wiimscreen on a computer (Windows, Raspberry Pi, Linux, macOS)

The `desktop` build runs the same application as the Tab5 on a computer: the same screens, and the same code for
the WiiM, Qobuz, Deezer, covers and favorites. It connects directly to your network: no relay, no Tab5. On Windows,
it is a ready-made program: `wiimscreen.exe` (build `windows`).

**Status:** built and tested on Linux (x86-64) and Windows (x86-64) against a mock WiiM (`platform/pc/selftest.sh`,
run by CI for both). On Windows 11, the WiiM was also found on a home network (discovery). **Not yet tried on a
Raspberry Pi or a Mac, nor with a real WiiM on Windows.** Please report what works.

## What works, what does not

| Works | Not on a computer (yet) |
|---|---|
| Now playing, covers, presets and tiles, search (Qobuz, Deezer), favorites, Journal, settings menu | Microphone (voice search) |
| WiiM discovery on the network (SSDP) and certificate pinning | Sound effects (taps, beeps) |
| Window or full screen, mouse or touch screen, any screen size (800×480 and larger) | Setup page and update page (use the settings file and `git pull`) |
| Settings and favorites in `~/.config/wiimscreen` (Windows: `%APPDATA%\wiimscreen`) | Battery level (not shown) |

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
   cd ~ && git clone https://github.com/couzinc/wiimscreen.git && cd wiimscreen
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

**A simple port for now.** `wiimscreen.exe` is the computer version built for Windows, not a full Windows app yet:
no installer, no icon, no code signature, no microphone or sounds, no setup or update page; updates are a new zip.
It passed the self-test against a mock WiiM and found a real WiiM on the network, but a full session with a real
WiiM on Windows has not been tried yet. A fuller Windows app may come later.

Windows 10 (version 1903 or later) or Windows 11, 64-bit. No installation, no administrator rights.

1. Download `wiimscreen-windows-x64.zip`: from the latest release on GitHub (Releases), or for a test build from
   the Actions tab (run of the `firmware` workflow, artifact `wiimscreen-windows-x64`; you must be signed in to
   GitHub).
2. Unzip the whole folder (right-click, *Extract All*). Keep `wiimscreen.exe` and `cacert.pem` together.
3. Double-click `wiimscreen.exe`.
4. The program is not signed, so Windows can show *Windows protected your PC*. Click **More info**, then
   **Run anyway**. Windows asks this only once.
5. Windows Firewall can ask if wiimscreen may use the network. Allow **Private networks**. Discovery of the WiiM
   (SSDP) needs it. If you refused, discovery can fail: start with `--wiim <address of the WiiM>`, or allow
   wiimscreen in *Windows Security > Firewall & network protection > Allow an app through firewall*.
6. wiimscreen finds the WiiM and asks you to confirm it (tap or click its name, as on the Tab5).

Options (table below): make a shortcut to `wiimscreen.exe` (right-click, *Create shortcut*), open its *Properties*
and add them at the end of *Target*, for example `...\wiimscreen.exe --wiim 192.168.1.50 --fullscreen`.

| What | Where |
|---|---|
| Settings and favorites | `%APPDATA%\wiimscreen` (type it in the address bar of File Explorer): `settings.txt` and the `fs` folder |
| Log of the last run | `%APPDATA%\wiimscreen\last.log` (the run before: `previous.log`). Send it when you report a problem. With `--console`, the messages show in a console window instead |
| Screenshots (F12) | `%APPDATA%\wiimscreen\captures` |
| Root certificates (Qobuz, Deezer, covers) | `cacert.pem` beside `wiimscreen.exe` |

Not on Windows: the microphone (voice search), sound effects, the setup page and the update page (edit
`settings.txt`; download a new zip to update). To remove wiimscreen, delete its folder and `%APPDATA%\wiimscreen`.

The zip holds `wiimscreen.exe` (SDL2, OpenSSL and the C++ runtime are linked into it: no DLL), `cacert.pem`,
`README.txt` (the same steps, for the user) and `LICENSE.txt`.

### Build it yourself

- **On Windows**: install [MSYS2](https://www.msys2.org/), open the *MSYS2 UCRT64* terminal, then:

  ```bash
  pacman -S --needed git mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-openssl mingw-w64-ucrt-x86_64-python
  ```

  Install PlatformIO with the Windows Python (`pip install platformio`). Start the *MSYS2 UCRT64* terminal with the
  Windows `PATH` too (environment variable `MSYS2_PATH_TYPE=inherit`, as CI does), so that it finds `pio`. Then, in
  the wiimscreen folder:
  `pio run -e windows && platform/windows/package.sh` (the folder and the zip are in `dist/`). `tools/check.sh windows`
  also runs the self-test against a mock WiiM (as CI does).
- **From Linux or WSL** (cross-compiling, no root needed): a MinGW-w64 toolchain such as
  [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) (`ucrt` build), and the MSYS2 packages
  `mingw-w64-ucrt-x86_64-SDL2` and `mingw-w64-ucrt-x86_64-openssl` (`.pkg.tar.zst` files from
  `repo.msys2.org/mingw/ucrt64/`, extracted in one folder). Then:

  ```bash
  export WINDOWS_CROSS=<llvm-mingw>/bin/x86_64-w64-mingw32- WINDOWS_SYSROOT=<extracted folder>/ucrt64
  ```

  ```bash
  tools/check.sh windows
  ```

  In WSL, the self-test starts `wiimscreen.exe` on Windows; it reaches the mock WiiM of WSL on `localhost`.

Developers can also use the Linux build in WSL (Ubuntu), as for the simulator (`tools/sim/README.md`, section
Windows). In the default WSL network mode, discovery cannot find the WiiM there: start with `--wiim <WiiM address>`.

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
| `--state <folder>` | Settings and favorites folder (default `~/.config/wiimscreen`; Windows: `%APPDATA%\wiimscreen`) |
| `--quiet` | No Journal on the terminal |
| `--log-keys` | Show each key event on the terminal (`[key] key=ok kind=press`) |
| `--console` | Windows only: show the messages in a console window (else they go to `last.log`) |
| `F12` key | Screenshot in `~/.config/wiimscreen/captures` (Windows: `%APPDATA%\wiimscreen\captures`) |

### Keys

You can use the app with a keyboard, a remote or a game controller. The first arrow key (or OK) shows a white
ring around one button: that button has the focus. The arrow keys move the ring, OK presses the button. A tap or a
click on the screen hides the ring; the next key shows it again, on the same button.

| Key | Controls view | Settings menu, Journal, touch test | Search keyboard and results |
|---|---|---|---|
| up, down, left, right | Move the ring. Left from the content goes to the bar on the left (Home, Search, Settings). Left or right past the first or last preset tile shows the previous or next page of tiles (on the first page, Left goes to the bar) | Move the ring | Not used yet |
| ok | Press the button with the ring | Press the button with the ring | Not used yet |
| ok, held 0.7 s | Settings menu (as a long press on the cover) | As ok | Not used yet |
| back | Nothing | Close (back to the controls view) | Results: back to the keyboard. Keyboard: back to the controls view. During dictation: stop it |
| menu | Settings menu | Settings menu (in the menu: close it) | Settings menu |
| search | Search keyboard | Search keyboard | Results: back to the keyboard |
| playpause, play, pause, next, prev | As the buttons of the controls view | As the buttons of the controls view | Not used yet |
| volup, voldown, mute | Volume +, volume −, mute (as the buttons; hold volume to keep changing it) | As the buttons | Not used yet |
| ffwd, rewind, backspace, typed text | Not used yet | Not used yet | Not used yet |

When the screen is dim or off, or shows the ambient view (large cover), a key wakes it, as a first tap does. The
play, next, previous, volume and mute keys also do their action at the same time; the other keys only wake it.
A key also counts as activity: the screen does not dim and the computer does not go to sleep while you use the keys.
WiiM picker (first start): the arrow keys and OK choose a WiiM, back searches again. Update and setup screens:
ok or back closes them.

How the computer reads the keys (`takeKey()`, `--log-keys` shows them):

| Input | Key |
|---|---|
| Arrow keys, D-pad of a game controller, hat of a joystick | up, down, left, right (held: repeat after 0.4 s, 8 per second) |
| Enter, keypad Enter, Select, controller A | ok (held 0.7 s: long press) |
| Escape, Android Back, controller B | back |
| Menu, F1, controller Start | menu |
| Space, Play media key | playpause |
| Stop media key | pause |
| Next, Previous, Fast forward, Rewind media keys | next, prev, ffwd, rewind |
| Volume up, Volume down, Mute keys | volup, voldown, mute |
| Search key, F3 | search |
| Backspace | backspace (held: repeat) |
| Typed characters | text (UTF-8) |

SDL starts the text input by itself when the window opens on a computer, so typed characters arrive without more
code. Space gives `playpause` and also the text " ". A controller is read as a controller only (not also as a
joystick). When the window loses the focus, the held keys are released without an event.

## Settings

The setup page of the Tab5 (Wi-Fi access point for a phone) does not exist on a computer. The settings are in
`~/.config/wiimscreen/settings.txt` (Windows: `%APPDATA%\wiimscreen\settings.txt`): one line per setting, the name, a tab, the value. Examples: `lang` (`en` or
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
| `pc_platform_keys.cpp` | Keys: keyboard, media keys, game controllers, joysticks and script keys → `takeKey()` |
| `pc_platform_exit.cpp` | Restart (`pc::restart()`: `execv`; Windows: `CreateProcess` of the same command line) and power off |
| `pc_platform_windows.cpp` | Windows only: settings folder `%APPDATA%\wiimscreen`, output to the terminal, `--console` or `last.log` |
| `pc_internal.h` | What the `pc_platform*.cpp` files share |
| `pc_http_fs.cpp` | HTTP/1.1 client, LittleFS on a folder |
| `net_direct.cpp` | TCP sockets, TLS with OpenSSL (system certificates for public servers, or the bundled `cacert.pem` with `PC_BUNDLED_CA`; the app pins the WiiM certificate), UDP for SSDP |
| `net_compat.h` | The socket calls that differ between POSIX and Windows (Winsock 2), so `net_direct.cpp` has one code path |
| `desktop_services.cpp` | No microphone; settings and update messages |
| `pc_sound.cpp` | Silent taps and beeps (shared with the simulator) |

The simulator (`tools/sim`) uses the same `platform/pc` layer with a fake WiiM or a relay.

Windows (`[env:windows]` in `platformio.ini`): the same sources as `desktop`, built with MinGW-w64 as a GUI program
(no console window) and linked statically. `tools/windows_build.py` sets the libraries, the cross toolchain
(`WINDOWS_CROSS`, `WINDOWS_SYSROOT`) and the resources of `platform/windows/` (manifest: UTF-8 code page so that
paths and files are UTF-8, as on Linux; version information); `platform/windows/package.sh` makes the folder and the
zip in `dist/`.

**Root certificates.** Linux, macOS and Raspberry Pi OS give OpenSSL a certificate store; Windows does not (the store
of Windows is not in a format OpenSSL reads, and OpenSSL's built-in folder does not exist there). With
`PC_BUNDLED_CA`, `net_direct.cpp` loads the PEM text that `pc::bundledRootCerts()` returns: by default
`cacert.pem` beside the program, made by `package.sh` from `certs/cacrt_all.pem` and `certs/extra_roots.pem` (the
same sources as the Tab5's bundle; update them with the same tools). OpenSSL's configuration file is not read
(`OPENSSL_INIT_NO_LOAD_CONFIG`): its built-in folder would be `C:\ucrt64\...`, which any user can create. A future
Android port can return the same file from its assets. The WiiM itself is not checked against these roots: its
certificate is self-signed, and the app pins its fingerprint.
