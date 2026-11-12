# WiiM / LinkPlay HTTP API — practical notes

Notes gathered while building wiimscreen, to complement the official
[HTTP API for WiiM Products (PDF, v1.2)](https://www.wiimhome.com/pdf/HTTP%20API%20for%20WiiM%20Products.pdf).
Sources are marked: **[doc]** official PDF, **[forum]** community reports, **[code]** how wiimscreen handles it.
Corrections from people with other WiiM models are welcome.

## Transport

- Requests: `GET https://<ip>/httpapi.asp?command=<command>` **[doc]**. Some older LinkPlay devices answer on plain
  HTTP too **[code: discovery.cpp tries HTTPS then HTTP]**.
- The certificate is **self-signed**: chain validation is impossible. wiimscreen pins its SHA-256 fingerprint on
  first contact and compares it before every request **[code: wiim_client.cpp]**.
- Responses may come without `Content-Length` and the connection may close without TLS `close_notify`: read until
  the connection closes, with a size cap **[code]**.
- Errors are often the plain text `Failed` **[forum, code]**; simple commands usually answer `OK` **[forum]**.

## Playback status: `getPlayerStatus`

Example from the doc **[doc]**:

```json
{"type":"0","ch":"2","mode":"10","loop":"4","eq":"0","status":"play","curpos":"184919",
 "offset_pts":"184919","totlen":"0","alarmflag":"0","plicount":"0","plicurr":"0","vol":"39","mute":"0"}
```

- **No title, artist or album on WiiM devices** **[doc example, forum]**. Older LinkPlay firmwares include
  `Title`/`Artist`/`Album`, **hex-encoded UTF-8** (`"4c612056696520656e20526f7365"` → *La Vie en Rose*);
  unknown values appear as `unknow`, `Unknown` or `un_known` **[forum, code]**. Use `getMetaInfo` for track info.
- `status`: `play`, `pause`, `stop`, `loading` (preparing playback, e.g. right after a preset) **[doc]**.
- `curpos` / `totlen`: milliseconds. `totlen` is `0` for live streams (radio) **[doc]**.
- `vol` 0–100, `mute` `0`/`1` **[doc]**. All numbers are sent as strings.
- `mode` (source) **[doc]**: `0` none, `1` AirPlay, `2` DLNA, `10`–`19` WiiM/network playlist (`11` USB, `16` SD card),
  `31` Spotify Connect, `32` TIDAL Connect, `40` line-in, `41` Bluetooth, `42` external storage, `43` optical,
  `50` mirror, `60` voice mail, `99` multiroom guest.
- `loop` (read) **[doc]**: `0` loop all, `1` single, `2` shuffle loop, `3` shuffle no loop, `4` no shuffle no loop.
  Note that `setPlayerCmd:loopmode:n` uses **different codes** (`0` sequence no loop, `1` single, `2` shuffle loop,
  `-1` sequence loop).

## Track info: `getMetaInfo`

```json
{ "metaData": { "album": "Country Heat", "title": "Old Dirt Roads", "artist": "Owen Riegling",
  "albumArtURI ": "https://m.media-amazon.com/images/I/51iU0odzJwL.jpg",
  "sampleRate ": "44100", "bitDepth": "16" } }
```

- The doc's example has **keys with a trailing space** (`"albumArtURI "`, `"sampleRate "`) **[doc]**. Accept both
  spellings **[code: core.cpp parseMetaInfo]**.
- Values are plain text here (not hex). Unknown values: `unknow`, `un_known` **[code]**.
- `albumArtURI` points to the streaming service's CDN (HTTPS); it may update a few seconds after the title.
- Neither `getPlayerStatus` nor `getMetaInfo` says which preset is playing.

## Presets: `getPresetInfo`, `MCUKeyShortClick`

```json
{ "preset_num": 3, "preset_list": [
  { "number": 1, "name": "BBC Radio Norfolk", "url": "http://…m3u8", "source": "Linkplay Radio",
    "picurl": "http://cdn-profiles.tunein.com/s6852/images/logoq.jpg?t=638353933090000000" }, … ] }
```

- `number` is an **integer** (not a string) and the list is **not contiguous** (1, 6, 8…) **[doc]**.
- `picurl` is the preset's artwork — perfect for tiles **[doc]**.
- Play preset *n* (1–12): `MCUKeyShortClick:n` **[doc]**.

## Commands (`setPlayerCmd:…`)

| Command | Effect |
|---|---|
| `onepause` | Toggle play/pause **[doc]** |
| `pause` / `resume` / `stop` | **[doc]** |
| `prev` / `next` | **[doc]** |
| `vol:n` | Volume 0–100 **[doc]** |
| `mute:1` / `mute:0` | Mute / unmute **[doc]** |
| `seek:s` | Seek, in **seconds** (not ms) **[doc]** |
| `loopmode:n` | See codes above **[doc]** |
| `play:<url>` | Play a stream URL, e.g. a radio. wiimscreen percent-encodes everything but `:` and `/` **[code — unverified on hardware]** |
| `switchmode:<source>` | Change input **[doc]** |

Also documented: `EQOn`, `EQOff`, `EQGetStat`, `EQGetList`, `EQLoad:<name>`, `reboot`, `setShutdown:<sec>`
(sleep timer), `getShutdown`, `setAlarmClock:…`, `getNewAudioOutputHardwareMode`,
`setAudioOutputHardwareMode:<n>` **[doc]**.

## Device identity: `getStatusEx`

Useful fields **[doc]**: `DeviceName` (name set in WiiM Home), `uuid` (unique, stable), `ssid`, `project`
(model, e.g. `Muzo_Mini`), `firmware`, `MAC`, `group` (`0` master, `1` multiroom guest), `internet`.

wiimscreen identifies a player by `uuid`, which survives renaming and IP changes **[code]**.

## Discovery

WiiM players are UPnP **MediaRenderers**. An SSDP `M-SEARCH` to `239.255.255.250:1900` with
`ST: urn:schemas-upnp-org:device:MediaRenderer:1` returns their `LOCATION` (IP); then call `getStatusEx` on each
IP to keep only WiiM/LinkPlay devices and read their name and `uuid` **[code: discovery.cpp, core.cpp]**.
Routers with client isolation (guest networks) block this.

## What the local API does not do

- **No catalog search** (Qobuz, Spotify, TIDAL…): search the service's own API, then play a URL or use Connect
  **[forum]**. wiimscreen searches radios in [Radio Browser](https://www.radio-browser.info/) and plays the stream
  URL with `setPlayerCmd:play:`.
- No push notifications: poll (wiimscreen polls every 1.5 s while playing, 3 s otherwise, over a kept-alive
  TLS connection; the WiiM answers `Connection: keep-alive` requests and keeps the socket open for a few seconds).
