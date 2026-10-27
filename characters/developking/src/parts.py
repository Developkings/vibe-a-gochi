"""DevelopKing buddy as poseable parts.

Every part is drawn on its own 64x64 layer in the "turned left" space (light from the upper
left), then compose() pastes the layers with offsets onto a wider canvas and mirrors it, so
the finished character faces the viewer's right. Effects (Zzz, hearts, confetti...) are drawn
afterwards in the final space, see animate.py.
"""
from PIL import Image, ImageDraw

N = 64
P = dict(
    ol=(29, 27, 38),
    coat=(244, 244, 248), coat_d=(196, 196, 214), coat_dd=(142, 140, 164),
    shirt=(40, 38, 52), shirt_d=(26, 24, 34),
    skin=(236, 176, 138), skin_d=(204, 136, 106), skin_h=(250, 206, 172), ear_d=(170, 104, 82),
    blush=(236, 128, 120),
    beard=(50, 46, 52), beard_g=(96, 92, 100),
    lip=(176, 96, 88), mouth=(92, 30, 40), teeth=(250, 248, 240),
    eye=(250, 248, 240),
    lens=(46, 168, 170), lens_d=(24, 110, 120), lens_g=(84, 170, 96), lens_hi=(206, 250, 244),
    lens_dark=(18, 40, 52), code_g=(120, 230, 120), code_w=(220, 240, 230), code_o=(218, 119, 86),
    frame=(238, 220, 166), frame_d=(196, 170, 112),
    straw=(226, 192, 132), straw_d=(182, 144, 92), straw_h=(244, 220, 170),
    band=(200, 40, 48), band_d=(140, 24, 36), hair=(36, 32, 40), hair_h=(70, 66, 80),
    gold=(250, 200, 60), gold_d=(200, 140, 30), gold_h=(255, 240, 160), gem=(200, 40, 48),
    heart=(240, 70, 100), heart_h=(255, 170, 190),
    logo=(29, 27, 38), pen=(218, 119, 86),
)


