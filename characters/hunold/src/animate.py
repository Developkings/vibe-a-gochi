"""Build the Hunold Holzwurm GIF pack: one GIF per buddy state plus manifest.json.

  python characters/hunold/src/animate.py

Writes characters/hunold/pack/ (copy that folder to the device: SD card or the Claude app's
folder push) and characters/hunold/preview/ (enlarged GIFs and a contact sheet).
"""
import json
import math
import random
from pathlib import Path

from PIL import Image

from parts import H, P, W, Frame, compose

ROOT = Path(__file__).resolve().parent.parent
PACK = ROOT / "pack"
PREVIEW = ROOT / "preview"
BG = (0, 0, 0)        # keyed out by the firmware (flood fill from the edges)

GLYPHS = {
    "Z": ["111", "..1", ".1.", "1..", "111"],
    "z": ["11", ".1", "1.", "11"],
    "!": ["11", "11", "11", "11", "11", "..", "11", "11"],
    "?": ["111", "..1", ".11", "...", ".1."],
    "<": ["..1", ".1.", "1..", ".1.", "..1"],
    "/": ["..1", "..1", ".1.", "1..", "1.."],
    ">": ["1..", ".1.", "..1", ".1.", "1.."],
    ")": ["1.", ".1", ".1", ".1", "1."],
}
HEART_S = [".1.1.", "11111", ".111.", "..1.."]
HEART_M = [".11.11.", "1211111", "1111111", ".11111.", "..111..", "...1..."]
STAR = ["..1..", ".111.", "11111", ".111.", ".1.1."]
SPARK = [".1.", "111", ".1."]
CURL = [".11.", "1..1", "..1.", ".1.."]      # a wood shaving
HEAD_TOP, FACE_X = 19, 75                  # where Hunold's head starts, and its middle


class FX(Frame):
    def glyph(self, s, x, y, c, outline=True):
        """Text with a 1-pixel dark outline so it reads on any background."""
        cells = []
        cx = x
        for ch in s:
            rows = GLYPHS[ch]
            cells += [(cx + i, y + j) for j, r in enumerate(rows) for i, v in enumerate(r) if v == "1"]
            cx += len(rows[0]) + 1
        if outline:
            for a, b in cells:
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        self.px(a + dx, b + dy, "ol")
        for a, b in cells:
            self.px(a, b, c)

    def shape(self, rows, x, y, c):
        self.sprite(rows, x, y, {"1": c, "2": "heart_h"}, outline="ol")


def frame(fx_back=None, fx_front=None, **pose):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if fx_back:
        img.alpha_composite(fx_back.im)
    img.alpha_composite(compose(**pose))
    if fx_front:
        img.alpha_composite(fx_front.im)
    return img


def breath(i, period=8):
    """0 for the first half of the period, 1 (sunk by a pixel) for the second."""
    return 1 if (i % period) >= period // 2 else 0


def sway(i, period, amp=1.5, droop=0):
    """Antennae swaying in turn: offsets for the left and right tip."""
    a = math.sin(2 * math.pi * i / period) * amp
    b = math.sin(2 * math.pi * i / period + 1.3) * amp
    return (round(a), droop), (round(b), droop)


# ------------------------------------------------------------------ states

def idle(n=24):
    """The reference pose: finger up, a grin, breathing, antennae swaying, a blink."""
    out = []
    for i in range(n):
        blink = i in (9, 21)
        out.append((frame(dy=breath(i), sway=sway(i, 12), eyes_kind="closed" if blink else "open",
                          mouth_kind="smile" if 12 <= i < 18 else "grin"), 140 if blink else 180))
    return out


def idle_idea():
    """A bright idea: the antenna knobs light up, he wags the finger, a spark pops."""
    out = []
    for i in range(16):
        fx = FX()
        lit = i >= 4
        if 4 <= i < 12:
            sx, sy = (40, 18) if i % 2 else (106, 18)
            fx.shape(SPARK, sx, sy, "bulb_h")
            fx.shape(SPARK, 150 - sx - 4, sy + 6, "gold")
        if 6 <= i < 14:
            fx.glyph("!", 108, 26 - (i % 2), "gold")
        out.append((frame(fx_front=fx, dy=breath(i, 4), sway=sway(i, 4, 1, -1 if lit else 0), bulb="bulb" if lit else None,
                          hand="wag" if lit else "point", phase=i, look=(1, -2) if lit else (0, 0),
                          mouth_kind="open" if 6 <= i < 10 else "grin"), 150))
    return out


