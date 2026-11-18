#!/usr/bin/env python3
"""Golden screens: render every preview screen and compare it with the committed reference hashes.

A change in src/ui/ui.cpp (or in lib/core layout code) that changes even one pixel of a screen makes this check fail.
That lets a refactor prove "nothing changed", and makes every visual change visible in the pull request
(the manifest diff names the screens that changed).

Usage (after `pio run -e preview`; needs SDL2 like the preview, Python standard library only):
    python tools/preview/check_golden.py            # render + compare with tools/preview/golden.sha256
    python tools/preview/check_golden.py --update   # render + rewrite the manifest (intended visual change)
Options:
    --out DIR       output folder (default .pio/golden-out; emptied of old .ppm/.png first)
    --program PATH  preview program (default .pio/build/preview/program)
    --no-render     only hash the .ppm files already in --out

Each configuration is rendered into its own folder: <lang>-<size>/<screen>.ppm, for
English and French at 1280x720 (and its portrait 720x1280) and 800x480 (and 480x800).
The input images are the committed files in tools/preview/images/ (tools/preview/make_images.py made them):
the check does not depend on the Pillow or libjpeg version of the machine.
"""
import argparse
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MANIFEST = os.path.join(HERE, "golden.sha256")
IMAGES = os.path.join(HERE, "images")
LANGS = ("en", "fr")
SIZES = ("1280x720", "800x480")


def rel(path):
    return os.path.relpath(path, ROOT)


def render(program, out):
    for lang in LANGS:
        for size in SIZES:
            d = os.path.join(out, f"{lang}-{size}")
            os.makedirs(d, exist_ok=True)
            for name in os.listdir(d):  # old screens would hide a screen that is no longer drawn
                if name.endswith((".ppm", ".png")):
                    os.remove(os.path.join(d, name))
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", PREVIEW_LANG=lang, PREVIEW_SIZE=size)
            r = subprocess.run([program, IMAGES, d], env=env, stdout=subprocess.DEVNULL)
            if r.returncode != 0:
                sys.exit(f"check_golden: the preview failed ({lang}, {size}), exit code {r.returncode}")


def hashes(out):
    result = {}
    for dirpath, _, files in os.walk(out):
        for name in files:
            if name.endswith(".ppm"):
                path = os.path.join(dirpath, name)
                with open(path, "rb") as f:
                    key = os.path.relpath(path, out).replace(os.sep, "/")
                    result[key] = hashlib.sha256(f.read()).hexdigest()
    return result


def read_manifest():
    expected = {}
    if not os.path.exists(MANIFEST):
        return expected
    with open(MANIFEST, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                digest, name = line.split(None, 1)
                expected[name.lstrip("*")] = digest
    return expected


def write_manifest(actual):
    # Same format as sha256sum: "cd .pio/golden-out && sha256sum -c ../../tools/preview/golden.sha256" also works
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        for name in sorted(actual):
            f.write(f"{actual[name]}  {name}\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=os.path.join(ROOT, ".pio", "golden-out"))
    ap.add_argument("--program", default=os.path.join(ROOT, ".pio", "build", "preview", "program"))
    ap.add_argument("--update", action="store_true", help="rewrite the manifest with the rendered screens")
    ap.add_argument("--no-render", action="store_true", help="only hash the screens already in --out")
    args = ap.parse_args()

    if not args.no_render:
        if not os.path.exists(args.program):
            sys.exit(f"check_golden: {rel(args.program)} not found: run `pio run -e preview` first")
        render(args.program, args.out)
    actual = hashes(args.out)
    if not actual:
        sys.exit(f"check_golden: no .ppm screen in {args.out}")

    if args.update:
        write_manifest(actual)
        print(f"check_golden: {rel(MANIFEST)} updated ({len(actual)} screens). Commit it with your change.")
        return 0

    expected = read_manifest()
    changed = sorted(n for n in actual if n in expected and actual[n] != expected[n])
    new = sorted(n for n in actual if n not in expected)
    missing = sorted(n for n in expected if n not in actual)
    if not (changed or new or missing):
        print(f"check_golden: OK, {len(actual)} screens identical to {rel(MANIFEST)}")
        return 0

    for title, names in (("changed", changed), ("new (not in the manifest)", new), ("missing (not drawn)", missing)):
        if names:
            print(f"\nScreens {title}: {len(names)}")
            for n in names:
                print(f"  {n}")
    out = rel(args.out) if os.path.abspath(args.out).startswith(ROOT + os.sep) else args.out
    print(f"""
check_golden: FAILED. The rendered screens are not the reference screens.
What to do:
  1. Look at the screens:  python tools/preview/topng.py {out}/*/*.ppm   (needs Pillow)
     then open the PNGs in {out}/ (on GitHub: the "golden-screens" artifact of the failed run).
  2. If the change is NOT intended: fix the code; the check must pass without --update.
  3. If the change IS intended: run
       python tools/preview/check_golden.py --update
     and commit tools/preview/golden.sha256 with your change. Say in the pull request which screens changed
     and attach PNGs of them.""")
    return 1


if __name__ == "__main__":
    sys.exit(main())
