"""Build the DevelopKing GIF pack: one GIF per buddy state plus manifest.json.

  python characters/developking/src/animate.py

Writes characters/developking/pack/ (copy that folder to the device: SD card or the Claude
app's folder push) and characters/developking/preview/ (enlarged GIFs and a contact sheet).
"""
import json
import math
import random
from pathlib import Path

from PIL import Image

from parts import H, P, W, Layer, compose

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
}
HEART_S = [".1.1.", "11111", ".111.", "..1.."]
HEART_M = [".11.11.", "1211111", "1111111", ".11111.", "..111..", "...1..."]
STAR = ["..1..", ".111.", "11111", ".111.", ".1.1."]
SPARK = [".1.", "111", ".1."]


class FX(Layer):
    def __init__(self):
        super().__init__(W, H)

    def glyph(self, s, x, y, c, outline=True):
        """Text with a 1-pixel dark outline so it reads on any background."""
        cells = []
        cx = x
        for ch in s:
            rows = GLYPHS[ch]
            for j, r in enumerate(rows):
                for i, v in enumerate(r):
                    if v == "1":
                        cells.append((cx + i, y + j))
            cx += len(rows[0]) + 1
        if outline:
            for a, b in cells:
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        self.px(a + dx, b + dy, "ol")
        for a, b in cells:
            self.px(a, b, c)

    def sprite(self, rows, x, y, c, outline=True):
        cells = [(x + i, y + j, ch) for j, r in enumerate(rows) for i, ch in enumerate(r) if ch != "."]
        if outline:
            for a, b, _ in cells:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    self.px(a + dx, b + dy, "ol")
        for a, b, ch in cells:
            self.px(a, b, "heart_h" if ch == "2" else c)


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


# ------------------------------------------------------------------ states

def idle(n=24):
    """Breathing; a shine sweeps across the lenses now and then."""
    out = []
    for i in range(n):
        g = i - 14
        out.append((frame(dy=breath(i), lens="glint" if 0 <= g <= 14 else "rest", phase=g), 140))
    return out


def idle_peek():
    """Pulls the glasses down, looks around, blinks, pushes them back up."""
    out = idle(16)
    seq = [(1, "open", "mid")] + [(2, "open", "mid")] + [(3, "open", "mid")] * 3 + [(3, "open", "right")] * 4 + \
          [(3, "open", "left")] * 4 + [(3, "closed", "mid")] + [(3, "open", "mid")] * 3 + [(2, "open", "mid"), (1, "open", "mid")]
    for i, (g, e, look) in enumerate(seq):
        out.append((frame(dy=0, glasses_dy=g, eye=e if g == 3 else "open", look=look, lens="rest",
                          brows=g == 3 and look == "right", mouth="smile"), 120))
    return out + idle(8)


def idle_hat_tip():
    """A friendly hat tip with a grin."""
    out = idle(16)
    lift = [0, -1, -3, -4, -4, -4, -4, -3, -1, 0]
    rot = [0, 4, 8, 10, 10, 10, 10, 8, 4, 0]
    for i, (l, r) in enumerate(zip(lift, rot)):
        out.append((frame(hat_dy=l, hat_dx=1 if l else 0, hat_rot=r, mouth="grin" if 2 <= i <= 7 else "smile",
                          lens="rest"), 110))
    return out + idle(8)


def sleep():
    """Hat over the eyes, glasses down on closed eyes, snoring, Zzz floating up."""
    out = []
    for i in range(16):
        fx = FX()
        for k, ch in enumerate("zZZ"):
            t = (i + k * 5) % 16
            x = 70 + k * 7 + (1 if t % 4 < 2 else 0)
            y = 26 - t * 2 + k
            if 0 <= y < H - 6:
                fx.glyph(ch, x, y, "lens_hi")
        out.append((frame(fx_front=fx, dy=2 + breath(i), head_dy=1, hat_dy=2, hat_rot=-4, glasses_dy=3, eye="closed",
                          lens="rest", mouth="o" if breath(i) else "half", brim_shadow=True), 180))
    return out


def busy():
    """Code scrolling in the lenses, focused face, a </> tag bobbing."""
    out = []
    for i in range(12):
        fx = FX()
        fx.glyph("</>", 71, 6 + (1 if i % 4 < 2 else 0), "code_g")
        if i % 6 < 3:   # a typing "..." under it
            for k in range(3):
                if k <= i % 3:
                    fx.px(74 + k * 3, 16, "code_w")
        out.append((frame(fx_front=fx, dy=breath(i, 4), lens="code", phase=i, mouth="flat"), 110))
    return out


def attention():
    """Glasses drop, wide eyes and brows up, a bouncing '!'."""
    out = []
    drop = [1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3]
    for i, g in enumerate(drop):
        fx = FX()
        bounce = [0, -3, -5, -3, 0, -2, 0, 0, -3, -5, -3, 0][i]
        fx.glyph("!", 80, 12 + bounce, "gold")
        out.append((frame(fx_front=fx, glasses_dy=g, eye="wide" if g == 3 else "open", look="right",
                          brows=g == 3, hat_dy=-2 if i in (2, 3, 9) else 0, mouth="o", lens="rest",
                          dy=1 if i in (0, 1) else 0), 100))
    return out


