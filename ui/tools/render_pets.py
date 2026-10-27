#!/usr/bin/env python3
"""
Render the reference firmware's ASCII pets to PNG for the EEZ design preview.

For each species in reference/claude-desktop-buddy/src/buddies/*.cpp we take a
named pose from a named state function (default: the first idle pose), draw it
with the classic 5x7 GLCD font (6x8 cell) in the species' own color, trim the
empty margin and save it with a transparent background.

On the device the pets are drawn live by the firmware; these PNGs only make the
designer's preview and the character picker look real.

Usage:  python ui/tools/render_pets.py
"""
import re
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
BUDDIES = ROOT / "reference/claude-desktop-buddy/src/buddies"
GLCD = ROOT / "ui/assets/fonts/glcdfont.c"
OUT = ROOT / "ui/assets/images"

# Pet slots: the firmware draws the live pet into a 135x82 canvas (the original buddy
# canvas, 1x = 6x8 px per character) scaled by a factor, transparent around the art.
# These previews put the first pose at the same place so EEZ shows what the device shows.
# (output name, species, state function, pose index, scale)  — scales must match pet_view.cpp
SLOTS = [
    ("pv_home", "cat", "doIdle", 0, 1.5),
    ("pv_approval", "cat", "doAttention", 0, 1.0),
    ("pv_pet", "cat", "doHeart", 0, 1.0),
    ("pv_clock", "cat", "doSleep", 0, 1.0),
]
CANVAS_W, CANVAS_H, X_CENTER, Y_BASE = 135, 82, 67, 30
THUMBS = ["cat", "capybara", "duck", "ghost", "robot", "axolotl", "dragon", "penguin", "blob"]


def load_font():
    body = GLCD.read_text()
    body = body[body.index("font[]"):]
    return [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", body[body.index("{"):body.index("};")])]


def c_strings(block):
    return [bytes(s, "utf-8").decode("unicode_escape") for s in re.findall(r'"((?:[^"\\]|\\.)*)"', block)]


def pose(species, fn, idx):
    src = (BUDDIES / f"{species}.cpp").read_text(encoding="utf-8")
    body = src[src.index(f"static void {fn}("):]
    body = body[:body.index("\n}\n")]
    arrays = re.findall(r"static const char\* const \w+\[\d+\]\s*=\s*\{(.*?)\};", body, re.S)
    lines = c_strings(arrays[idx])
    m = re.search(r"buddyPrintSprite\([^,]+,\s*\d+,\s*[^,]+,\s*(0x[0-9A-Fa-f]+)", body)
    c565 = int(m.group(1), 16) if m else 0xFFFF
    rgb = (((c565 >> 11) & 31) * 255 // 31, ((c565 >> 5) & 63) * 255 // 63, (c565 & 31) * 255 // 31)
    return lines, rgb


def render(lines, rgb, scale, font):
    w, h = max(len(l) for l in lines) * 6, len(lines) * 8
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for row, line in enumerate(lines):
        for col, ch in enumerate(line):
            c = ord(ch)
            for x in range(5):
                bits = font[c * 5 + x]
                for y in range(8):
                    if bits >> y & 1:
                        px[col * 6 + x, row * 8 + y] = rgb + (255,)
    bbox = img.getbbox()
    img = img.crop(bbox) if bbox else img
    return img.resize((img.width * scale, img.height * scale), Image.NEAREST)


def render_slot(lines, rgb, scale, font):
    """Same placement as buddyPrintSprite at 1x (lines centered on X_CENTER from Y_BASE), then scaled."""
    img = Image.new("RGBA", (CANVAS_W, CANVAS_H), (0, 0, 0, 0))
    px = img.load()
    for row, line in enumerate(lines):
        x0 = X_CENTER - len(line) * 6 // 2
        y0 = Y_BASE + row * 8
        for col, ch in enumerate(line):
            for x in range(5):
                bits = font[ord(ch) * 5 + x]
                for y in range(8):
                    X, Y = x0 + col * 6 + x, y0 + y
                    if bits >> y & 1 and 0 <= X < CANVAS_W and 0 <= Y < CANVAS_H:
                        px[X, Y] = rgb + (255,)
    return img.resize((int(CANVAS_W * scale), int(CANVAS_H * scale)), Image.NEAREST)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("pet_cat_*.png"):
        old.unlink()   # superseded by the pv_* slot previews
    font = load_font()
    for name, species, fn, idx, scale in SLOTS:
        lines, rgb = pose(species, fn, idx)
        img = render_slot(lines, rgb, scale, font)
        img.save(OUT / f"{name}.png")
        print(f"{name}.png {img.size}")
    # GIF pack preview: first frame of DevelopKing's idle animation (this project's own art), 64px tall.
    gif = ROOT / "characters/developking/pack/idle.gif"
    if gif.exists():
        fr = Image.open(gif).convert("RGBA")
        fr = fr.crop(fr.getbbox())
        fr.resize((fr.width * 64 // fr.height, 64), Image.NEAREST).save(OUT / "thumb_developking.png")
        print("thumb_developking.png")
    for species in THUMBS:
        lines, rgb = pose(species, "doIdle", 0)
        img = render(lines, rgb, 1, font)
        img.save(OUT / f"thumb_{species}.png")
        print(f"thumb_{species}.png {img.size}")


if __name__ == "__main__":
    main()
