#!/usr/bin/env python3
"""Convertit les captures PPM de tools/preview en PNG."""
import sys

from PIL import Image

for path in sys.argv[1:]:
    out = path.rsplit(".", 1)[0] + ".png"
    Image.open(path).convert("RGB").save(out)
    print(out)
