"""Hunold Holzwurm as poseable parts, cut from his reference art (base.png, see trace.py).

The art stays as drawn; animation edits it in place: the eyes (look, blink and effect eyes inside
the lenses), the mouth, the pointing hand (moved, wagged, hidden or mirrored) and the antennae
(swayed row by row from their base). compose() builds one 135 x 82 frame from a pose; effects
(Zzz, hearts, sawdust...) are drawn afterwards, see animate.py.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
W, H = 135, 82
BASE = Image.open(HERE / "base.png").convert("RGBA")

P = dict(
    ol=(24, 20, 24), eye=(250, 248, 244), eye_d=(222, 214, 204),
    heart=(240, 70, 100), heart_h=(255, 170, 190), blush=(236, 128, 112),
    gold=(250, 200, 60), gold_h=(255, 240, 160), spark=(255, 255, 255),
    code=(120, 230, 120), zzz=(170, 200, 255), wave=(150, 220, 255),
    spiral=(92, 52, 24), dust=(200, 150, 92), dust_d=(150, 100, 56), shaving=(236, 196, 138),
    mouth=(110, 36, 34), tongue=(214, 98, 88), teeth=(250, 248, 240), iris=(120, 72, 36),
    bulb=(255, 214, 90), bulb_h=(255, 248, 200),
)

# where things are in base.png (canvas pixels)
EYES = ((60, 37, 70, 47), (80, 37, 90, 47))     # the white of each eye, inside the lenses
IRIS = ((66, 43), (84, 43))                     # iris centres in the art
LID_ROW = 35                                    # wood just above the eyes: the eyelid colour per column
MOUTH = (59, 52, 86, 62)                        # box around the open grin and its creases
CHEEK_ROW = 51                                  # wood just above the mouth
HAND = (28, 54, 51, 82)                         # the pointing hand and forearm (left of the vest)
ANT_ROW = 19                                    # antennae are everything above this row
HEAD_CX = 75                                    # middle of the face, for mirroring


def rgba(c):
    return (P[c] if isinstance(c, str) else c[:3]) + (255,)


def ellipse_mask(box):
    m = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m).ellipse(box, fill=1)
    return m


def _eye_mask(box):
    """The eye as drawn: flood from its white up to its dark outline, then fill the pupil in."""
    x0, y0, x1, y1 = box
    m = Image.new("L", (W, H), 0)
    inside = ellipse_mask((x0 - 1, y0 - 1, x1 + 1, y1 + 1))   # never past the eye's own outline
    todo = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1) if min(BASE.getpixel((x, y))[:3]) > 205]
    seen = set()
    while todo:
        x, y = todo.pop()
        if (x, y) in seen or not inside.getpixel((x, y)):
            continue
        seen.add((x, y))
        if sum(BASE.getpixel((x, y))[:3]) < 120:
            continue
        m.putpixel((x, y), 1)
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    for y in range(y0 - 2, y1 + 3):   # holes (the pupil): anything with the eye on both sides of its row
        xs = [x for x in range(x0 - 2, x1 + 3) if m.getpixel((x, y))]
        if xs:
            for x in range(xs[0], xs[-1] + 1):
                m.putpixel((x, y), 1)
    return m


EYE_MASKS = [_eye_mask(b) for b in EYES]


def _mouth_mask():
    """The grin: flood from the teeth across everything that isn't plain wood."""
    m = Image.new("L", (W, H), 0)
    x0, y0, x1, y1 = MOUTH

    def wood(p):
        r, g, b, a = p
        return a and r > 150 and g >= 100 and min(r, g, b) < 205
    todo, seen = [(73, 56)], set()
    while todo:
        x, y = todo.pop()
        if (x, y) in seen or not (x0 <= x <= x1 and y0 <= y <= y1):
            continue
        seen.add((x, y))
        if wood(BASE.getpixel((x, y))):
            continue
        m.putpixel((x, y), 1)
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return m


def _cheek():
    """Per column, the plain wood just above the mouth (the grain runs down, so it carries on)."""
    out = {}
    for x in range(W):
        for y in range(CHEEK_ROW + 1, 40, -1):
            p = BASE.getpixel((x, y))
            if p[3] and sum(p[:3]) > 330:
                out[x] = p
                break
    return out


CHEEK = _cheek()
# the grin and its corner creases: everything in the mouth box that isn't plain wood
MOUTH_MASK = Image.new("L", (W, H), 0)
for _y in range(MOUTH[1], MOUTH[3] + 1):
    for _x in range(MOUTH[0], MOUTH[2] + 1):
        if _x in CHEEK and (sum(BASE.getpixel((_x, _y))[:3]) < 330 or min(BASE.getpixel((_x, _y))[:3]) > 205):
            MOUTH_MASK.putpixel((_x, _y), 1)
