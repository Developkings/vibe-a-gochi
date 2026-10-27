"""Concept sheet of the DevelopKing buddy's base pose (the parts live in parts.py).

  python characters/developking/src/draw.py characters/developking/concept.png
"""
import sys

from PIL import Image

from parts import H, W, compose

if __name__ == "__main__":
    out = sys.argv[1]
    im = compose(lens="rest")
    im.save(out.replace(".png", "_1x.png"))
    sheet = Image.new("RGB", (W * 6 + W * 2 + 60, H * 6 + 40), (26, 22, 20))
    big = im.resize((W * 6, H * 6), Image.NEAREST)
    sheet.paste(big, (20, 20), big)
    small = im.resize((W * 2, H * 2), Image.NEAREST)  # its size on the home screen
    sheet.paste(small, (W * 6 + 40, 20 + H * 2), small)
    sheet.save(out)
