# Changelog

Versions follow [Semantic Versioning](https://semver.org/). Firmware builds show their version at the bottom of
the *Settings* menu (`<build>-<commit>` for CI builds, `vX.Y.Z` for releases).

## [Unreleased]

## [0.4.0-beta] — 2026-11-21 — Library, tile menu, last played tiles first, Play all

**Tab5:** checked in the simulator and with a mock WiiM only: this firmware has **not been checked on a Tab5 yet**.
**Deezer *Play all*** has not been tried on any WiiM: if nothing plays, send the Journal lines. If a tile or a button
does not react, report it and install v0.3.1-beta again.

### Added
- **Library**: a new button in the side bar (grid, between Home and Search) shows all the tiles on one screen, in the
  order of the strip, with filters for the kinds you have (*Presets*, *Favorites*, *Qobuz*, *Deezer*) and pages. A
  touch plays the tile and goes back to the main screen; a long press opens the tile menu, which comes back to the
  Library. The grid is redrawn when the tiles or their pictures change while it is open.
- **Tile menu**: hold a finger on a tile (or hold OK on a focused tile) to pin it at the front of the tiles, unpin it,
  or remove a favorite saved on the remote. The Journal shows `Menu de la tuile : …` and `Tuile épinglée : …`.
- **Last played tiles first**: the remote keeps a short history of the tiles you start (`/fav/tiles.txt`: last play,
  count, pins; no clock needed). Pinned tiles come first, then the last played ones, then the others. The new order
  is applied after one minute without a touch, so tiles never move under the finger. Setup page › *Tile order*:
  *As saved* keeps the old order (pins still apply).
- **Play all / Shuffle** in the *Qobuz favorites* and *Deezer favorites* lists: the whole list plays from the first
  track, or from a random track with shuffle on. Shuffle is turned off before the queue is made (else the WiiM mixes
  it and the first track is not the chosen one), and on again after it starts for *Shuffle*. Qobuz: the same queue
  as the list (checked with the mock WiiM, `tools/relay/selftest.sh` case 7). Deezer: the "favorite tracks" playlist
  of the profile through airable (`/deezer/playlist/<id>`): **not checked on a WiiM yet**. In these lists the two
  buttons take the place of *Edit* (search stays in the side bar) and the title has no quotes.

### Changed
- **Clearer tiles**: album tiles show the artist on a second line when the title fits; edition notes at the end of an
  album title ("(Legacy Edition)", "[Remastered 2011]", " - Deluxe") are removed on the tile. The *Qobuz favorites*
  and *Deezer favorites* tiles show a large heart instead of initials.

## [0.3.1-beta] — 2026-11-20 — Faster touch feedback, volume by touch

**Tab5:** checked in the simulator only, like v0.3.0-beta: this firmware has **not been checked on a Tab5 yet**. If a
button does not react, report it and install v0.2.0-beta again. After about 20 touches, *Settings › Journal* shows a
"Réactivité" line: please send it with your report.

### Added
- **Volume by touch**: slide a finger on the volume bar (the WiiM follows, at most one command every 150 ms and the
  last one at release), or hold − / + to repeat the step.
- **Responsiveness in the Journal**: every 20 touches, one line gives the time from a touch to the first image sent
  to the screen, and the time of full and partial images (`[perf]` on the serial port).

### Changed
- **Faster touch feedback**: −/+, the volume bar, mute, play/pause, previous/next and a pressed tile redraw only their
  own area instead of the whole screen (two full images per touch before). A seek during playback redraws only the
  progress line. Checked in the simulator: the partial images are identical, pixel for pixel, to a full image.
- **Dimmed screen**: a touch on the dimmed controls (or menu, search) turns the light on **and** does what it touches;
  before, it only turned the light on (seen when nothing plays: with music, the ambient view shows first). A screen
  that is off, or the dimmed ambient view, still only wakes up.

## [0.3.0-beta] — 2026-11-19 — Windows app, keyboard and remote control on computers

**Tab5:** every tap now goes through a new dispatcher (one list of buttons per screen, shared with the keys). The
simulator shows the same behaviour on every screen, but this firmware has **not been checked on a Tab5 yet**. If a
button does not react, report it and install v0.2.0-beta again.

### Added
- **Windows (simple port, experimental)**: `wiimscreen-windows-x64.zip` holds `wiimscreen.exe`, a ready-made program for
  Windows 10 (1903 or later) and 11, 64-bit: unzip it and double-click, no installation, no WSL. The same app as the
  computer version: WiiM discovery, covers, presets, favorites, Qobuz and Deezer search, keys. Settings and favorites
  in `%APPDATA%\wiimscreen`, the log of the last run in `last.log` there (`--console` shows a console window). It
  is not signed (Windows: *More info*, *Run anyway*), and the firewall can ask for the network (allow private
  networks for discovery). Built and self-tested by CI against a mock WiiM; seen working with a real WiiM Pro on
  Windows 11 (2026-11-19: discovery, playback, search with the keyboard, sharp picture at 125 % scaling). No microphone, no sounds, no setup page. docs/DESKTOP.md. No change on
  the Tab5.
- Computer version: you can use the app with a keyboard, a remote or a game controller. The arrow keys move a white
  ring between the buttons (the bar on the left, the heart, playback, volume, the preset tiles and their pages, the
  Settings menu, the Journal, the WiiM picker), OK presses the button, Back closes a screen, Space and the media keys
  play, pause, skip and change the volume, Menu opens Settings, F3 opens the search. The ring shows only after a key
  and goes away when you touch the screen. No change on the Tab5. Keys: docs/DESKTOP.md.
- Computer version: search with the computer's keyboard. On the search screen, type the text, Backspace erases,
  Enter searches; the focus then goes to the first result, so a second Enter plays it. Typing on the results list
  goes back to the keyboard with the text changed. With a remote or a controller, the arrows move the ring over the
  on-screen keys, the sources and the results (Down past the last result of a page shows the next page), and OK
  presses. No change on the Tab5.
- For contributors: key input plumbing for remotes and keyboards (not used by the app yet). The computer builds read
  the keyboard, media keys and game controllers; simulator scripts can send keys (`key`, `hold`, `text` steps) and
  `--log-keys` shows them. No change on the Tab5.

### Changed
- Computer version, sharper picture: the app is drawn at the size of the window instead of a 1280x720 picture
  stretched into it. Without `--size`, the picture is 1280x720 times 1.25, 1.5, 1.75 or 2, the largest that fits
  the screen (90 % of it in a window, all of it in full screen), and the window shows it pixel for pixel; the text
  grows with it (fonts made from DejaVu Sans when the program starts). On Windows, the program is DPI aware: at
  125 % or 150 % display scaling, Windows no longer stretches (and blurs) the window. `--size` larger than
  1280x720 also gets larger text now. A window made smaller than the picture is smoothed (not jagged), and an
  unchanged picture is not copied to the window again (less CPU). The Windows zip has `DejaVuSans.ttf` beside the
  program. No change on the Tab5.
- Computer version and simulator: the screenshot key is now **F12** (it was `S`).
- Computer version: `settings.txt` is written with `\n` line endings on every system, and lines ending with `\r\n`
  (a Windows editor) are read too. The log shows each public server whose certificate was verified once
  (`[tls] host=… verified=1`).
- For contributors: `platform/pc/net_direct.cpp` uses POSIX or Winsock sockets through `platform/pc/net_compat.h`;
  `platform/pc/selftest.sh` takes `PROGRAM=` (the Windows exe) and `SELFTEST_ART_URL=` (a cover over HTTPS, to
  check the root certificates); new `tools/check.sh windows` gate; `mock_wiim.py --art-url`.
- For contributors: `src/` is split into folders by role (`app/`, `ui/`, `net/`, `device/`, `storage/`;
  `main.cpp` stays at the root), with includes written from `src/` (`#include "net/wiim_worker.h"`). File names
  and the code (apart from the include lines) are unchanged. No change for the user.
- For contributors: each screen registers its tappable elements in one list (`wr::TargetList`, `Ui::targets()`),
  first step to a D-pad / remote-control focus; most `hitXxx()` functions are lookups in it. No change for the user.
- For contributors: D-pad move logic over that list (`wr::nextTarget`, `wr::defaultTarget`), tested on the real
  layouts. No change for the user.
- For contributors: every tap is now found in that list and handled by one function per screen
  (`activateControls`, `activateMenu`, …), which the OK key also calls. New simulator scripts `taps.txt` (a tap on
  every element of every screen) and `dpad.txt` (keys only), both run by `tools/check.sh`. No change for the user
  (the Tab5 still needs a tap check on hardware).

## [0.2.0-beta] — 2026-11-18 — computer version, safer search, code split for contributors

### Added
- **Computer version (experimental)**: `pio run -e desktop` runs the same app on a Raspberry Pi, Linux or macOS,
  directly on the network (sockets, OpenSSL, SSDP discovery, certificate pinning as on the Tab5). Window or full
  screen, touch or mouse, any screen size from 800×480 (`--size`), settings in `~/.config/wiimscreen`, autostart
  files for the Pi. Tested on Linux against a mock WiiM (CI); not yet on a Raspberry Pi or a Mac. docs/DESKTOP.md.

### Changed
- A track is now found in queues of more than 200 tracks (up to 2,000): beyond 200 only the titles are read. The
  music before a search, and an album played at a track, go back to the right place in long playlists.
- The computer layer of the simulator moved to `platform/pc/` (shared with the desktop build).
- For contributors: the state of `src/main.cpp` is grouped in named structs in `src/app_internal.h`, one
  `enum class Screen` gives the current screen, and search, dictation and tiles moved to `src/app_search.cpp`,
  `src/app_voice.cpp` and `src/app_tiles.cpp`. No change for the user.
- For contributors: one table (`kFields` in `src/settings.cpp`) lists every setting for the NVS and for the backup
  copy; a setting without its line stops the build. Stored settings and backup files stay the same.

### Fixed
- Search through the WiiM with shuffle on: the results were listed in a shuffled order and a tap could start another
  track. Shuffle is turned off for the search and put back after it; a result now plays from its album, at this
  track, and the title is checked after the start.
- A track that was paused before a search is put back, paused at the same position (it was lost).
- The music before a search was not put back when its queue was very large (Qobuz search queue of 1000 tracks).
- Direct Qobuz answers with extra spaces in their HTTP headers are read correctly.
- A search through the WiiM that loads nothing ends after about 10 s (one word) or 20 s (several words), not 43 s.
- If the remote restarts during a search through the WiiM, the next start turns the WiiM's sound and shuffle back on.

### Fixed (code review, 2026-11-18)
- A crash when an artist, an album or a title is made only of spaces; an endless loop on a malformed Qobuz or Deezer
  answer; very slow reading of a malformed queue.
- Automatic sleep no longer starts the moment the music ends: it counts from the last touch and from the end of the
  music. A WiiM that cannot be reached no longer keeps the screen on.
- The waiting ring around a button sometimes did not show.
- Search through the WiiM: no search when the WiiM's state cannot be read; if the music before cannot be put back,
  the search result is stopped before the sound comes back; the sound and shuffle are put back at the next
  contact if the WiiM did not answer.
- Searches for short or non-Latin names ("U2", "AC DC", "Кино") no longer try every spelling.
- Favorites of long lists are saved without their tracks (the WiiM reads them again); a failed save no longer
  damages an existing favorite.
- The cover of the previous track no longer stays when the new one cannot be loaded; the screen is no longer
  redrawn in full at each status during playback; volume and play taps wait for the first status of the WiiM.
- Names made of digits and the letters A to F ("2021") are no longer read as hexadecimal; Deezer numbers above
  2 147 483 647; a Deezer track named "album".

### Security
- Setup page: a secret form token and a check of the address protect it from other web pages; field lengths are
  checked on the device.
- Update page: a wrong code adds a delay that doubles (it no longer ends update mode).
- Relay of the simulator: it connects only to the known WiiMs and their ports; a key is necessary when it listens
  on the network (`WIIMSIM_RELAY_KEY`).
- First start: the Tab5 shows the WiiM that it found and waits for a touch, also when there is only one (its
  certificate is kept at the first contact). It does not follow a WiiM to a new address before that first contact.
- CI: actions pinned to commits, Python packages to versions. SECURITY.md states the limits of the unsigned update.

### Changed (no change of behaviour)
- Large files split so that several people can work at the same time: `ui.cpp` (5 files), `wiim_worker.cpp`
  (5 files), `lib/core/src/core.cpp` (8 topic files), `platform/pc/pc_platform.cpp` (7 files), and the unit tests
  (one file per module). The reference screens and the self-tests are identical before and after.

### Tests
- Reference screens: CI compares 140 rendered screens (English and French, 1280×720 and 800×480, landscape and
  portrait) with `tools/preview/golden.sha256`. `tools/check.sh` runs every CI gate on a computer.
- CI fails on a compiler warning in the project's files (PC builds).
- The self-tests look for stable tags (`[search.restore] create=200 …`) on the terminal, not for French Journal
  sentences. The Journal on the screen is unchanged.
- Unit tests run with AddressSanitizer and UBSan, with malformed input for every parser (50 tests).
- The mock WiiM has a play-queue service (shuffle, queues that fill step by step, a mode that loads nothing). The
  relay self-test now runs the search through a WiiM queue: results in order, music before put back (paused and
  playing), a result played from its album, an empty WiiM, a search cut by a restart.

## [0.1.1-beta] — 2026-11-16 — direct Qobuz search becomes an option

### Changed
- **Direct Qobuz search is now an option, off by default** (setup page › Advanced). It reuses the Qobuz access of
  the WiiM's own integration (`GetUserInfo`), which is not an official Qobuz API. By default the WiiM does the
  Qobuz search and the favorites list itself (the music stops a few seconds during a search) and the Tab5 never
  reads a Qobuz access; the Albums choice shows only with the option. README: new "Unofficial interfaces" section.
- The Tab5 no longer asks the WiiM for a Deezer access (there is no Deezer token, only airable's).

### Fixed
- After a search through the WiiM, when nothing played before, the WiiM is stopped again (not left paused on a
  result).

## [0.1.0-beta] — 2026-11-16 — first public release

Beta: only the M5Stack Tab5 is tested. Items marked *(simulator)* were verified with the real-WiiM simulator
(`simreal`) on a real WiiM, not yet on a Tab5.

### Added
- **English interface** (default) with French as an option: setup page › *Language / Langue*. QWERTY keyboard in
  English, AZERTY in French; voice search follows the language. The Journal stays in French.
- **Qobuz album search** (search screen › *Albums*): touch an album to play it, touch its heart to add it as a tile
  without playing it. *(simulator)*
- **Album covers in the result lists**, loaded per page without flicker. *(simulator)*
- **Deezer Flow tile**: added automatically the first time Flow plays on the WiiM; replays Flow with new tracks;
  a removed tile does not come back by itself. *(simulator)*
- The heart button recognizes the queue that plays (not only the album name), so Flow and albums match exactly.
- `tools/check_ca_bundle.py`: checks that the Tab5 can verify the HTTPS servers it uses.

### Fixed
- **Voice search: "OpenAI unreachable"** on the Tab5. api.openai.com sends a root cross-signed by GlobalSign Root
  CA (R1), which ESP-IDF's bundle lacked; R1 is added (`certs/extra_roots.pem`). TuneIn covers had the same problem.
- Favoris Qobuz list while music plays: the previous music stopped instead of coming back; with shuffle on, the
  wrong track came back; the position was lost. *(simulator)*
- A track chosen in the Favoris Qobuz list played another track. *(simulator)*
- A new Tab5 had no Favoris Qobuz tile (the Qobuz queue model was never found on the WiiM). *(simulator)*
- Deezer covers stayed grey in lists (progressive JPEGs): a baseline variant is downloaded. *(simulator)*
- Settings menu: titles drawn over the top edge of their rows; long subtitles off screen in portrait.
- Relay for the simulator: TLS 1.3 connections could hang (the self-test failed about one time in two).

### Changed
- Qobuz refuses the WiiM's token for favorites (401): no favorite-album tiles from Qobuz; use the album search.
- README redesigned; technical parts moved to docs/DEVELOPERS.md; screenshots in English.

## Before 0.1.0-beta

### Added
- PC simulator (`pio run -e sim`, tools/sim/README.md): the real app with a fake WiiM in a window (mouse = touch),
  scripted taps and screenshots; CI runs a tour of the screens on every push.
- Simulator with your real WiiM (`pio run -e simreal`): the Tab5's own network code through a small relay on the
  home network (`tools/relay/wiim_relay.py`, Python standard library). CI checks it against a mock WiiM.

### Fixed (found with the simulator)
- The search result message showed an empty box instead of "▶" (no such glyph in the font): now "Lecture : …".
- A tile whose cover is the cover of the music playing never got its image.
- While loading, the status chips could cover the battery level.
- Deezer favorites from a public Deezer profile (setup page › *Favoris Deezer*: profile number or address; no key):
  favorite albums as tiles, and a **Favoris Deezer** tile with the list of favorite tracks (touch one to play it).
  The unverified token from the WiiM is still tried once per start when no profile is set.

### Removed
- Spoken commands ("joue FIP", "plus fort"…) and the Radio Browser station search: the microphone is only for the
  search field now. The Journal *Essais* button and its queue tests (their results are in docs/WIIM_API_NOTES.md).
- The **Favoris Qobuz** tile opens the list of favorite tracks: touch one to play it (before, the tile played all
  favorites from the first one). The list comes from Qobuz with the access the WiiM gives; without it, the WiiM
  loads the list itself (sound muted, previous music put back).
- Update screen: QR code of the update page, so a phone opens it without typing the address. The page no
  longer filters the file picker on `.bin` (some phones greyed the file out).
- Setup page › *Avancé* › *Tout effacer*: factory reset (settings, OpenAI key, favorites and their backup).
- Memory report in the Journal at start and every 10 minutes (internal heap, minimum, PSRAM, WiiM task stack), to
  find leaks in long tests.
- Local builds show their git commit as the version (`local-<sha>`, `-modifie` with uncommitted changes).
- Settings backup in LittleFS: a full USB flash (merged image at 0x0, which overwrites the NVS) no longer loses the
  Wi-Fi, WiiM, OpenAI key and options; they are restored at the next start. OTA updates already kept them.
- Sleep: Settings › *Mise en veille* turns the Tab5 off; optional automatic sleep after 30 min / 1 h / 2 h without
  touch or music (setup page, default never). The power button turns it on again (touch cannot wake the P4).
- Deezer favorite albums as tiles (experimental): Deezer token asked to the WiiM (`GetUserInfo`), albums from
  `api.deezer.com/user/me/albums`, played through the WiiM's airable gateway. *File du WiiM* lists the linked services.
- Qobuz favorite albums as tiles (with covers) and a "Favoris Qobuz" tile for favorite tracks.
- Qobuz search without playback: the remote searches Qobuz with the access the WiiM gives on the LAN (UPnP
  `GetUserInfo`, found by a session on the user's network); the music only changes when a result is chosen.
- M5Stack Tab5 (ESP32-P4) firmware: color "now playing" UI with album art, accent color from the cover,
  progress/seek, volume pill, preset tiles with artwork (from the WiiM), pages and swipe, active preset marker,
  loading and offline states, notifications.
- Ambient view with microphone spectrum visualizer; pick-up-to-wake (IMU); backlight dimming.
- Automatic WiiM discovery (SSDP + `getStatusEx`), choice among several players, following the same player
  (uuid) across IP changes.
- Phone setup portal (QR code), optional OpenAI key for voice search (dictation in the search field).
- Over-the-air updates from a browser (6-digit code), firmware version display.
- PC preview tool rendering the real UI to PNG; native unit tests for all hardware-free logic.
- Navigation bar on the left (top in portrait): 🏠 main screen, ⚙ settings; also shown on the settings and log
  screens. The ambient view has its own 🏠. A 3 s press on the album art still opens the settings.
- Favorites: the button left of the playback controls saves the album or playlist that plays (a copy of the
  WiiM's play queue, no streaming-service key needed); favorites appear as tiles after the WiiM presets (small
  heart badge) and replay with one tap. Touch the button again on a favorite album to remove it. Up to 24.
- Deezer search (Qobuz | Deezer switch on the search screen): the remote searches Deezer's public API (no key);
  the WiiM plays the album of the chosen track through its airable gateway, starting at that track.
- Qobuz search without any key (verified on a WiiM; results are read back until their number is stable): the search button in the left bar opens an on-screen keyboard; the WiiM runs
  the track search with its own Qobuz access, the remote reads the results back and lists them (title, artist,
  album); a tap plays a track. Needs one Qobuz album played from the WiiM Home app once (as a model).
- Journal › *Essais*: asks the WiiM to load the user's Qobuz favorite tracks, then albums, then Deezer albums and
  tracks found with Deezer's public search (through the WiiM's airable gateway), into queues (no playback), and
  logs what it loaded.
- Settings menu: touch test and event log (for hardware bug reports). The log wraps long lines and has a
  *File du WiiM* button: a read-only check of the WiiM's undocumented UPnP PlayQueue service (first step towards
  Qobuz favorites).
- Dark, phone-friendly setup and update pages: Wi-Fi list with signal strength, show-password toggle, folded
  advanced sections, file name and progress on the update page.

### Changed
- Faster screens: fonts are no longer reloaded when already loaded; on the search keyboard a key press redraws only
  the key and the field; the waiting rings redraw only the row of playback buttons.
- Qobuz search: the WiiM starts the search queue as soon as it is created, so the remote mutes the WiiM during the
  search, then puts back what was playing before (same track and position) or stops playback, then unmutes.
- Voice is now only for the search: a microphone in the search field, tap to start listening and tap the red
  button to stop; the text heard fills the field and the search starts. No more microphone on the main screen.
- Settings › Configuration restarts the Tab5 into setup mode instead of switching live (that froze the device);
  a tap on the screen cancels setup mode when the Tab5 is already configured.
- Loading feedback: after ⏮/⏭, play/pause, a preset, a favorite or a search result, a ring turns around the
  button until the WiiM reports the change (10 s at most).
- Search never starts playback by itself: Qobuz results come from the Qobuz API with the access the WiiM gives
  (`GetUserInfo`); the music only changes when a result is chosen. (`CreateQueue` has no `QueuePolicy` argument on
  the WiiM and always plays, so the older "QueuePolicy 0" attempt is removed.)
- The main screen always keeps its preset row. Without presets on the WiiM, the row explains how to add them
  (before, the screen looked like the ambient view, so the home button seemed to do nothing).
- All WiiM networking runs in a background task; the screen never waits for the network.
- Kept-alive TLS connection to the WiiM: commands and polls no longer pay a TLS handshake each time; faster
  polling (1.5 s while playing). Redraws are coalesced to one per loop, and the 1 Hz progress tick redraws only
  the progress bar.

### History
- The project started as a remote for the e-ink **M5PaperS3** (ESP32-S3) and moved to the Tab5 for a color
  screen and microphones. The e-ink version is in the git history.