class Layer:
    def __init__(self, w=N, h=N):
        self.im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)
        self.w, self.h = w, h

    def mask(self, *shapes):
        m = Image.new("L", (self.w, self.h), 0)
        g = ImageDraw.Draw(m)
        for kind, args in shapes:
            getattr(g, kind)(args, fill=1)
        return m

    def on(self, m, x, y):
        return 0 <= x < self.w and 0 <= y < self.h and m.getpixel((x, y))

    def px(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.im.putpixel((x, y), P[c] + (255,) if isinstance(c, str) else c + (255,))

    def get(self, x, y):
        return self.im.getpixel((x, y))[:3] if 0 <= x < self.w and 0 <= y < self.h else None

    def fill(self, m, shade):
        for y in range(self.h):
            for x in range(self.w):
                if m.getpixel((x, y)):
                    c = shade(x, y)
                    if c:
                        self.px(x, y, c)

    def outline(self, m, col="ol"):
        for y in range(self.h):
            for x in range(self.w):
                if m.getpixel((x, y)) and any(not self.on(m, x + a, y + b)
                                              for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    self.px(x, y, col)

    def part(self, m, shade, col="ol"):
        self.fill(m, shade)
        self.outline(m, col)
        return m

    def line(self, pts, c):
        self.d.line(pts, fill=P[c])

    def bitmap(self, rows, x, y, c):
        for j, r in enumerate(rows):
            for i, ch in enumerate(r):
                if ch != ".":
                    self.px(x + i, y + j, c if ch == "1" else {"2": "heart_h", "3": "ol", "4": "gold_d"}.get(ch, c))


def pts(*p):
    return list(p)


CROWN_LOGO = ["....1....",
              "1..1.1..1",
              "11.1.1.11",
              "1.11.11.1",
              "1...1...1",
              "111111111"]

# ------------------------------------------------------------------ body


def body():
    L = Layer()
    coat = L.mask(("polygon", pts((20, 47), (40, 47), (52, 49), (58, 52), (61, 63), (2, 63), (4, 53), (10, 49))))
    L.part(coat, lambda x, y: "coat_d" if x >= 50 or x <= 6 else "coat")
    L.line([(11, 54), (11, 63)], "coat_dd")   # sleeve seams
    L.line([(50, 53), (51, 63)], "coat_dd")
    L.line([(8, 55), (8, 60)], "coat_d")
    # neck: from behind the jaw down into the T-shirt collar
    L.part(L.mask(("polygon", pts((24, 38), (38, 34), (41, 38), (40, 50), (23, 50)))),
           lambda x, y: "skin_d" if x >= 34 or y >= 46 else "skin")
    shirt = L.mask(("polygon", pts((19, 48), (23, 49), (30, 50), (37, 49), (40, 47), (37, 63), (21, 63))))
    L.part(shirt, lambda x, y: "shirt_d" if x >= 33 else "shirt")
    for p, c in ((pts((17, 47), (22, 48), (24, 63), (19, 63), (17, 54), (14, 50)), "coat"),
                 (pts((37, 47), (42, 47), (46, 51), (40, 54), (38, 63), (34, 63), (35, 52)), "coat_d")):
        L.part(L.mask(("polygon", p)), lambda x, y, c=c: c)
    L.line([(16, 52), (18, 58)], "coat_d")   # fold on the far lapel
    # breast pocket with the DevelopKings crown, pen clipped on it
    pk = L.mask(("polygon", pts((41, 53), (53, 52), (54, 63), (41, 63))))
    L.part(pk, lambda x, y: "coat" if x < 51 else "coat_d")
    L.bitmap(CROWN_LOGO, 43, 55, "logo")
    L.line([(52, 49), (52, 54)], "pen")
    L.px(52, 48, "ol")
    return L.im


def arm_listen():
    """Near arm raised, the hand cupped behind the ear."""
    L = Layer()
    sleeve = L.mask(("polygon", pts((50, 63), (50, 50), (55, 36), (61, 36), (62, 50), (60, 63))))
    L.part(sleeve, lambda x, y: "coat_d" if x >= 58 else "coat")
    L.line([(56, 40), (54, 50)], "coat_d")   # fold at the elbow
    L.part(L.mask(("rectangle", (53, 33, 61, 37))), lambda x, y: "coat_d")   # cuff
    hand = L.mask(("ellipse", (46, 20, 56, 34)))
    L.part(hand, lambda x, y: "skin_d" if x >= 53 else "skin")
    for y in (24, 27, 30):                    # fingers
        L.line([(48, y), (52, y)], "skin_d")
    L.line([(47, 22), (47, 31)], "skin_d")    # the cupped edge
    return L.im


# ------------------------------------------------------------------ head


def lens_paint(L, m, kind, phase, near):
    """Paint inside one lens mask (already filled with the base colours)."""
    box = m.getbbox()
    x0, y0, x1, y1 = box[0] + 1, box[1] + 1, box[2] - 2, box[3] - 2   # inside the outline
    w = x1 - x0 + 1
    cx = (x0 + x1) // 2
    inside = lambda x, y: L.on(m, x, y) and L.get(x, y) != P["ol"]

    def put(x, y, c):
        if inside(x, y):
            L.px(x, y, c)

    if kind == "glint":
        g = x0 - 3 + phase   # a diagonal shine sweeping across
        for k in range(3):
            put(g + k, y0 + k, "lens_hi")
            put(g + k + 1, y0 + k, "lens_hi")
        return
    if kind in ("code", "dark"):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, "lens_dark")
        if kind == "dark":
            return
        # scrolling lines of code, a different indent per line
        for row in range(y0, y1 + 1, 2):
            n = (row // 2 + phase) % 6
            indent = (n * 2) % 5
            length = 3 + (n * 3) % (max(3, w - 3))
            col = ("code_g", "code_w", "code_o", "code_g", "code_w", "code_g")[n]
            for x in range(x0 + indent, min(x1 + 1, x0 + indent + length)):
                put(x, row, col)
        return
    if kind == "heart":
        big = near and phase % 2 == 0
        shape = (["11.11", "11111", ".111.", "..1.."] if not big else
                 [".11.11.", "1211111", "1111111", ".11111.", "..111..", "...1..."])
        sw = len(shape[0])
        hx = cx - sw // 2
        hy = y0 + (0 if big else 1)
        for j, r in enumerate(shape):
            for i, ch in enumerate(r):
                if ch != ".":
                    put(hx + i, hy + j, "heart_h" if ch == "2" else "heart")
        return
    if kind == "spiral":
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                put(x, y, "lens_d")
        cy = (y0 + y1) // 2
        ring = [(0, -2), (1, -2), (2, -1), (2, 0), (2, 1), (1, 2), (0, 2), (-1, 2),
                (-2, 1), (-2, 0), (-2, -1), (-1, -2)]
        for k in range(len(ring)):
            a, b = ring[(k + phase * 3) % len(ring)]
            if k < 8:
                put(cx + a, cy + b, "lens_hi")
        put(cx, cy, "lens_hi")
        return
    if kind == "star":
        cy = (y0 + y1) // 2
        s = 2 if (phase + near) % 2 == 0 else 1
        for k in range(-s, s + 1):
            put(cx + k, cy, "gold_h")
            put(cx, cy + k, "gold_h")
        put(cx, cy, "eye")
        return


def eyes(L, kind, look):
    """Eyes above the lowered glasses: near eye x 29..33, far eye x 15..18, y 18..20."""
    for ex, ew in ((29, 4), (15, 3)):
        if kind == "closed":
            L.line([(ex, 20), (ex + ew - 1, 20)], "ol")
            L.px(ex - 1, 19, "ol")
            continue
        top = 17 if kind == "wide" else 18
        for y in range(top, 21):
            for x in range(ex, ex + ew):
                L.px(x, y, "eye")
        L.line([(ex, top - 1), (ex + ew - 1, top - 1)], "ol")    # upper lid
        px = ex + {"left": 0, "right": ew - 1, "mid": ew // 2}[look]
        L.px(px, 19, "ol")
        L.px(px, 20, "ol")
        if kind == "wide":
            L.px(px, 18, "ol")


def head(mouth="smile", lens="glint", phase=0, glasses_dy=0, eye="open", look="mid",
         blush=False, brim_shadow=True, brows=False):
    L = Layer()
    hd = L.mask(("ellipse", (14, 9, 45, 42)), ("polygon", pts((30, 41), (43, 32), (41, 37), (35, 43))))
    L.part(hd, lambda x, y: "skin_d" if x >= 38 or (x <= 17 and y >= 26) else "skin")
    L.px(19, 29, "skin_h"); L.px(20, 29, "skin_h")   # cheek light
    # black hair on top (hidden by the hat, shows when it comes off)
    cap = L.mask(("ellipse", (14, 8, 46, 30)), ("rectangle", (14, 0, 46, 15)))
    for y in range(0, 17):
        for x in range(0, N):
            if L.on(hd, x, y) and L.on(cap, x, y) and y <= 15 - (2 if 20 < x < 30 else 0):
                L.px(x, y, "hair_h" if (x + 2 * y) % 9 == 0 else "hair")
    L.outline(L.mask(("ellipse", (14, 9, 45, 42))), "ol")
    ear = L.mask(("ellipse", (42, 23, 47, 32)))
    L.part(ear, lambda x, y: "skin_d")
    L.line([(44, 25), (44, 30)], "ear_d")
    L.part(L.mask(("polygon", pts((35, 15), (46, 16), (46, 24), (42, 25), (38, 20)))),
           lambda x, y: "hair_h" if y == 18 and 39 <= x <= 43 else "hair")
    L.part(L.mask(("polygon", pts((14, 16), (18, 15), (17, 21), (14, 22)))), lambda x, y: "hair")
    if blush:
        for x, y in ((31, 27), (32, 27), (33, 27), (32, 28), (14, 27), (15, 27)):
            L.px(x, y, "blush")

    # beard: moustache, chin, a thin jaw line
    beard = L.mask(("ellipse", (17, 31, 33, 45)), ("polygon", pts((31, 36), (41, 31), (42, 33), (34, 42))))
    L.part(beard, lambda x, y: "beard_g" if (x * 3 + (y // 2) * 5) % 11 == 0 else "beard")
    L.part(L.mask(("polygon", pts((18, 31), (24, 29), (31, 30), (33, 32), (30, 34), (20, 34)))),
           lambda x, y: "beard")
    draw_mouth(L, mouth)
    # nose pointing to the viewer's left
    L.line([(20, 25), (19, 26)], "skin_d"); L.px(16, 27, "skin_d")
    L.line([(17, 28), (20, 28)], "skin_d"); L.px(18, 27, "ol"); L.px(16, 26, "skin_h")

    if glasses_dy:
        eyes(L, eye, look)
    if brows:
        L.line([(29, 15), (33, 14)], "hair")
        L.line([(15, 14), (18, 15)], "hair")

    # sunglasses: big near lens, small far lens
    gy = glasses_dy
    base = lambda x, y: "lens_g" if y >= 22 + gy else ("lens_d" if y == 21 + gy else "lens")
    near = L.part(L.mask(("polygon", [(x, y + gy) for x, y in
                                      pts((24, 18), (37, 18), (37, 22), (35, 24), (26, 24), (24, 22))])), base)
    far = L.part(L.mask(("polygon", [(x, y + gy) for x, y in
                                     pts((13, 18), (20, 18), (20, 22), (19, 24), (15, 24), (13, 22))])), base)
    L.line([(13, 17 + gy), (37, 17 + gy)], "frame")
    L.line([(13, 18 + gy), (37, 18 + gy)], "frame_d")
    L.line([(21, 19 + gy), (23, 19 + gy)], "frame")
    L.line([(38, 19 + gy), (43, 22)], "frame_d")       # temple arm stays on the ear
    if lens in ("glint", "rest"):
        L.line([(26, 20 + gy), (27, 21 + gy)], "lens_hi"); L.px(26, 19 + gy, "lens_hi")
        L.px(14, 19 + gy, "lens_hi"); L.px(15, 20 + gy, "lens_hi")
    if lens == "glint":
        lens_paint(L, near, "glint", phase, True)
        lens_paint(L, far, "glint", phase - 4, False)
    elif lens != "rest":
        lens_paint(L, near, lens, phase, True)
        lens_paint(L, far, lens, phase, False)

    if brim_shadow:
        for x in range(15, 44):
            for y in (16, 17):
                if L.on(hd, x, y) and L.get(x, y) in (P["skin"], P["skin_d"]):
                    L.px(x, y, "skin_d")
    return L.im


def draw_mouth(L, kind):
    if kind == "smile":
        L.line([(21, 36), (28, 36)], "ol"); L.px(29, 35, "ol")
        L.line([(22, 37), (27, 37)], "lip")
    elif kind == "flat":
        L.line([(22, 36), (28, 36)], "ol")
    elif kind == "grin":
        L.part(L.mask(("polygon", pts((20, 35), (30, 34), (28, 39), (22, 39)))), lambda x, y: "mouth")
        L.line([(21, 35), (28, 35)], "teeth")
        L.line([(23, 38), (26, 38)], "lip")
    elif kind == "open":
        L.part(L.mask(("ellipse", (21, 35, 28, 40))), lambda x, y: "mouth")
        L.line([(23, 38), (26, 38)], "lip")
    elif kind == "half":
        L.part(L.mask(("ellipse", (21, 35, 28, 38))), lambda x, y: "mouth")
    elif kind == "o":
        L.part(L.mask(("ellipse", (23, 35, 27, 39))), lambda x, y: "mouth")
    elif kind == "wavy":
        for i, x in enumerate(range(21, 29)):
            L.px(x, 36 + (i // 2) % 2, "ol")


# ------------------------------------------------------------------ hats


def straw_hat():
    L = Layer()
    straw = lambda x, y, base: "straw_d" if (x * 3 + y * 2) % 7 == 0 and base == "straw" else base
    brim = L.mask(("ellipse", (5, 7, 54, 16)))
    L.part(brim, lambda x, y: straw(x, y, "straw_d" if x >= 44 else "straw"))
    crown = L.mask(("polygon", pts((13, 12), (16, 3), (21, 0), (37, 0), (42, 3), (45, 12))))
    L.part(crown, lambda x, y: straw(x, y, "straw_d" if x >= 39 else ("straw_h" if x <= 17 else "straw")))
    L.line([(22, 1), (36, 1)], "straw_d")
    L.line([(29, 2), (30, 6)], "straw_d")
    for y in range(9, 12):
        for x in range(14, 45):
            if L.on(crown, x, y):
                L.px(x, y, "band" if x < 39 else "band_d")
    L.line([(14, 12), (44, 12)], "ol")
    L.d.arc((5, 7, 54, 16), 25, 155, fill=P["ol"])
    L.line([(12, 14), (47, 14)], "straw")
    return L.im


def gold_crown():
    """The DevelopKings crown, sitting on the hair."""
    L = Layer()
    c = L.mask(("polygon", pts((17, 13), (16, 1), (23, 7), (29, -1), (35, 7), (42, 1), (41, 13))))
    L.part(c, lambda x, y: "gold_d" if x >= 37 or y >= 12 else ("gold_h" if x <= 18 else "gold"))
    L.line([(18, 10), (40, 10)], "gold_d")
    for x in (21, 29, 37):
        L.px(x, 11, "gem"); L.px(x + 1, 11, "gem")
    L.px(29, 3, "gold_h")
    return L.im


# ------------------------------------------------------------------ composition

W, H = 96, 61     # GIF canvas: 2x fits the 203x123 home slot, 1x the 135x82 slots
OX = 16           # where the 64-pixel character sits in the canvas


def compose(dy=0, head_dx=0, head_dy=0, hat="straw", hat_dx=0, hat_dy=0, hat_rot=0, arm=False, **face):
    """Return the mirrored character on a W x H RGBA canvas. Offsets are in the final
    (facing right) space: positive head_dx moves the head to the viewer's right."""
    c = Image.new("RGBA", (W, 64 + 4), (0, 0, 0, 0))
    b = body()
    c.alpha_composite(b, (OX, dy))
    face.setdefault("brim_shadow", hat == "straw" and hat_dy >= -2)
    h = head(**face)
    hx, hy = OX - head_dx, dy + head_dy           # pre-mirror x runs the other way
    c.alpha_composite(h, (hx, hy))
    if arm:
        c.alpha_composite(arm_listen(), (OX, dy))
    if hat:
        layer = straw_hat() if hat == "straw" else gold_crown()
        if hat_rot:
            layer = layer.rotate(-hat_rot, resample=Image.NEAREST, center=(29, 10))
        x, y = hx - hat_dx, hy + hat_dy + (3 if hat == "crown" else 0)   # the crown sits on the hair
        if y < 0:          # flying off the top
            layer = layer.crop((0, -y, N, N))
            y = 0
        c.alpha_composite(layer, (x, y))
    c = c.transpose(Image.FLIP_LEFT_RIGHT)
    return c.crop((0, 0, W, H))