def idle_look():
    """Looks around through his specs, a knowing smirk, back to the grin."""
    seq = [((0, 0), "grin", "open", 400), ((-2, 0), "small", "open", 500), ((-2, 1), "small", "half", 400),
           ((2, 0), "small", "open", 500), ((2, -1), "smile", "half", 500), ((0, 0), "smile", "closed", 120),
           ((0, 0), "grin", "open", 500)]
    out = []
    for i, (look, m, e, ms) in enumerate(seq):
        out.append((frame(dy=breath(i, 2), look=look, mouth_kind=m, eyes_kind=e, sway=sway(i, 6), hand="point"), ms))
    return out


def sleep():
    out = []
    n = 16
    for i in range(n):
        fx = FX()
        for k in range(3):   # Zzz drifting up and to the right
            t = (i + k * 5) % 15
            if t < 12:
                fx.glyph("Z" if k == 0 else "z", 104 + t, 30 - t * 2, "zzz")
        out.append((frame(fx_front=fx, dy=1 + breath(i, 8), eyes_kind="closed", mouth_kind="small", hand="none",
                          sway=((4, 4 + breath(i, 8)), (-4, 4 + breath(i, 8)))), 260))
    return out


def busy():
    """Chewing through the code: eyes scanning, a </> tag, sawdust falling."""
    out = []
    rnd = random.Random(7)
    dust = [(rnd.randint(60, 90), rnd.randint(0, 11)) for _ in range(8)]
    for i in range(12):
        fx = FX()
        fx.glyph("</>", 104, 14 + (i // 3) % 2, "code")
        for x, t0 in dust:
            t = (i + t0) % 12
            y = 60 + t * 2
            if y < H:
                fx.px(x + (t % 3) - 1, y, "dust" if t % 2 else "dust_d")
        look = (-2, 1) if (i // 3) % 2 == 0 else (2, 1)
        out.append((frame(fx_front=fx, dy=breath(i, 2), look=look, hand="none", sway=sway(i, 3, 1),
                          mouth_kind=("talk", "flat")[i % 2]), 140))
    return out


def attention():
    out = []
    for i in range(10):
        fx = FX()
        fx.glyph("!", 109, 18 - (i % 2) * 2, "gold")
        bulb = "bulb" if i % 2 == 0 else None
        jolt = -1 if i % 4 == 0 else 0
        out.append((frame(fx_front=fx, dy=jolt, eyes_kind="wide", mouth_kind="o", hand="up", bulb=bulb,
                          sway=((-1 + (i % 2), -1), (1 - (i % 2), -1))), 150))
    return out


def celebrate():
    out = []
    rnd = random.Random(3)
    bits = [(rnd.randint(4, W - 8), rnd.randint(-40, 0), rnd.choice(["gold", "heart", "code", "zzz", "shaving"]),
             rnd.choice([SPARK, STAR, CURL])) for _ in range(16)]
    hop = [0, -2, -4, -4, -2, 0, 1, 0]
    for i in range(16):
        fx = FX()
        for x, y0, c, shp in bits:
            y = y0 + i * 5
            if 0 <= y < H - 4 and not (48 <= x <= 102 and y > 16):
                fx.shape(shp, x, y, c)
        out.append((frame(fx_front=fx, dy=hop[i % 8], eyes_kind="happy", mouth_kind="open", hand="both",
                          bulb="bulb", sway=sway(i, 4, 2, -1)), 110))
    return out


def dizzy():
    out = []
    for i in range(12):
        fx_b, fx_f = FX(), FX()
        for k in range(3):   # stars orbiting his head
            a = 2 * math.pi * (i / 12 + k / 3)
            x, y = FACE_X + 30 * math.cos(a) - 2, 16 + 7 * math.sin(a)
            (fx_f if math.sin(a) > 0 else fx_b).shape(STAR, round(x), round(y), "gold")
        a = 2 * math.pi * i / 6
        circle = ((round(3 * math.cos(a)), round(2 * math.sin(a))), (round(3 * math.cos(a + 1)), round(2 * math.sin(a + 1))))
        out.append((frame(fx_back=fx_b, fx_front=fx_f, dy=(0, 1)[(i // 3) % 2], eyes_kind="spiral", phase=i,
                          mouth_kind="wavy", hand="none", sway=circle), 120))
    return out


def heart():
    out = []
    for i in range(12):
        fx = FX()
        for k in range(3):
            t = (i + k * 4) % 12
            x = (106 if k % 2 else 24) + round(2 * math.sin(t / 2))
            fx.shape(HEART_S if t < 6 else HEART_M, x, 44 - t * 3, "heart")
        out.append((frame(fx_front=fx, dy=breath(i, 4), eyes_kind="heart", phase=i // 2, blush=True,
                          mouth_kind="grin" if i % 6 < 4 else "smile", sway=sway(i, 6, 1, 1), hand="point"), 160))
    return out


def talk():
    """Explaining something, finger up for emphasis."""
    mouths = ["talk", "grin", "small", "open", "talk", "smile", "o", "grin"]
    out = []
    for i in range(16):
        out.append((frame(dy=breath(i, 4), mouth_kind=mouths[i % 8], hand="wag", phase=i,
                          eyes_kind="closed" if i == 11 else "open", sway=sway(i, 8, 1)), 130))
    return out


def listen():
    """All ears: antennae lean towards the sound, eyes to the side, sound waves coming in."""
    out = []
    for i in range(12):
        fx = FX()
        for k in range(3):
            t = (i + k * 4) % 12
            x = 126 - t * 2
            if x > 102:
                fx.glyph(")", x - 4, 26 + k * 3 - 2, "wave")
        out.append((frame(fx_front=fx, dy=breath(i, 6), look=(2, -1), eyes_kind="open", mouth_kind="small",
                          hand="none", sway=((3, 0), (4 + (i % 2), 0))), 150))
    return out


STATES = {
    "sleep": [("sleep.gif", sleep)],
    "idle": [("idle.gif", idle), ("idle_idea.gif", idle_idea), ("idle.gif", None), ("idle_look.gif", idle_look)],
    "busy": [("busy.gif", busy)],
    "attention": [("attention.gif", attention)],
    "celebrate": [("celebrate.gif", celebrate)],
    "dizzy": [("dizzy.gif", dizzy)],
    "heart": [("heart.gif", heart)],
    "talk": [("talk.gif", talk)],
    "listen": [("listen.gif", listen)],
}


# ------------------------------------------------------------------ output

def build_palette(all_frames):
    colours = {BG}
    for im, _ in all_frames:
        for _n, c in im.getcolors(W * H) or []:
            if c[3]:
                colours.add(c[:3])
    colours = sorted(colours - {BG})
    assert len(colours) < 255, f"{len(colours)} colours"
    pal = [BG] + colours
    flat = [v for c in pal for v in c] + [0] * (768 - 3 * len(pal))
    img = Image.new("P", (1, 1))
    img.putpalette(flat)
    return img, {c: i for i, c in enumerate(pal)}


def to_p(im, index, scale=1):
    out = Image.new("P", (W, H), 0)
    out.putdata([index[p[:3]] if p[3] else 0 for p in im.get_flattened_data()])
    return out.resize((W * scale, H * scale), Image.NEAREST) if scale > 1 else out


def save_gif(path, frames, palimg, index, scale=1):
    ps = []
    for im, _ in frames:
        p = to_p(im, index, scale)
        p.putpalette(palimg.getpalette())
        ps.append(p)
    ps[0].save(path, save_all=True, append_images=ps[1:], duration=[d for _, d in frames], loop=0,
               optimize=False, disposal=1)


def main():
    PACK.mkdir(exist_ok=True)
    PREVIEW.mkdir(exist_ok=True)
    built = {}
    for state, entries in STATES.items():
        for fn, make in entries:
            if make and fn not in built:
                built[fn] = make()
    palimg, index = build_palette([f for fr in built.values() for f in fr])
    for fn, frames in built.items():
        save_gif(PACK / fn, frames, palimg, index)
        save_gif(PREVIEW / fn, frames, palimg, index, scale=3)
    manifest = {
        "name": "Hunold",
        "colors": {"body": "#D6A064", "bg": "#000000", "text": "#FFFFFF", "textDim": "#808080", "ink": "#000000"},
        "states": {s: (e[0][0] if len(e) == 1 else [fn for fn, _ in e]) for s, e in STATES.items()},
    }
    (PACK / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
    # contact sheet: a few frames of every animation, 2x, on the board's background
    rows = list(built.items())
    cols, s = 6, 2
    sheet = Image.new("RGB", (cols * W * s, len(rows) * H * s), (21, 17, 15))
    for r, (fn, frames) in enumerate(rows):
        for c in range(cols):
            im = frames[(c * len(frames)) // cols][0].resize((W * s, H * s), Image.NEAREST)
            sheet.paste(im, (c * W * s, r * H * s), im)
    sheet.save(PREVIEW / "sheet.png")
    total = sum((PACK / fn).stat().st_size for fn in built)
    print(f"{len(built)} GIFs, {sum(len(f) for f in built.values())} frames, {len(index)} colours, {total // 1024} KB")


if __name__ == "__main__":
    main()