def celebrate():
    """The straw hat flies off and the DevelopKings crown lands on his head. Confetti."""
    rnd = random.Random(5)
    confetti = [(rnd.randrange(0, W), rnd.randrange(-H, 0), rnd.choice(["gold", "band", "code_g", "lens", "heart_h", "eye"]),
                 rnd.choice([1, 2])) for _ in range(40)]
    plan = []
    plan += [dict(hat="straw", dy=d, mouth="grin") for d in (0, 1, 2)]                               # crouch
    plan += [dict(hat="straw", hat_dy=y, hat_rot=r, dy=0, mouth="grin")                              # hat flies
             for y, r in ((-6, 15), (-14, 35), (-24, 60), (-40, 90))]
    plan += [dict(hat="crown", hat_dy=y, dy=0, mouth="open") for y in (-30, -18, -8, -2)]            # crown drops
    plan += [dict(hat="crown", hat_dy=y, dy=d, mouth="grin") for y, d in ((1, 1), (0, 0), (0, 0))]  # lands
    plan += [dict(hat="crown", dy=breath(i, 4), mouth="grin" if i % 4 else "open") for i in range(14)]
    plan += [dict(hat="crown", hat_dy=y, dy=0, mouth="smile") for y in (-6, -16, -30)]              # crown leaves
    plan += [dict(hat="straw", hat_dy=y, dy=0, mouth="smile") for y in (-20, -10, -3, 0)]           # hat back
    out = []
    for i, p in enumerate(plan):
        fx = FX()
        if 11 <= i <= 30:
            t = i - 11
            for x, y, c, s in confetti:
                yy = y + t * 4
                xx = x + int(2 * math.sin((t + x) * 0.6))
                if 0 <= yy < H:
                    fx.px(xx, yy, c)
                    if s == 2:
                        fx.px(xx + 1, yy, c)
        crowned = p["hat"] == "crown"
        out.append((frame(fx_front=fx, lens="star" if crowned else "rest", phase=i, brim_shadow=not crowned and p.get("hat_dy", 0) >= -2,
                          **p), 90))
    return out


def dizzy():
    """Head wobbles, spirals in the lenses, stars circling the hat."""
    out = []
    n = 12
    for i in range(n):
        back, front = FX(), FX()
        for k in range(3):
            a = 2 * math.pi * (i / n + k / 3)
            x = 48 + int(26 * math.cos(a)) - 2
            y = 6 + int(4 * math.sin(a)) - 2
            (front if math.sin(a) > 0 else back).sprite(STAR, x, y, "gold")
        dx = round(2 * math.sin(2 * math.pi * i / n))
        out.append((frame(fx_back=back, fx_front=front, head_dx=dx, hat_dx=dx // 2, hat_rot=-4 * dx,
                          lens="spiral", phase=i, mouth="wavy"), 100))
    return out


def heart():
    """Hearts in the lenses, blushing, hearts floating up."""
    out = []
    for i in range(12):
        fx = FX()
        for k in range(3):
            t = (i + k * 4) % 12
            x = 70 + k * 8 + (1 if (t // 2) % 2 else 0)
            y = 34 - t * 3
            if y > -6:
                fx.sprite(HEART_S if k != 1 else HEART_M, x, y, "heart")
        out.append((frame(fx_front=fx, lens="heart", phase=i // 2, blush=True, mouth="smile",
                          head_dx=1 if i % 6 < 3 else 0, dy=breath(i, 6)), 120))
    return out


def talk():
    """Talking: the mouth moves, the head bobs a little."""
    mouths = ["open", "half", "smile", "o", "open", "flat", "half", "grin", "o", "half"]
    return [(frame(mouth=m, dy=1 if i % 3 == 0 else 0, lens="glint", phase=i * 2), 100)
            for i, m in enumerate(mouths)]


def listen():
    """Hand cupped behind the ear, sound waves coming in."""
    out = []
    for i in range(10):
        fx = FX()
        for k in range(3):
            r = 10 - ((i + k * 3) % 9)      # arcs moving towards the ear
            if r >= 2:
                cx, cy = 28, 26
                for t in range(-3, 4):
                    a = t * 0.3
                    fx.px(cx - r - int(r * (1 - math.cos(a))), cy + round(r * math.sin(a) * 0.9), "lens_hi")
        out.append((frame(fx_front=fx, arm=True, head_dx=-1, brows=False, mouth="o" if i % 5 == 4 else "smile",
                          lens="rest", dy=breath(i, 10)), 130))
    return out


STATES = {
    "sleep": [("sleep.gif", sleep)],
    "idle": [("idle.gif", idle), ("idle_peek.gif", idle_peek), ("idle.gif", None), ("idle_hat.gif", idle_hat_tip)],
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
    pixels = im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()
    data = [index[p[:3]] if p[3] else 0 for p in pixels]
    out.putdata(data)
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
        save_gif(PREVIEW / fn, frames, palimg, index, scale=4)
    manifest = {
        "name": "developking",
        "colors": {"body": "#E2C084", "bg": "#000000", "text": "#FFFFFF", "textDim": "#808080", "ink": "#000000"},
        "states": {s: (e[0][0] if len(e) == 1 else [fn for fn, _ in e]) for s, e in STATES.items()},
    }
    (PACK / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
    # contact sheet: a few frames of every animation, 3x
    rows = list(built.items())
    cols = 8
    sheet = Image.new("RGB", (cols * W * 3, len(rows) * H * 3), (26, 22, 20))
    for r, (fn, frames) in enumerate(rows):
        for c in range(cols):
            im = frames[(c * len(frames)) // cols][0].resize((W * 3, H * 3), Image.NEAREST)
            sheet.paste(im, (c * W * 3, r * H * 3), im)
    sheet.save(PREVIEW / "sheet.png")
    total = sum((PACK / fn).stat().st_size for fn in built)
    print(f"{len(built)} GIFs, {sum(len(f) for f in built.values())} frames, {len(index)} colours, {total // 1024} KB")


if __name__ == "__main__":
    main()
