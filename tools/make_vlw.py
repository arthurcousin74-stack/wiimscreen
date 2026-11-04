#!/usr/bin/env python3
"""Génère une police lissée VLW (format Processing / TFT_eSPI / M5GFX) depuis un TTF.

Format (entiers 32 bits big-endian) :
  en-tête : nb_glyphes, version (11), taille, 0, ascent, descent
  par glyphe (7 entiers) : unicode, hauteur, largeur, avance, dY (au-dessus de la
                           ligne de base), dX (décalage gauche), 0
  puis les bitmaps 8 bits (alpha), dans l'ordre des glyphes.

Usage : make_vlw.py police.ttf taille nom_symbole sortie.h [--preview texte sortie.png]
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

# ASCII + Latin-1 + Latin étendu A + ponctuation typographique courante
CHARS = (
    [chr(c) for c in range(0x20, 0x7F)]
    + [chr(c) for c in range(0xA0, 0x180)]
    + list("‘’‚“”„–—•…€™−")
)


def build(ttf: str, size: int) -> bytes:
    font = ImageFont.truetype(ttf, size)
    ascent, descent = font.getmetrics()
    metrics, bitmaps = [], []
    for ch in sorted(set(CHARS), key=ord):
        adv = round(font.getlength(ch))
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        if ch == " " or w == 0 or h == 0:
            metrics.append((ord(ch), 0, 0, adv, 0, 0, 0))
            bitmaps.append(b"")
            continue
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
        metrics.append((ord(ch), h, w, adv, -y0, x0, 0))
        bitmaps.append(img.tobytes())
    out = struct.pack(">6i", len(metrics), 11, size, 0, ascent, descent)
    for m in metrics:
        out += struct.pack(">7i", *m)
    return out + b"".join(bitmaps)


def parse(data: bytes):
    n, _ver, size, _, ascent, descent = struct.unpack_from(">6i", data, 0)
    off = 24
    glyphs = {}
    metrics = [struct.unpack_from(">7i", data, off + 28 * i) for i in range(n)]
    off += 28 * n
    for code, h, w, adv, dy, dx, _ in metrics:
        glyphs[chr(code)] = (h, w, adv, dy, dx, data[off : off + w * h])
        off += w * h
    assert off == len(data), "taille incohérente"
    return size, ascent, descent, glyphs


def preview(data: bytes, text: str, png: str):
    size, ascent, descent, glyphs = parse(data)
    width = sum(glyphs.get(c, glyphs["?"])[2] for c in text) + 20
    img = Image.new("L", (width, ascent + descent + 20), 255)
    x, base = 10, 10 + ascent
    for c in text:
        h, w, adv, dy, dx, bm = glyphs.get(c, glyphs["?"])
        if w and h:
            g = Image.frombytes("L", (w, h), bm)
            img.paste(0, (x + dx, base - dy), g)
        x += adv
    img.save(png)


def to_header(data: bytes, symbol: str, src: str, size: int) -> str:
    lines = [
        f"// Généré par tools/make_vlw.py depuis {src} ({size} px) : ne pas modifier.",
        "// Police DejaVu Sans, licence libre (voir fonts/LICENSE-DejaVu.txt).",
        "#pragma once",
        "#include <stdint.h>",
        f"static const uint8_t {symbol}[{len(data)}] = {{",
    ]
    for i in range(0, len(data), 24):
        lines.append("  " + ",".join(f"0x{b:02x}" for b in data[i : i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    ttf, size, symbol, out = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    data = build(ttf, size)
    parse(data)  # auto-vérification du format
    with open(out, "w", encoding="utf-8") as f:
        f.write(to_header(data, symbol, ttf.split("/")[-1], size))
    if "--preview" in sys.argv:
        i = sys.argv.index("--preview")
        preview(data, sys.argv[i + 1], sys.argv[i + 2])
    print(f"{out} : {len(CHARS)} glyphes, {len(data)} octets")
