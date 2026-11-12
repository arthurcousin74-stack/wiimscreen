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

## Play queue: UPnP `PlayQueue` service (undocumented)

Not in WiiM's PDF; known from community reverse engineering **[forum]**. Réglages › Journal › *File du WiiM* runs a
read-only check and logs what the device answers.

**Seen on a real WiiM (wiimscreen, 2026-11):** `description.xml` on port 49152 lists the PlayQueue service;
`BrowseQueue` returns the queue started from the WiiM Home app for a Qobuz album, with `<SourceName>Qobuz`, a
`<SearchUrl>` `https://www.qobuz.com/api.json/0.2/album/get?album_id=<id>&offset=0&limit=50` **and** the track list
(`<Track1>`… with DIDL-Lite titles). The same queue answered under several names. Replaying a saved queue
(`CreateQueue` + `PlayQueueWithIndex`, used by the favorites) is not confirmed yet.

- Plain HTTP SOAP on the UPnP port: device description at `http://<ip>:49152/description.xml` (59152 on some
  LinkPlay players). Service type `urn:schemas-wiimu-com:service:PlayQueue:1`, control URL usually
  `/upnp/control/PlayQueue1`, action list in `/upnp/PlayQueueSCPD.xml` **[forum]**.
- Header `SOAPACTION: "urn:schemas-wiimu-com:service:PlayQueue:1#<Action>"`. Actions reported: `CreateQueue`,
  `PlayQueueWithIndex`, `BrowseQueue`, `AppendTracksInQueue`, `DeleteQueue`, `ReplaceQueue`, `GetQueueIndex`,
  `SetQueueLoopMode`… **[forum, linkplay-console]**.
- `CreateQueue(QueueContext)`: `QueueContext` is an escaped XML `<PlayList>` with `<ListName>`,
  `<ListInfo>` (`SourceName`, `TrackNumber`, `SearchUrl`, `Quality`…) and `<Tracks><Track1>…` (each with `URL`,
  `Source`, `Id`, `Key`, `Metadata` = DIDL-Lite, escaped again). Then `PlayQueueWithIndex(QueueName, Index)` with
  the **same name**; the index is 1-based **[linkplay-console]** (verified 2026-11-12, see *Loading a queue without
  playing it*). Older examples pass a second argument `QueuePolicy`: this WiiM's SCPD has **no such argument**
  (`QueuePolicy` is only a state variable), so it is ignored.
- **Qobuz**: a 2023 forum post builds a queue with `<SourceName>Qobuz</SourceName>`, a `<SearchUrl>` pointing to
  `https://www.qobuz.com/api.json/0.2/album/get?album_id=…` (or `playlist/get?…`) and `<Login_username>`, **without
  tracks**; the WiiM then fetches and streams the tracks with its own Qobuz login **[forum, unconfirmed]**. If this
  holds, an album can be started from its ID alone, without any Qobuz API key on our side.
- `BrowseQueue(QueueName)` returns the stored queue (`QueueContext`); `TotalQueue` is tried for an overview.
  An unknown name answers HTTP 500 **[linkplay-console]**.

