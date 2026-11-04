#!/usr/bin/env python3
"""Convertit les captures PPM de tools/preview en PNG, avec le rendu e-ink (16 niveaux de gris)."""
import sys

from PIL import Image

for path in sys.argv[1:]:
    img = Image.open(path).convert("L")
    img = img.point(lambda v: min(255, round(v / 17) * 17))
    out = path.rsplit(".", 1)[0] + ".png"
    img.save(out)
    print(out)
