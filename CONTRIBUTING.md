# Contributing

Thanks for helping! Bug reports, hardware test results, translations, ports to other ESP32 boards and UI ideas are all
welcome. Only the M5Stack Tab5 has been tested so far, so reports from real hardware are especially useful.

## Support

Not ready to contribute code? A star on GitHub already helps: it makes the project easier to find for other WiiM
owners and developers.

## Reporting a problem

Open an issue with:

- your **board** (only the M5Stack Tab5 is tested so far),
- your **WiiM model** and its firmware version (WiiM Home app),
- the **wiimscreen version** (bottom of the *Réglages* menu: ⚙ button in the left bar),
- what you did, what you expected, what happened — a photo of the screen helps,
- if possible, the serial log (`pio device monitor`, 115200 baud).

## Making a change

1. Read [AGENTS.md](AGENTS.md): commands, layout, conventions and known pitfalls.
2. Put hardware-independent logic in `lib/core/` and add a test in `test/test_core/test_main.cpp`.
3. Run `pio test -e native` and `pio run -e tab5`. For UI changes, render with `tools/preview` and attach the PNGs to
   your pull request.
4. Say in the pull request what you verified **on real hardware** and what you did not.

Keep pull requests focused: one feature or fix at a time.

## Porting to another board

See [Other ESP32 boards](README.md#other-esp32-boards) in the README. Add a `[env:<board>]` in `platformio.ini`,
keep board differences small and isolated (pins, audio bus, panel), add the screen size to the `checkLayout` tests,
and include photos of the running screens in your pull request.

## Translations

UI strings are currently French and live in `src/ui.cpp`, `src/main.cpp`, `src/portal.cpp`, `src/ota.cpp` and the voice
phrases in `lib/core/src/voice.cpp`. A small string table to support several languages would be a welcome first step.

## Code of conduct

Be kind and constructive. This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md).
