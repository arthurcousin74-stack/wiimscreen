## What does this change?

## How was it checked?

<!-- The same gates as CI, details in CONTRIBUTING.md › Checks. `tools/check.sh` runs them all; paste its summary. -->

- [ ] `tools/check.sh` ends with `check.sh: OK` (or list the gates it skipped: CI runs them)
  - `native`: `pio test -e native` (unit tests, sanitizers)
  - `sim`: simulator tour (`pio run -e sim` + `tools/sim/scripts/tour.txt`)
  - `preview`: `pio run -e preview` + `python3 tools/preview/check_golden.py` (golden screens)
  - `asan`: simulator tour under AddressSanitizer/UBSan
  - `simreal`: `pio run -e simreal` + `./tools/relay/selftest.sh` (needs openssl, about 2 min)
  - `desktop`: `pio run -e desktop` + `./platform/pc/selftest.sh` (needs libssl-dev)
  - `tab5`: `pio run -e tab5`
- [ ] New logic in `lib/core` has a unit test
- [ ] UI changes: PNGs of the changed screens attached; `tools/preview/golden.sha256` updated (`--update`) only for intended changes — which screens changed:
- [ ] New UI texts are `wr::L("English", "Français")`; Journal lines stay in French
- [ ] README.md / LISEZMOI.md updated if users see the change
- [ ] Verified on real hardware (say which: Tab5 + WiiM model, and which items of docs/HARDWARE_TEST.md) — or say it was not
