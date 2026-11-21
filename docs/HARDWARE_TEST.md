# Hardware test checklist (M5Stack Tab5 + WiiM)

Use this list to check the firmware on a real Tab5. Each test says what to do, what you should see, and which
Journal line to note (Settings ⚙ › **Journal**). Write the result in the table at the end: ✅ works, ❌ fails
(with what you saw), ⏭ not tested.

The screen texts quoted in this list are the French ones (setup page › **Language** › Français). In English (the
default) the screens say the same in English. The Journal is in French in both languages.

Before you report a result, remove private data: WiiM IP address, account names, the airable sub-domain number.
The Journal masks tokens and passwords, but check anyway.

## 0. Preparation

| # | Do | Expected |
|---|---|---|
| 0.1 | Install the firmware with the update page (⚙ › **Mise à jour**). | The Tab5 restarts. |
| 0.2 | Open ⚙ › **Journal**. | `Démarrage, version local-…` with the version you installed. A line `Mémoire : interne … ko (min …), PSRAM … ko, pile WiiM … o`. Note the four numbers. |
| 0.3 | In the WiiM Home app, play a Qobuz album once. If you use Deezer, also play a Deezer album once. | The Tab5 shows the music. (The remote copies a model of these queues; Qobuz and Deezer features need it.) |
| 0.4 | Touch **File du WiiM** in the Journal. | `File : services liés …` lists Qobuz (and Deezer if linked). |

## 1. Quick fixes (firmware `bd76a4f` and later)

| # | Do | Expected | Journal |
|---|---|---|---|
| 1.1 | Play a Deezer album from WiiM Home. Search ⌕ › **Deezer** › an artist › touch a result (the WiiM loads the album: several seconds). At once touch 🏠, then the volume. | The volume reacts within about 1 s, not after the album has loaded. | `WiiM <- setPlayerCmd:vol:… (… ms)` before the `Deezer ▶ …` line. |
| 1.2 | Only with an OpenAI key (firmware with `certs/extra_roots.pem`: before it, « OpenAI unreachable » every time): search ⌕ › microphone › say an artist › red button. While « Transcription… » shows, touch keys. | The screen stays responsive; the text fills the field; the search starts. | `Transcription : …`, then `Recherche Qobuz : …`. |
| 1.3 | Leave the Tab5 on for 30 minutes. | A new `Mémoire : …` line every 10 minutes. | Note the numbers. |
| 1.4 | ⚙ › **Configuration** (setup mode). On the phone, open the page › **Avancé**. | A box **Tout effacer…** is visible. Do not tick it yet (see test 9.3). | — |

## 2. Qobuz

