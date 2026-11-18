#!/usr/bin/env python3
"""Input images for the preview screenshots in docs/: an album cover and six preset logos (no real artwork).

The output is committed in tools/preview/images/ and the preview and the golden-screens check use those files, so
that the screens do not depend on the Pillow/libjpeg version or the fonts of the machine. Regenerate them only on
purpose: every golden hash changes (python tools/preview/check_golden.py --update), and so do the docs/*.png.

Usage:
    python tools/preview/make_images.py tools/preview/images/
    SDL_VIDEODRIVER=dummy .pio/build/preview/program tools/preview/images/ out/ && python tools/preview/topng.py out/*.ppm
Needs Pillow. Uses DejaVu Sans Bold when present, else Pillow's default font.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont


def font(size):
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return ImageFont.load_default()


def cover(path):
    # Vinyl on a blue gradient, white stripes at the bottom
    s = 600
    im = Image.new("RGB", (s, s))
    d = ImageDraw.Draw(im)
    for y in range(s):
        t = y / s
        d.line([(0, y), (s, y)], fill=(int(30 + 30 * t), int(90 + 40 * t), int(190 - 20 * t)))
    for x in range(-s, s, 34):
        d.polygon([(x, s), (x + 14, s), (x + 14 + 90, s - 90), (x + 90, s - 90)], fill=(235, 240, 250))
    d.rectangle([0, 0, s, s - 90], fill=None)
    c, r = s // 2, 200
    d.ellipse([c - r, c - r - 40, c + r, c + r - 40], fill=(242, 196, 92))
    d.ellipse([c - 120, c - 160, c + 120, c + 80], fill=(18, 20, 28))
    d.ellipse([c - 18, c - 58, c + 18, c - 22], fill=(242, 196, 92))
    im.save(path, quality=92)


def logo(path, letter, bg):
    s = 300
    im = Image.new("RGB", (s, s), bg)
    d = ImageDraw.Draw(im)
    m = 46
    d.ellipse([m, m, s - m, s - m], outline=(255, 255, 255), width=14)
    f = font(120)
    w, h = d.textbbox((0, 0), letter, font=f)[2:]
    d.text(((s - w) / 2, (s - h) / 2 - 12), letter, font=f, fill=(255, 255, 255))
    im.save(path)


def main(out):
    os.makedirs(out, exist_ok=True)
    cover(os.path.join(out, "cover.jpg"))
    presets = [("F", (226, 52, 72)), ("J", (40, 40, 44)), ("I", (30, 110, 220)), ("3", (240, 190, 40)),
               ("P", (40, 170, 90)), ("N", (110, 50, 200))]
    for i, (letter, bg) in enumerate(presets, 1):
        logo(os.path.join(out, f"preset{i}.png"), letter, bg)
    print(f"cover.jpg and preset1..6.png in {out}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
