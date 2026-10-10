#!/usr/bin/env python3
"""Writes font5x8.json: the 5x8 pixel glyphs the simulator draws text with.

They come from the X11 "5x8" bitmap font (public domain, in xfonts-base).
A real HD44780 has its own character ROM, so letter shapes differ in detail;
the cell size, 5 by 8 dots, is the same. Needs Pillow.
"""
import gzip
import json
import os
import sys

from PIL import PcfFontFile

SOURCE = "/usr/share/fonts/X11/misc/5x8.pcf.gz"

font = PcfFontFile.PcfFontFile(gzip.open(sys.argv[1] if len(sys.argv) > 1 else SOURCE))
glyphs = {}
for code in range(32, 127):
    image = font.glyph[code][3]
    glyphs[str(code)] = [
        sum((1 << (4 - x)) for x in range(5) if image.getpixel((x, y))) for y in range(8)
    ]
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "font5x8.json")
with open(out, "w") as f:
    json.dump(glyphs, f, separators=(",", ":"))
    f.write("\n")