Sources: [WiiM forum (Qobuz queue)](https://forum.wiimhome.com/threads/possible-to-start-a-qobuz-playlist-and-or-wiim-preset-from-the-http-api.968/),
[Arylic forum (SOAP example)](https://forum.arylic.com/t/http-control-multiroom-tunein/241),
[linkplay-console PR 35](https://github.com/laozhudu/linkplay-console/pull/35),
[wiimplay (Go)](https://pkg.go.dev/github.com/shumatech/wiimplay/upnp).

### Searching Qobuz

- The WiiM offers no search. Qobuz's own API is partner-only. Open-source clients take the app ID from the web
  player's `bundle.js` (`production:{api:{appId:"…"`, about 1.5 MB into a 9.5 MB file in 2026-11).
- **As of 2026-11, `album/search` and `catalog/search` answer `401 User authentication is required`** with the
  app ID alone: searching needs a logged-in Qobuz user (`user_auth_token`), i.e. the user's Qobuz credentials on
  the device. wiimscreen does not do this. *(2026-11-12: the WiiM itself hands out that token over UPnP,
  `GetUserInfo` — see the end of *Loading a queue without playing it*.)*
- **Key-free search works through the WiiM (verified on a real WiiM, 2026-11).** A queue built from a real Qobuz
  queue (`wr::queueFromTemplate`: same SourceName, `Login_username`, quality; no tracks) with:
  - (A) the album `SearchUrl` `album/get?album_id=…`: the WiiM fetched the 48 tracks itself and played them;
  - (B) `SearchUrl` = `https://www.qobuz.com/api.json/0.2/track/search?query=<q>&offset=0&limit=50`: the WiiM
    accepted the queue, filled it (7 tracks, with DIDL-Lite titles, artists, albums) and played the first one,
    **but the tracks had nothing to do with the query** — most likely an old queue of the same name read back.
    With a fresh queue name each time (and `DeleteQueue` first), **the search works** (verified, 2026-11):
    "daft" → Daft Punk tracks, "stromae" → Stromae; `%20` for spaces; the WiiM stores the `SearchUrl` unchanged and
    **fills the queue without `PlayQueueWithIndex`** (nothing plays until a result is chosen), but
    **progressively** (1–2 tracks after ~1 s, 7 after ~6 s): read it back until the count is stable.
  wiimscreen's search screen uses (B). It first reads the queue back without `PlayQueueWithIndex` (whether the
  WiiM fills it before playing is not known yet), then starts it if it stays empty. *(Changed: the user reported
  that a search started the first result; the remote now never starts a search queue and creates it with
  `QueuePolicy 0` first — 1, then no argument, if refused. Whether 0 avoids playback: **no** — `CreateQueue` has no
  `QueuePolicy` argument on this WiiM and always plays, see *Loading a queue without playing it*.)*

- Journal › *Essais*: same trick with `favorite/getUserFavorites?type=tracks` then `type=albums`, read back
  without playing. **First result on a real WiiM: the tracks query loaded 1 favorite track** (so the WiiM does add
  the user's token); the read-back was probably too short (now: stable for ~3 s, 3 s minimum). Albums: not known.
- **PlayQueue actions on a WiiM (SCPD, 2026-11):** `CreateQueue ReplaceQueue DeleteQueue BackUpQueue AppendQueue
  BrowseQueue BrowseQueueEx SetQueueLoopMode GetQueueLoopMode SetQueuePolicy PlayQueueWithIndex
  AppendTracksInQueue AppendTracksInQueueEx RemoveTracksInQueue MoveTracksInQueue GetKeyMapping SetKeyMapping
  GetQueueOnline SearchQueueOnline SetQueueRecord SetSongsRecord UserRegister…` (all tried 2026-11-12, see below).

### Loading a queue without playing it (verified on a real WiiM Pro, 2026-11-12)

Tested from a computer on the same network, WiiM stopped with nothing loaded, muted; each action alone, playback and
the queue read back ~4–9 s later. Queue model: `<PlayList><ListName>…</ListName><ListInfo><Radio>0</Radio>
<SourceName>Qobuz</SourceName><requestQuality>6</requestQuality><SearchUrl>…</SearchUrl><Login_username>…
</Login_username></ListInfo></PlayList>` (no tracks), `SearchUrl` = `track/search?query=daft%20punk&offset=0&limit=50`
or `album/get?album_id=…&offset=0&limit=50`.

**Arguments as listed by this WiiM's `/upnp/PlayQueueSCPD.xml`** (`>` = output):
`CreateQueue(QueueContext)`, `ReplaceQueue(QueueContext)`, `BackUpQueue(QueueContext)`, `AppendQueue(QueueContext)`,
`DeleteQueue(QueueName)`, `BrowseQueue(QueueName, >QueueContext)`,
`BrowseQueueEx(QueueName, TrackIndex, TrackNums, >QueueContext)`, `SetQueuePolicy(QueueName)`,
`PlayQueueWithIndex(QueueName, Index)`, `AppendTracksInQueue(QueueContext)`,
`AppendTracksInQueueEx(QueueContext, Direction, StartIndex, Play, Action)`,
`RemoveTracksInQueue(QueueName, RangStart, RangEnd, Action)`, `MoveTracksInQueue(QueueName, IndexList, ToIndex)`,
`GetQueueIndex(QueueName, >CurrentIndex, >PreloadingIndex, >CurrentPage, >TrackNums)`,
`GetQueueOnline(QueueName, QueueID, QueueType, Queuelimit, QueueAutoInsert, >QueueContext)`,
`SearchQueueOnline(QueueName, SearchKey, Queuelimit, >QueueContext)`, `SetQueueLoopMode(LoopMode)`,
`GetQueueLoopMode(>LoopMode)`, `SetQueueRecord(QueueName, QueueID, Action)`, `SetSongsRecord(QueueName, SongID,
Action)`, `SetKeyMapping(QueueContext)`, `GetKeyMapping(>QueueContext)`, `GetBasicUserInfo(>Result)`,
`GetUserInfo(AccountSource, RefreshToken, >Result)`, `GetUserFavorites(AccountSource, MediaType, Filter, >Result)`,
`SetUserFavorites(AccountSource, Action, MediaType, MediaID, MediaContext, >Result)`,
`GetUserAccountHistory(AccountSource, Number, >Result)`, `UserLogin(…)`, `UserLogout(AccountSource, >Result)`,
`UserRegister(…)`, `SetSpotifyPreset(KeyIndex, >Result)`, `DeleteActionQueue(PressType)`, `TakePlayControl(>Result)`,
`SetRating(Source, TrackID, Rating, >Result)`.

| Action (arguments) | Answer | Plays? | Queue afterwards |
|---|---|---|---|
| `SearchQueueOnline` (`QueueName` `Qobuz` or a new name, `SearchKey` `daft punk`, `Queuelimit` `20`) | 500, `501 GetPlayListFormOnline failed` | no | none |
| `GetQueueOnline` (`QueueName` `Qobuz`, `QueueID` = album id, `QueueType` `album`/`0`/`1`, `Queuelimit` `20`, `QueueAutoInsert` `0`) | 500, same error | no | none |
| `BackUpQueue(QueueContext)` | 200 | **no** | stored with its `SearchUrl`, **`TrackNumber` 0, no tracks** (still 0 after 9 s of `BrowseQueueEx(name, 1, 20)`) |
| `AppendQueue(QueueContext)` | 200 | **no** | same as `BackUpQueue` |
| `ReplaceQueue(QueueContext)` | 200 | **no** | same as `BackUpQueue` |
| `AppendTracksInQueueEx(QueueContext, Direction 0, StartIndex 0, Play 0, Action 0)` on a new name | 500, `501 AddTracksInQueue error` | no | none |
| `SetQueuePolicy(QueueName)` | 500, `501 Action Failed` | no | — |
| `CreateQueue(QueueContext)` after it, and alone | 200 | **yes**, first result | filled: 9 then 15 tracks for the search |

- **Conclusion: no PlayQueue action makes the WiiM fetch a `SearchUrl` without playing.** `BackUpQueue`,
  `AppendQueue` and `ReplaceQueue` store a queue silently, but the WiiM only fetches the tracks when the queue starts
  with `CreateQueue`. `PlayQueueWithIndex` on a queue stored with `BackUpQueue` answers 200 and does nothing (no
  playback, still no tracks).
- **`PlayQueueWithIndex` index is 1-based and follows the queue's own order** (`Index 3` played the queue's third
  track, `GetQueueIndex` → `CurrentIndex 3`; `Index 0` behaves like `1`). With shuffle on (`getPlayerStatus` `loop`
  `3`), **an album queue is already shuffled when created**, differently each time: to start a chosen track, find
  its index by title in the queue read back after `CreateQueue`, not by its album track number.
- **Mute does not stick**: the WiiM was found unmuted again between two trials after a `setPlayerCmd:stop`, with
  nobody touching it. Re-send `mute:1` before each action that may play.
- `TotalQueue` keeps only a couple of queues (older names are dropped); `DeleteQueue` cleans up test queues.

**Search without any queue (the way out):** `GetUserInfo(AccountSource=Qobuz, RefreshToken=0)` answers, with no
authentication, a JSON `Result` holding the linked account (`userName`, `userId`), the Qobuz app ID (`clientId`) and
a valid user token (`accessToken`). `GET https://www.qobuz.com/api.json/0.2/track/search?query=…&limit=…` (and
`album/search`) with headers `X-App-Id: <clientId>` and `X-User-Auth-Token: <accessToken>` answers 200 with real
results (track id, title, performer, album id), **without touching playback**. A search screen can list these, then
play a choice with `CreateQueue` (album `SearchUrl` from the result's album id) + `PlayQueueWithIndex` at the index
found by title. `AccountSource` is case-sensitive (`qobuz` → `501 GetUserInfo failed`); `GetBasicUserInfo` lists
every linked service with its `showName`. Treat the token as a secret: never log, display or store it.

**Used by wiimscreen (2026-11):** the search screen (Qobuz) and the favorite-album tiles use this access
(`WiimWorker::qobuzAccess`, token in RAM only, asked again on 401/403); results play with `CreateQueue` (album
`SearchUrl`) + `PlayQueueWithIndex` at the index found by title. The queue-based search (with mute and restore)
remains as a fallback when `GetUserInfo` fails. `CreateQueue` is now sent with `QueueContext` only.

### Deezer

- **Favorites (wiimscreen, not yet verified):** `GetUserInfo(AccountSource=Deezer|Deezer2|deezer, RefreshToken=0)`;
  if the Result holds `accessToken`/`access_token`, `GET https://api.deezer.com/user/me/albums?limit=50&access_token=…`
  (`wr::parseDeezerAlbums`); each album plays as a `Deezer2` queue `<airable origin>/deezer/album/<id>`. *File du
  WiiM* logs the linked services from `GetBasicUserInfo` (`showName` values) to find the right `AccountSource`.

- A Deezer album started from the WiiM Home app gives a queue with **`SourceName` `Deezer2`** and
  `SearchUrl` **`https://<id>.airable.io/deezer/album/<Deezer album id>`** (airable: the music-service gateway the
  WiiM uses; `<id>` looks specific to the device or account, do not publish it). 11 tracks, read with `BrowseQueue`.
- Deezer's public search `https://api.deezer.com/search?q=…` needs no key (`wr::parseDeezerTracks`).
- **The WiiM starts playing a queue as soon as it is created** (user report, 2026-11, with `QueuePolicy 0` too).
  The Qobuz search therefore notes what plays before (title, position, `CurrentQueue` and the track's place in
  it) and afterwards puts it back (`CreateQueue` + `PlayQueueWithIndex` + seek), or stops playback if nothing
  played or the music came from outside a queue. The WiiM is muted (`setPlayerCmd:mute:1`) for the whole search and
  unmuted after (user report: a track could still be heard). *File du WiiM* now logs the arguments of `CreateQueue`,
  `SetQueuePolicy`, `SearchQueueOnline`, `GetQueueOnline`, `BackUpQueue`, `AppendQueue`, `BrowseQueueEx`, to look
  for a way to load a queue without playing it.
- **Other PlayQueue actions** (signatures from the Go library `wiimplay`, not yet tried on a WiiM):
  `SearchQueueOnline(QueueName, SearchKey, Queuelimit) -> QueueContext`,
  `GetQueueOnline(QueueName, QueueID, QueueType, Queuelimit, QueueAutoInsert) -> QueueContext`,
  `BackUpQueue(QueueContext)`, `SetQueuePolicy(QueueName)`, `ReplaceQueue(QueueContext)`, `AppendQueue(QueueContext)`.
  Journal › *Essais* tries them to load results **without starting playback** (sound muted during the test).
  Source: [wiimplay](https://pkg.go.dev/github.com/shumatech/wiimplay/upnp).
- **Verified on a real WiiM (2026-11):** a `Deezer2` queue with `SearchUrl` `https://<id>.airable.io/deezer/album/<id>`
  (no tracks) is filled by the WiiM (127 tracks for a large album); `/deezer/track/<id>` and the guessed airable
  search paths (`/deezer/search?q=`, `/deezer/search/tracks?q=`) load nothing.
- wiimscreen's Deezer search: the remote searches `api.deezer.com`, then plays the album of the chosen track and
  starts it at that track (index found by title in the loaded queue).
- *File du WiiM* also keeps a model of queues from other sources (`/fav/model_<source>.xml`, e.g. Deezer) for
  later work.
- **Reading Deezer favorites over UPnP does not work on this WiiM (tested 2026-11-12).** `GetUserFavorites`
  (`AccountSource` `Deezer2` and `Deezer`, each with `MediaType` `album`/`track`/`playlist`, `Filter` empty) answers
  HTTP 500 `UPnP 501 UserRegister failed` every time — the same error the firmware gives for actions it does not
  implement. So the favorites list cannot be read through the PlayQueue service. The alternative that
  `runDeezerFavorites()` assumes — `GetUserInfo(AccountSource=Deezer…)` handing out a Deezer `access_token` usable
  against `api.deezer.com/user/me/albums` — is **still unverified** (that path reads an account credential and the
  check was not completed). Treat `runDeezerFavorites()` as experimental until the token shape is confirmed.

## Other UPnP services (AVTransport, RenderingControl, ConnectionManager, QPlay)

`description.xml` (port 49152) lists **five** services, not just PlayQueue (verified on a WiiM Pro, 2026-11-12). All
were enumerated from their SCPD; the reads below are plain SOAP `POST` with `SOAPACTION:
"urn:schemas-upnp-org:service:<Service>:1#<Action>"`, every input has `<InstanceID>0</InstanceID>`.

| Service | controlURL | SCPD |
|---|---|---|
| `AVTransport:1` | `/upnp/control/rendertransport1` | `/upnp/rendertransportSCPD.xml` |
| `RenderingControl:1` | `/upnp/control/rendercontrol1` | `/upnp/rendercontrolSCPD.xml` |
| `ConnectionManager:1` | `/upnp/control/renderconnmgr1` | `/upnp/renderconnmgrSCPD.xml` |
| `PlayQueue:1` (wiimu) | `/upnp/control/PlayQueue1` | `/upnp/PlayQueueSCPD.xml` |
| `QPlay:1` (Tencent) | `/upnp/control/QPlay1` | `/upnp/QPlaySCPD.xml` |

### AVTransport — reading now-playing, and loading a URI

Actions: `GetInfoEx`, `GetMediaInfo`, `GetPositionInfo`, `GetTransportInfo`, `GetTransportSettings`,
`GetDeviceCapabilities`, `GetCurrentTransportActions`, `GetPlayType`, `Play(Speed)`, `Pause`, `Stop`, `Next`,
`Previous`, `Seek(Unit, Target)`, `SeekForward`, `SeekBackward`, `SetPlayMode(NewPlayMode)`,
`SetAVTransportURI(CurrentURI, CurrentURIMetaData)`, `SetNextAVTransportURI(NextURI, NextURIMetaData)`.

- **`GetInfoEx` is the best read on the device: one call, 30 fields** (far more than httpapi `getPlayerStatus`), all
  read-only, no playback change. Fields seen: `CurrentTransportState` (`PLAYING`/`PAUSED_PLAYBACK`/`STOPPED`…),
  `CurrentSpeed`, `Track`, `TrackDuration`, `TrackMetaData` (DIDL-Lite: title, artist, album, albumArtURI),
  `TrackURI`, `RelTime` (position `hh:mm:ss`), `CurrentVolume`, `CurrentMute`, `CurrentChannel`, `LoopMode` (same
  codes as httpapi `loop`), `PlayType`, `SlaveFlag`/`MasterUUID`/`SlaveList` (multiroom; `SlaveList` is JSON with
  `wmrm_version`, `surround`, `group_type`), `PlayMedium` (`SONGLIST-NETWORK`, `STATION-NETWORK`, `SONGLIST-LOCAL`…),
  `TrackSource` (`Qobuz`, `Deezer`, …), `InternetAccess`, `BatteryFlag`/`BatteryPercent` (0 on mains models; real on
  portables), `AlarmFlag`, `SpotifyActive`. This gives title/artist/album/art **and** the source and loop mode in a
  single request — unlike `getMetaInfo`, which omits source and loop. `AbsTime` is `NOT_IMPLEMENTED`;
  `RelCount`/`AbsCount` are `2147483647` (unset).
- **SENSITIVE:** for a streaming track, `GetInfoEx.TrackURI` (and `GetMediaInfo` / `GetPositionInfo` metadata)
  contains the **resolved, HMAC-signed CDN URL**, which embeds the account user id and app id (e.g.
  `https://streaming-qobuz-std.akamaized.net/file?uid=<account>&eid=…&app_id=…&hmac=…`). Never log, display, cache
  or commit it. wiimscreen should read only title/artist/album/art/source from the DIDL and ignore `TrackURI`.
- `GetDeviceCapabilities.PlayMedia` lists the media kinds the device accepts:
  `NONE,STATION-NETWORK,SONGLIST-NETWORK,SONGLIST-LOCAL,SONGLIST-LOCAL_TF,THIRD-DLNA,AIRPLAY,UNKNOWN`.
- `GetCurrentTransportActions.Actions` is the valid next actions (`Play,Stop` while paused) — handy to enable/disable
  remote buttons.
- **`SetAVTransportURI(CurrentURI, CurrentURIMetaData)` is the standard UPnP "load a direct URI" primitive** — a
  different path from PlayQueue's `CreateQueue`. In UPnP it sets the media **without** starting playback (you then
  call `Play`). **Not yet tested on this WiiM** (it is a write that replaces the current media; not run while music
  was paused mid-track). Worth testing for: casting an arbitrary stream URL (radio, a `/deezer/album/<id>` airable
  URL, any `Sink` format below) and whether this WiiM honours set-without-play or auto-starts like `CreateQueue`.
  When testing: note `GetInfoEx` first, mute, then restore.
- `GetMediaInfo.CurrentURI` is **empty for queue/songlist playback** (the track comes from the PlayQueue, not an
  AVTransport URI); `GetInfoEx.TrackURI` still resolves it. So AVTransport reads describe the current track but do
  not expose the queue — use PlayQueue `BrowseQueue` for that.

### RenderingControl — volume, mute, EQ, presets, multiroom, alarms, rename

Actions: `GetVolume(Channel)`/`SetVolume(Channel, DesiredVolume)`, `GetMute(Channel)`/`SetMute(Channel,
DesiredMute)`, `GetEqualizer(Channel)`/`SetEqualizer(Channel, DesiredEqualizer)`, `GetChannel`/`SetChannel`,
`ListPresets`/`SelectPreset(PresetName)`, `GetSimpleDeviceInfo`, `GetControlDeviceInfo`, `MultiRoomJoinGroup(MasterInfo)`,
`MultiRoomLeaveGroup`, `SetAlarmQueue(AlarmContext)`/`GetAlarmQueue(AlarmName)`/`DeleteAlarmQueue(AlarmName)`,
`SetDeviceName(Name)`, `AirplayAutoSyncDelay(Name)`, `StreamServicesCapability`/`SetStreamServicesCapability`.

- `GetEqualizer` returned `0` (EQ flat/off). `ListPresets.CurrentPresetNameList` returned `FactoryDefaults` — these
  are **sound/EQ presets, not the radio presets** of httpapi `getPresetInfo` (a different list).
- `MultiRoomJoinGroup`/`MultiRoomLeaveGroup` are the UPnP way to build/break a multiroom group; `GetInfoEx`
  `SlaveFlag`/`MasterUUID`/`SlaveList` read the group back.
- `SetDeviceName` renames the player (mirrors `friendlyName`). `SetAlarmQueue` is the alarm-clock mechanism.
- **SENSITIVE:** `GetControlDeviceInfo` returns `Router` and `Ssid` (the home Wi-Fi name) — do not read into logs or
  commit. `GetSimpleDeviceInfo` is the safer device read (name, volume, channel, multiroom) without network names.
- All `Set*` here are writes — not tested while the device was in use.

### ConnectionManager — supported formats

`GetProtocolInfo` returns an empty `Source` (the WiiM is a renderer, not a server) and a `Sink` list of **~20
`http-get` protocol entries**. Audio MIME types accepted: `aac, ac3, ape, flac, m4a, mp3, mpeg, ogg,
vnd.dlna.adts, wav, wma, x-ape, x-ms-wma, x-wav`. This is the format whitelist for anything cast via
`SetAVTransportURI` (so FLAC and WAV/LPCM direct URLs are fine). `GetCurrentConnectionIDs` /
`GetCurrentConnectionInfo` enumerate active UPnP connections.

### QPlay — Tencent QQ Music

A full queue service for QQ Music (China): `InsertTracks`, `RemoveTracks`, `RemoveAllTracks`, `GetTracksInfo`,
`SetTracksInfo`, `GetTracksCount`, `GetMaxTracks`, `QPlayAuth(Seed) -> Code, MID, DID`, `SetNetwork(SSID, Key, …)`.
`QPlayAuth` is the device-pairing handshake. Not relevant to a Qobuz/Deezer user; listed for completeness.

## What the local API does not do

- **No catalog search** (Qobuz, Spotify, TIDAL…): search the service's own API, then play a URL or use Connect
  **[forum]**. wiimscreen searches radios in [Radio Browser](https://www.radio-browser.info/) and plays the stream
  URL with `setPlayerCmd:play:`.
- No push notifications: poll (wiimscreen polls every 1.5 s while playing, 3 s otherwise, over a kept-alive
  TLS connection; the WiiM answers `Connection: keep-alive` requests and keeps the socket open for a few seconds).
