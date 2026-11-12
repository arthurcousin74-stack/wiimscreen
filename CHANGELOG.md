# Changelog

Versions follow [Semantic Versioning](https://semver.org/). Firmware builds show their version in the
*Réglages* menu (`<build>-<commit>` for CI builds, `vX.Y.Z` for releases).

## [Unreleased] — first public release candidate (v0.1.0)

Hardware testing in progress.

### Added
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
- Phone setup portal (QR code), optional OpenAI key for French voice commands (presets, playback, volume,
  Radio Browser station search).
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
- Search never starts playback by itself: queues are created with `QueuePolicy 0` first, and the remote no
  longer starts the queue when results are slow.
- The main screen always keeps its preset row. Without presets on the WiiM, the row explains how to add them
  (before, the screen looked like the ambient view, so the home button seemed to do nothing).
- All WiiM networking runs in a background task; the screen never waits for the network.
- Kept-alive TLS connection to the WiiM: commands and polls no longer pay a TLS handshake each time; faster
  polling (1.5 s while playing). Redraws are coalesced to one per loop, and the 1 Hz progress tick redraws only
  the progress bar.

### History
- The project started as a remote for the e-ink **M5PaperS3** (ESP32-S3) and moved to the Tab5 for a color
  screen and microphones. The e-ink version is in the git history.