| # | Do | Expected | Journal |
|---|---|---|---|
| 2.0 | Default settings (direct Qobuz search **off**): search ⌕ › Qobuz › « miles davis » › ↵. | The **Albums** choice is not shown. Results after about 10 s; the music stops during the search and comes back. No `Qobuz : accès…` line in the Journal. Then turn on setup page › **Advanced** › **Direct Qobuz search** for tests 2.1–2.11. | `Recherche Qobuz par une file du WiiM (recherche directe non choisie)` |
| 2.1 | Look at the Journal after the start. | `Qobuz : accès par le WiiM obtenu (GetUserInfo 200)`. If it says `indisponible`, note the code: many tests below then use the slower fallback. | `Qobuz : accès …` |
| 2.2 | Same Journal. | `Qobuz : albums favoris 200, N`. N = number of favorite **albums** in your Qobuz account. On the WiiM tested with the simulator (2026-11-16), Qobuz refuses this (no other way found, see WIIM_API_NOTES): `albums favoris 401, 0 (Qobuz refuse l'accès du WiiM aux favoris…)`. Note which one you get. | `Qobuz : albums favoris …` |
| 2.3 | 🏠 › swipe the tiles. | If N > 0: album tiles with covers after the presets. With 401: no album tiles, but the **Favoris Qobuz** tile shows (also on a new Tab5: the model is copied from the WiiM's queues). | `Qobuz : modèle de file enregistré` |
| 2.4 | Touch one Qobuz album tile. | A ring turns on ⏯, then the album plays from track 1. | `Album Qobuz ▶ … CreateQueue 200, Play 200` |
| 2.5 | Touch the **Favoris Qobuz** tile. | « Chargement de tes favoris Qobuz… », then the list of favorite tracks. **No music change** before you choose. | `Favoris Qobuz : N morceau(x), liste sans lecture` (or `(file)` for the fallback) |
| 2.5b | Do 2.5 while music plays. | The music stops a few seconds (muted), then the same track comes back at the same place, also with shuffle on. | `Avant la recherche : lecture « … » (file connue)`, `Après la recherche : musique d'avant remise (200, 200, essai 1)` |
| 2.5c | In the list of 2.5, touch **Play all**; open the list again and touch **Shuffle**. | Play all: the first favorite plays, shuffle off. Shuffle: another favorite, shuffle on (WiiM Home shows it). | `[favlist.play] deezer=0 shuffle=0 ok=1`, then `shuffle=1 ok=1` |
| 2.6 | Touch one track in the list. | That track plays (then the rest of its album, or of the list for the fallback). | `Qobuz ▶ « … » (piste k) : CreateQueue 200, Play 200` (fallback: `Liste Qobuz ▶ …`) |
| 2.7 | Search ⌕ › Qobuz › type « miles davis » › ↵. | Results in a few seconds. **The music does not change** before you choose. | `Recherche Qobuz directe « … » : N morceau(x) (… ms)` |
| 2.8 | Touch a result. | That track plays. | `Qobuz ▶ …` |
| 2.10 | Search ⌕ › **Albums** › type « miles » › ↵. Touch the heart of one album, then 🏠. | A list of albums with a heart on each row; the heart fills; **no music change**; a tile with the cover. Touch the tile: the album plays. Touch the heart again in the search: the tile goes. | `Recherche d'albums Qobuz « … » : 200, N album(s)`, `Favori « … » (album Qobuz depuis la recherche) : OK`, `Favori ▶ …` |
| 2.11 | Do 2.7 and 2.10 again; touch **Suivants**. | Each row shows its album cover within about 1 s; only the cover squares change (no flicker); the keyboard and the list stay responsive. | — |
| 2.9 | Only if 2.1 failed: search again. | The music may stop briefly (muted), then the previous music comes back at the same place. | `Avant la recherche…`, `Après la recherche : musique d'avant remise` |

## 3. Deezer (only if Deezer is linked in WiiM Home)

| # | Do | Expected | Journal |
|---|---|---|---|
| 3.1 | Search ⌕ › switch to **Deezer** › type an artist › ↵. | Results; no music change. | `Recherche Deezer « … » : 200, N morceau(x)` |
| 3.2 | Touch a result. | « chargement de l'album… », then the album of this track plays, at this track if possible. | `Deezer ▶ « … » (piste k) : CreateQueue 200, Play 200` |
| 3.3 | Setup page › **Favoris Deezer** › paste your profile number (deezer.com › your profile › number at the end of the address) › save. | After the restart: Deezer album tiles (if you have favorite albums) and a **Favoris Deezer** tile. | `Deezer : albums favoris 200, N` |
| 3.4 | Touch **Favoris Deezer**. | The list of your favorite tracks; no music change before you choose. | `Favoris Deezer : 200, N morceau(x)` |
| 3.4b | In the list of 3.4, touch **Play all**. **Not checked yet on any WiiM.** | Your favorite tracks play from the first one. If nothing plays, note the Journal lines: the WiiM may refuse the playlist address. | `Favoris Deezer : playlist des coups de cœur 200 <n>`, `[favlist.play] deezer=1 … ok=…` |
| 3.5 | Touch a track, then a Deezer album tile. | The album of the track plays at this track; the album tile plays the album. | `Deezer ▶ …`, `Album Deezer ▶ …` |

| 3.6 | In WiiM Home, start Deezer **Flow**. Wait for the next track on the Tab5 (or restart it). Play something else, then touch the **Flow** tile. | A **Flow** tile with the Flow picture appears by itself; the heart is filled while Flow plays. The tile starts Flow again (a new track). Remove it with ♥: it does not come back by itself. | `Flow vu sur le WiiM : tuile « Flow » ajoutée automatiquement : OK`, `Favori ▶ « Flow » : CreateQueue 200, 1 piste(s), PlayQueueWithIndex 200` |

## 4. Favorites saved on the Tab5

| # | Do | Expected | Journal |
|---|---|---|---|
| 4.1 | Play an album (Qobuz) from WiiM Home. On the Tab5 main screen, touch the ♥ button. | The heart fills; a new tile appears. | `Favori « … » (Qobuz, N piste(s), …) : …` |
| 4.2 | Play something else. Touch the new tile. | The saved album plays again. | `Favori ▶ « … » : CreateQueue 200, PlayQueueWithIndex 200` |
| 4.3 | Touch ♥ again while this album plays. | The favorite is removed (tile gone). | `Favori retiré (…)` |
| 4.4 | Hold a finger on a tile for about 1 s. | A menu opens with the tile's image and name; the tile does not start. | `Menu de la tuile : …` |
| 4.5 | In that menu, touch **Pin to the front**. | Back to the main screen; the tile is first. Hold it again: the menu says **Unpin**. | `Tuile épinglée : …` |
| 4.6 | Touch a tile on page 2, then do not touch the screen for 1 min. | The tile does not move at once; after 1 min it is first after the pinned tiles. Restart the Tab5: the order stays. | |
| 4.7 | Hold a favorite tile (small ♥ in its corner) › **Remove from favorites**. | The tile goes away. A Qobuz/Deezer album tile or a WiiM preset has no Remove choice. | `Favori retiré depuis sa tuile (…) : OK` |
| 4.8 | Touch the grid button (2nd in the side bar). Touch each filter; hold a tile, then **Close**; touch a tile. | All the tiles on one screen, same order as the strip; each filter shows only its kind. After Close: back in the Library, same filter. The touched tile plays and the main screen comes back. | `Bibliothèque : N tuile(s)`, `Menu de la tuile : …`, `Preset … : …` |

## 5. Screen and touch

| # | Do | Expected |
|---|---|---|
| 5.1 | Search ⌕ › type fast on the keyboard. | Each key lights at once; no missed letters. |
| 5.2 | Touch ⏮ / ⏭ / ⏯. | A ring turns on the button until the WiiM changes; the progress bar moves every second without flicker. |
| 5.3 | ⚙ › **Test tactile**. Touch the corners. | The circle follows the finger everywhere. |
| 5.5 | Put a finger on the volume bar and slide it left and right. | The knob follows the finger; the WiiM volume follows within about 1 s; Journal: `Volume glissé : N` once, at release. |
| 5.6 | Touch and hold − or + of the volume for 2 s. | After a short delay the volume changes step by step and the button stays lit; Journal: `Volume maintenu : N` at release. |
| 5.7 | Touch − / +, the speaker, ⏯, then a tile and slide off it. | Each button lights at once, without a flicker of the whole screen. After 20 touches, the Journal shows `Réactivité : image … ms après l'appui …`: note the numbers in the results. |
| 5.8 | Pause the music. Wait until the screen dims (10 min without a touch), then touch ⏯. | The light comes back and the music plays with the same touch. While music plays, the ambient view shows instead: a touch there only brings back the controls. When the screen is off (20 min, nothing playing), a touch only turns it on. |
| 5.4 | Navigation bar: 🏠, ⌕, ⚙ from each screen (main, search, results, Journal, menu). | 🏠 always goes to the main screen with the tiles. |

## 6. Update page (firmware `cb22222` and later)

| # | Do | Expected |
|---|---|---|
| 6.1 | ⚙ › **Mise à jour**. Scan the QR code with the phone (phone on the same Wi-Fi, mobile data off). | The update page opens. |
| 6.2 | Choose the `-ota.bin` file on the phone. | The file is not greyed out. Progress on the phone and on the Tab5; restart; new version in the Journal. |

## 7. Microphone and sound (only if you use them)

| # | Do | Expected | Journal |
|---|---|---|---|
| 7.1 | Setup page › **Options** › **Visualiseur** on. Wait for the ambient view (no touch, music playing). | Bars move with the sound in the room. | `Micro (visualiseur) : OK` |
| 7.2 | Setup page › **Options** › **Bips**: try each level, touch some buttons after each restart. | Each level is audible and not too loud. | — |
| 7.3 | Lift or move the Tab5 while the screen is dim. | The screen turns on (if **Réveil au mouvement** is on in the setup page). | — |

## 8. Several WiiMs (only if you have more than one)

| # | Do | Expected |
|---|---|---|
| 8.1 | Setup page › leave the WiiM address empty › save. | After the restart, the Tab5 lists the WiiMs; touch one; it is used and kept after a restart. |

## 9. Sleep, setup mode, flash and reset

Do these last: some restart or erase the Tab5.

| # | Do | Expected | Journal |
|---|---|---|---|
| 9.1 | ⚙ › **Mise en veille**. Then press the power button. | The Tab5 turns off; the power button turns it on. Note if it was on battery or USB (on USB it may not turn off fully). | `Mise en veille (…)` |
| 9.2 | ⚙ › **Configuration**, then touch the Tab5 screen without using the phone. | Setup mode starts after a restart; a touch cancels it and the Tab5 restarts normally with the same settings. | `Configuration : redémarrage en mode configuration` |
| 9.3 | Optional (you lose all settings): setup page › **Avancé** › tick **Tout effacer** › confirm. | « Tout est effacé »; the Tab5 restarts in setup mode with a new Wi-Fi password, no favorites. | — |
| 9.4 | Optional (needs a computer and USB): flash the full image (without `-ota`) at `0x0` with esptool-js, without « Erase flash ». | After the start, the settings are back (Wi-Fi, WiiM, OpenAI key). | `Réglages remis depuis la copie de secours …` |

## Results

Firmware version: `local-……`  Date: `……`  WiiM model / firmware: `……`

| Test | Result | Notes (what you saw, Journal line) |
|---|---|---|
| 0.2 memory at start | | |
| 1.1 taps during search | | |
| 1.2 background transcription | | |
| 1.3 memory after 30 min | | |
| 2.1 Qobuz access | | |
| 2.2–2.3 Qobuz album tiles | | |
| 2.4 play Qobuz album tile | | |
| 2.5–2.6 Favoris Qobuz list | | |
| 2.5c Qobuz Play all / Shuffle | | |
| 2.7–2.8 Qobuz search, no playback before choice | | |
| 2.10 Qobuz album search, heart as tile | | |
| 2.11 covers in result lists | | |
| 3.1–3.2 Deezer search and play | | |
| 3.3–3.5 Deezer favorites (profile) | | |
| 3.4b Deezer Play all (playlist) | | |
| 3.6 Deezer Flow tile | | |
| 4.1–4.3 Tab5 favorites | | |
| 4.4–4.8 tile menu, order, Library | | |
| 5.x screen and touch | | |
| 6.x update page from phone | | |
| 7.x microphone and sound | | |
| 8.1 several WiiMs | | |
| 9.1 sleep | | |
| 9.2 setup mode restart | | |
| 9.3 factory reset | | |
| 9.4 settings after USB flash | | |

## Long test (24 h) and WiiM status prototype — desktop session

A Claude Code session on a computer on the same network can do this part. Copy this prompt into it:

> Goal: a 24-hour test of the wiimscreen firmware, and a prototype of better WiiM status reading. Repo
> github.com/couzinc/wiimscreen, branch `claude/gifted-cannon-ayyn0z` — pull it first; read
> AGENTS.md, docs/WIIM_API_NOTES.md and docs/HARDWARE_TEST.md.
>
> 1. The Tab5 is on USB. Record its serial output (115200 baud, the Journal goes there too) for 24 hours to a file,
>    with a timestamp on each line (for example a small Python script with pyserial). Do not stop it on errors.
> 2. During this time, leave music playing for some hours, then paused, then stopped.
> 3. After 24 h, summarize: every `Mémoire :` line (is the internal heap or the minimum going down?), the worker
>    stack, restarts (`Démarrage`), errors (`WiiM <- … : erreur`, `Relevé du WiiM … erreur`), reconnections,
>    and any crash dump (`Guru Meditation`, `Backtrace`). Decode backtraces with the `firmware.elf` built from
>    the same commit (`pio run -e tab5`, then `riscv32-esp-elf-addr2line` from the PlatformIO toolchain; the
>    ESP32-P4 is RISC-V).
> 4. Prototype, in Python on the computer: (a) call `httpapi.asp?command=getStatusEx` and
>    `httpapi.asp?command=getPlayerStatus` (HTTPS, self-signed: no verification), and the UPnP AVTransport
>    `GetInfoEx` action (control URL in docs/WIIM_API_NOTES.md, section AVTransport); list which one gives the source
>    (Qobuz, Deezer, AirPlay, Spotify, radio…), title, artist, album, cover, position and volume in one call.
>    (b) Subscribe to UPnP events (GENA `SUBSCRIBE` on AVTransport and RenderingControl event URLs) and log what
>    arrives when you play, pause, skip and change the volume in WiiM Home: delay, content, how often.
> 5. Write the results in docs/WIIM_API_NOTES.md (new section "Status: GetInfoEx and UPnP events") and a short
>    report in docs/SOAK_TEST.md (no logs over 200 lines; keep only the useful lines). Commit and push to
>    `claude/gifted-cannon-ayyn0z`.
>
> Never publish the WiiM IP address, tokens, the airable sub-domain number or account names. Do not change the
> firmware in this task.
