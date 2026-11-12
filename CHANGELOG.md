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

### History
- The project started as a remote for the e-ink **M5PaperS3** (ESP32-S3) and moved to the Tab5 for a color
  screen and microphones. The e-ink version is in the git history.
