# Changelog

Versions follow [Semantic Versioning](https://semver.org/). Firmware builds show their version in the
*Réglages* menu (`<build>-<commit>` for CI builds, `vX.Y.Z` for releases).

## [Unreleased] — first public release candidate (v0.1.0)

Hardware testing in progress.

### Added
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
- Qobuz search without any key (experimental: the first results had nothing to do with the query; several URL
  forms are now tried and logged): the search button in the left bar opens an on-screen keyboard; the WiiM runs
  the track search with its own Qobuz access, the remote reads the results back and lists them (title, artist,
  album); a tap plays a track. Needs one Qobuz album played from the WiiM Home app once (as a model).
- Journal › *Essai recherche*: key-free experiment that asks the WiiM to play a Qobuz queue made only of an
  album address, then of a track search (no Qobuz key on the remote).
- Settings menu: touch test and event log (for hardware bug reports). The log wraps long lines and has a
  *File du WiiM* button: a read-only check of the WiiM's undocumented UPnP PlayQueue service (first step towards
  Qobuz favorites).
- Dark, phone-friendly setup and update pages: Wi-Fi list with signal strength, show-password toggle, folded
  advanced sections, file name and progress on the update page.

### Changed
- The main screen always keeps its preset row. Without presets on the WiiM, the row explains how to add them
  (before, the screen looked like the ambient view, so the home button seemed to do nothing).
- All WiiM networking runs in a background task; the screen never waits for the network.
- Kept-alive TLS connection to the WiiM: commands and polls no longer pay a TLS handshake each time; faster
  polling (1.5 s while playing). Redraws are coalesced to one per loop, and the 1 Hz progress tick redraws only
  the progress bar.

### History
- The project started as a remote for the e-ink **M5PaperS3** (ESP32-S3) and moved to the Tab5 for a color
  screen and microphones. The e-ink version is in the git history.
