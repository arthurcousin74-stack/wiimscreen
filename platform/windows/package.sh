#!/usr/bin/env bash
# Makes the Windows download: dist/wiimscreen-windows-x64/ (wiimscreen.exe, cacert.pem, README.txt, LICENSE.txt)
# and dist/wiimscreen-windows-x64.zip, from the build of `pio run -e windows`.
# cacert.pem = certs/cacrt_all.pem (Mozilla roots, also the source of the Tab5's bundle) + certs/extra_roots.pem.
# Runs in MSYS2 (CI) or on Linux with WINDOWS_CROSS set (strip of the cross toolchain). Needs python3 (zip).
set -euo pipefail
cd "$(dirname "$0")/../.."
EXE=.pio/build/windows/wiimscreen.exe
OUT=dist/wiimscreen-windows-x64
[ -f "$EXE" ] || { echo "package.sh: $EXE missing (pio run -e windows)"; exit 1; }
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT"
"${WINDOWS_CROSS:-}strip" -o "$OUT/wiimscreen.exe" "$EXE"
cat certs/cacrt_all.pem certs/extra_roots.pem > "$OUT/cacert.pem"
cp platform/windows/README.txt "$OUT/README.txt"
cp LICENSE "$OUT/LICENSE.txt"
python3 -m zipfile -c "$OUT.zip" "$OUT"
ls -l "$OUT" "$OUT.zip"