MOUTH_MASK.paste(1, mask=_mouth_mask())


class Frame:
    """A 135 x 82 RGBA image with pixel helpers."""

    def __init__(self, im=None):
        self.im = im.copy() if im else Image.new("RGBA", (W, H), (0, 0, 0, 0))

    def px(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.im.putpixel((int(x), int(y)), rgba(c))

    def fill(self, mask, colour_at):
        box = mask.getbbox()
        if not box:
            return
        for y in range(box[1], box[3]):
            for x in range(box[0], box[2]):
                if mask.getpixel((x, y)):
                    c = colour_at(x, y)
                    if c:
                        self.px(x, y, c)

    def sprite(self, rows, x, y, colours, outline=None):
        cells = [(x + i, y + j, ch) for j, r in enumerate(rows) for i, ch in enumerate(r) if ch != "."]
        if outline:
            for a, b, _ in cells:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    self.px(a + dx, b + dy, outline)
        for a, b, ch in cells:
            self.px(a, b, colours[ch])


# ------------------------------------------------------------------ eyes

def eyes(F, kind, look, phase):
    """kind: open (the art's iris, moved by look), wide, closed, happy, half, spiral, heart, star."""
    lx, ly = look
    for box, m, (icx, icy) in zip(EYES, EYE_MASKS, IRIS):
        x0, y0, x1, y1 = box
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        if kind == "open" and look == (0, 0):
            continue
        lid = lambda x, y: BASE.getpixel((x, LID_ROW))
        if kind in ("closed", "happy"):
            F.fill(m, lid)
            for k in range(-4, 5):
                bend = k * k / 16
                F.px(cx + k, round(cy - 1 + bend * 2) if kind == "happy" else round(cy + 1.5 - bend * 1.5), "ol")
            continue
        # a fresh white, then whatever goes inside it
        F.fill(m, lambda x, y: P["eye_d"] if y >= y1 - 1 else P["eye"])
        if kind == "spiral":
            for k in range(30):
                a = k * 0.55 + phase * 1.2
                r = k * 0.17
                F.px(round(cx + r * math.cos(a)), round(cy + r * math.sin(a)), "spiral")
        elif kind == "heart":
            rows = ([".11.11.", "1211111", "1111111", ".11111.", "..111..", "...1..."] if phase % 2 == 0 else
                    ["..1.1..", ".12111.", ".11111.", "..111..", "...1..."])
            F.sprite(rows, round(cx) - 3, round(cy) - 3, {"1": "heart", "2": "heart_h"})
        elif kind == "star":
            s = 3 + phase % 2
            for k in range(-s, s + 1):
                F.px(cx + k, cy, "gold"); F.px(cx, cy + k, "gold")
            for k in (-1, 1):
                F.px(cx + k, cy + k, "gold"); F.px(cx + k, cy - k, "gold")
            F.px(cx, cy, "gold_h")
        elif kind == "wide":   # small startled irises
            ix, iy = round(cx) + lx, round(cy) + ly
            F.fill(ellipse_mask((ix - 2, iy - 2, ix + 2, iy + 2)), lambda x, y: P["iris"] if m.getpixel((x, y)) else None)
            F.fill(ellipse_mask((ix - 1, iy - 1, ix + 1, iy + 1)), lambda x, y: P["ol"] if m.getpixel((x, y)) else None)
            F.px(ix - 1, iy - 1, "eye")
        else:   # the art's iris, moved
            for y in range(icy - 6, icy + 6):
                for x in range(icx - 6, icx + 6):
                    p = BASE.getpixel((x, y))
                    if m.getpixel((x, y)) and p[3] and min(p[:3]) < 200 and m.getpixel((x + lx, y + ly)):
                        F.px(x + lx, y + ly, p)
        if kind == "half":   # sleepy: the top lidded
            F.fill(m, lambda x, y: lid(x, y) if y < cy - 1 else (P["ol"] if y == int(cy - 1) else None))


# ------------------------------------------------------------------ mouth

def mouth(F, kind):
    """kind: grin (as drawn), smile, small, o, flat, wavy, talk (a smaller grin), open (wider)."""
    if kind == "grin":
        return
    F.fill(MOUTH_MASK, lambda x, y: CHEEK[x])
    cx, y = 75, 56          # the middle of the grin as drawn
    if kind == "smile":
        for k in range(-8, 9):
            F.px(cx + k, y - 1 + round((64 - k * k) / 64 * 2.4), "ol")
        F.px(cx - 9, y - 2, "ol"); F.px(cx + 9, y - 2, "ol")
    elif kind == "small":
        for k in range(-4, 5):
            F.px(cx + k, y + round((16 - k * k) / 16 * 1.5), "ol")
    elif kind == "flat":
        for k in range(-4, 5):
            F.px(cx + k, y + 1, "ol")
    elif kind == "wavy":
        for i, k in enumerate(range(-6, 7)):
            F.px(cx + k, y + (i // 2) % 2, "ol")
    elif kind in ("o", "talk", "open"):
        w, h = {"o": (2, 2), "talk": (5, 2), "open": (8, 3)}[kind]
        m = ellipse_mask((cx - w, y - h, cx + w, y + h + 1))
        F.fill(m, lambda x, yy: "tongue" if yy >= y + h - 1 and kind != "o" else "mouth")
        if kind != "o":
            for x in range(cx - w + 2, cx + w - 1):
                F.px(x, y - h + 1, "teeth")
        edge = [(x, yy) for yy in range(y - h - 1, y + h + 3) for x in range(cx - w - 1, cx + w + 2)
                if not m.getpixel((x, yy)) and any(m.getpixel((x + a, yy + b)) for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
        for x, yy in edge:
            F.px(x, yy, "ol")


# ------------------------------------------------------------------ antennae and hand

def antenna_layer(sway, bulb):
    """The antennae, each row pushed sideways (and the tips down) more the higher it is."""
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    for side, (ox, oy) in zip(("l", "r"), sway):
        for y in range(ANT_ROW):
            t = (ANT_ROW - y) / ANT_ROW
            dx, dy = round(ox * t * t), round(oy * t)
            for x in (range(0, HEAD_CX) if side == "l" else range(HEAD_CX, W)):
                p = BASE.getpixel((x, y))
                if p[3] and 0 <= x + dx < W and 0 <= y + dy < H:
                    out.putpixel((x + dx, y + dy), p)
        if bulb:   # the knob lights up: its lighter pixels recoloured
            tx, ty = (53 if side == "l" else 93) + ox, 2 + oy
            for yy in range(max(0, ty - 3), min(H, ty + 4)):
                for xx in range(max(0, tx - 5), min(W, tx + 6)):
                    p = out.getpixel((xx, yy))
                    if p[3] and sum(p[:3]) > 200:
                        out.putpixel((xx, yy), rgba("bulb_h" if (xx < tx and yy < ty) else bulb))
    return out


HAND_IMG = Image.new("RGBA", (W, H), (0, 0, 0, 0))
HAND_IMG.paste(BASE.crop(HAND), HAND[:2])
BODY = BASE.copy()
for _y in range(H):                       # the body without the antennae and the hand
    for _x in range(W):
        if _y < ANT_ROW or (HAND[0] <= _x < HAND[2] and HAND[1] <= _y < HAND[3]):
            BODY.putpixel((_x, _y), (0, 0, 0, 0))


def hand_pose(kind, phase):
    """kind: point (as drawn), up (raised), wag (finger wagging), both (a mirrored second hand
    on the right), none (out of frame)."""
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if kind == "none":
        return out
    h = HAND_IMG
    if kind == "wag":   # shear the finger above the knuckles
        h = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        off = (-1, 0, 1, 0)[phase % 4]
        for y in range(H):
            row = HAND_IMG.crop((0, y, W, y + 1))
            h.paste(row, (round(off * max(0, 67 - y) / 6), y), row)
    lift = {"point": 0, "wag": 0, "up": -4, "both": -3}[kind]
    if lift:   # raise it, and stretch the forearm's bottom row down to the frame's edge
        raised = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        raised.paste(h.crop((0, -lift, W, H)), (0, 0))
        last = h.crop((0, H - 1, W, H))
        for y in range(H + lift, H):
            raised.paste(last, (0, y))
        h = raised
    out.alpha_composite(h)
    if kind == "both":
        mirror = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        flipped = h.transpose(Image.FLIP_LEFT_RIGHT)
        mirror.paste(flipped, (2 * HEAD_CX - W, 0), flipped)   # mirrored about the face's middle
        out.alpha_composite(mirror)
    return out


# ------------------------------------------------------------------ composition

def compose(dy=0, eyes_kind="open", look=(0, 0), mouth_kind="grin", hand="point", sway=((0, 0), (0, 0)),
            bulb=None, blush=False, phase=0):
    F = Frame(BODY)
    eyes(F, eyes_kind, look, phase)
    mouth(F, mouth_kind)
    if blush:
        for cx in (57, 93):
            for k in range(-2, 3):
                F.px(cx + k, 51, "blush")
            for k in range(-1, 2):
                F.px(cx + k, 52, "blush")
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    img.alpha_composite(antenna_layer(sway, bulb))
    img.alpha_composite(F.im)
    img.alpha_composite(hand_pose(hand, phase))
    if dy:
        moved = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        moved.paste(img, (0, dy))
        img = moved
    return img
