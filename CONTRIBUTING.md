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
- the **wiimscreen version** (bottom of the *Settings* menu, *Réglages* in French: ⚙ button in the left bar),
- what you did, what you expected, what happened — a photo of the screen helps,
- if possible, the serial log (`pio device monitor`, 115200 baud).

## Making a change

1. Read [AGENTS.md](AGENTS.md): commands, layout, conventions and known pitfalls. For the most common changes (a
   setting, a screen, a text), follow the checklists in [docs/DEVELOPERS.md](docs/DEVELOPERS.md#how-to-add).
2. Put hardware-independent logic in `lib/core/` and add a test in the matching `test/test_core/test_<module>.cpp`.
3. Run the checks below (`tools/check.sh` runs them all). For UI changes, attach PNGs of the changed screens to
   your pull request.
4. Say in the pull request what you verified **on real hardware** and what you did not.

Keep pull requests focused: one feature or fix at a time. Code comments: existing files are commented in French
(the original author's language), keep each file consistent; new files and new code may be commented in English.

## Checks (the same as CI)

CI ([.github/workflows/firmware.yml](.github/workflows/firmware.yml)) runs these gates, in this order, on every push
and pull request. Each CI step calls `tools/check.sh <gate>`: the commands are the same on your computer.

```bash
pip install platformio "click<8.2"   # once; click >= 8.2 breaks the platform's esptool
tools/check.sh                       # every gate, in CI order, then a summary
tools/check.sh native preview        # only some gates
```

| Gate | What it checks | Command run by `tools/check.sh` | Needs (Debian/Ubuntu packages) |
|---|---|---|---|
| `native` | Unit tests of `lib/core`, under AddressSanitizer + UBSan | `pio test -e native` | a C++ compiler |
| `sim` | The real app with a fake WiiM: scripted tour of the screens, a tap on every element of every screen (`taps.txt`), scripted keys and D-pad (`keys.txt`, `dpad.txt`); fails on a crash or a hang | `pio run -e sim` then `.pio/build/sim/program --headless --quiet --reset --script tools/sim/scripts/tour.txt` | `libsdl2-dev` |
| `preview` | Preview build and **golden screens**: every screen, English and French, 1280×720 and 800×480, compared with `tools/preview/golden.sha256` | `pio run -e preview` then `python3 tools/preview/check_golden.py` | `libsdl2-dev`, `python3` |
| `asan` | The simulator tour, taps and D-pad under AddressSanitizer/UBSan (separate build folder `.pio/build-asan`) | see `gate_asan` in `tools/check.sh` | `libsdl2-dev` |
| `simreal` | The real network code through the relay to a mock WiiM (HTTPS, certificate pinning, search through queues). About 2 minutes | `pio run -e simreal` then `./tools/relay/selftest.sh` | `libsdl2-dev`, `openssl`, `python3` |
| `desktop` | The computer build, directly against a mock WiiM | `pio run -e desktop` then `./platform/pc/selftest.sh` | `libsdl2-dev`, `libssl-dev`, `openssl`, `python3` |
| `tab5` | The Tab5 firmware | `pio run -e tab5` | nothing (PlatformIO downloads the toolchain) |

On macOS: `brew install sdl2` (and `openssl` for the `desktop` gate).

- A gate whose prerequisites are missing is **skipped** with a message; the summary lists it as `SKIP`. A skip is not
  a pass: CI still runs that gate.
- `CHECK_SKIP_RELAY=1 tools/check.sh` builds `simreal` but skips the relay self-test (it uses the fixed local ports
  4443, 49152 and 8799; use this when another program uses them).
- **Warnings**: in CI, a compiler warning in the project's own files (`src/`, `lib/core`, `test/`, `tools/sim`,
  `tools/preview`, `platform/pc`) fails the PC builds. Locally it is only a warning, so a newer compiler does not
  block you. To check like CI: `CHECK_WERROR=1 tools/check.sh`. The Tab5 firmware is not built with `-Werror`
  (headers of the ESP-IDF framework warn).

### Golden screens

`tools/preview/check_golden.py` draws every preview screen with the real `src/ui/ui.cpp` and compares each image, byte
for byte, with `tools/preview/golden.sha256`. The input images are in `tools/preview/images/` (made by
`tools/preview/make_images.py`; do not regenerate them without a reason, every hash would change).

- **A refactor** must pass the check without any change to the manifest: this proves that no pixel changed.
- **A visual change** fails the check, which lists the screens that changed. Look at them
  (`python3 tools/preview/topng.py .pio/golden-out/*/*.ppm`, needs Pillow; on GitHub: the `golden-screens` artifact
  of the failed run). If the change is intended, run `python3 tools/preview/check_golden.py --update` and commit
  `tools/preview/golden.sha256` with your change. Say in the pull request which screens changed and attach them.
- A new screen: draw it in `tools/preview/preview.cpp`, then run `--update`.

## When is a pull request done?

- [ ] `tools/check.sh` ends with `check.sh: OK`, and CI is green (CI also runs the gates you skipped).
- [ ] New logic in `lib/core` has a unit test.
- [ ] UI changes: PNGs of the changed screens attached; `tools/preview/golden.sha256` updated only for intended changes.
- [ ] Every new user-visible text is `wr::L("English", "Français")`; Journal lines (`logEvent`) stay in French.
- [ ] README.md and LISEZMOI.md updated if the user sees the change (README.md is in Simplified Technical English).
- [ ] The pull request says what was verified on real hardware, and what was not.

### What CI cannot check

CI has no Tab5 and no WiiM. The simulator, the mock WiiM and the relay cover the app logic and the network protocol,
not the hardware: touch, Wi-Fi through the ESP32-C6, microphones, speaker, battery, sleep, firmware update over
Wi-Fi, and the behaviour of a real WiiM and of the Qobuz/Deezer services. Test these on a Tab5 with
[docs/HARDWARE_TEST.md](docs/HARDWARE_TEST.md), or say in the pull request that you could not. `simreal` with the
relay (see [tools/sim/README.md](tools/sim/README.md)) lets you test against your real WiiM without a Tab5.

## Porting to another board

See [Other ESP32 boards](docs/DEVELOPERS.md#other-esp32-boards) in docs/DEVELOPERS.md. Add a `[env:<board>]` in `platformio.ini`,
keep board differences small and isolated (pins, audio bus, panel), add the screen size to the `checkLayout` tests,
and include photos of the running screens in your pull request.

## Translations

UI strings are in English and French, side by side in the code: `wr::L("English", "Français")` (`lib/core/src/i18n.h`).
A third language would need a string table instead of the two-argument helper; the Journal stays in French.

## Code of conduct

Be kind and constructive. This project follows the [Contributor Covenant](CODE_OF_CONDUCT.md).
