"""Turns the reference art (ref/hunold.png) into the pet's base sprite (base.png).

    python trace.py

The reference is pixel art blown up to ~6.6 screen pixels per art pixel (and saved as JPEG), so
each art pixel is read back from the middle of its cell (the median of a 4x4 patch, away from
the noisy edges). That gives ref/native.png, 127 x 190 art pixels. The upper body, from the
antenna tips down to the top of the vest, is cut out to the 135 x 82 pet canvas, the colours are
cut down to a small palette and the white background becomes see-through.
"""
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
REF = HERE.parent / "ref"
CELL_X, CELL_Y = 6.59, 6.62      # the reference's art-pixel size, measured from its edges
PHASE_X, PHASE_Y = 5.5, 2.9      # where its first cell starts
W, H = 135, 82                   # pet canvas: 1x in the 135x82 slots, 1.5x on Home
OX, OY = 10, -13                 # art pixel (x, y) lands at canvas (x + OX, y + OY)
COLORS = 64
DARKEST = (24, 20, 24)           # pure black is the firmware's see-through key: never use it in the art


def native(im):
    cols, rows = int((im.width - PHASE_X) / CELL_X), int((im.height - PHASE_Y) / CELL_Y)
    out = Image.new("RGB", (cols, rows))
    for j in range(rows):
        for i in range(cols):
            x, y = PHASE_X + (i + .5) * CELL_X, PHASE_Y + (j + .5) * CELL_Y
            patch = im.crop((int(x - 1.5), int(y - 1.5), int(x + 2.5), int(y + 2.5)))
            px = sorted(patch.get_flattened_data(), key=sum)
            out.putpixel((i, j), px[len(px) // 2])
    return out


def is_white(p):
    return min(p) > 205 and max(p) - min(p) < 40


def main():
    nat = native(Image.open(REF / "hunold.png").convert("RGB"))
    nat.save(REF / "native.png")
    canvas = Image.new("RGB", (W, H), (255, 255, 255))
    canvas.paste(nat, (OX, OY))
    q = canvas.quantize(COLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).convert("RGB")
    base = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    # background: white reachable from the edges, plus the white pockets between hand and vest
    bg = set()
    todo = [(x, y) for x in range(W) for y in (0, H - 1)] + [(x, y) for y in range(H) for x in (0, W - 1)]
    while todo:
        x, y = todo.pop()
        if (x, y) in bg or not (0 <= x < W and 0 <= y < H) or not is_white(q.getpixel((x, y))):
            continue
        bg.add((x, y))
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    for y in range(H):
        for x in range(W):
            p = q.getpixel((x, y))
            face = 44 + OX <= x <= 88 + OX and y <= 76 + OY
            if (x, y) in bg or (is_white(p) and not face):
                continue
            if sum(p) < sum(DARKEST) + 30:
                p = DARKEST
            base.putpixel((x, y), p + (255,))
    base.save(HERE / "base.png")
    print("base.png", base.size, len({c for c in base.get_flattened_data() if c[3]}), "colours")


if __name__ == "__main__":
    main()
