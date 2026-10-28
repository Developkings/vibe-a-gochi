"""A sheet of Hunold's poses, to check the parts: python draw.py → ../preview/poses.png"""
from pathlib import Path

from PIL import Image

from parts import H, W, compose

POSES = [
    dict(),
    dict(eyes_kind="closed", mouth_kind="small", hand="none", sway=((3, 3), (-3, 3))),
    dict(eyes_kind="happy", mouth_kind="open", hand="both", bulb="bulb", dy=-2),
    dict(eyes_kind="spiral", mouth_kind="wavy", hand="none", phase=1, sway=((-4, 0), (4, 0))),
    dict(eyes_kind="heart", mouth_kind="grin", blush=True),
    dict(eyes_kind="open", look=(2, -1), mouth_kind="small", hand="wag", phase=1),
    dict(eyes_kind="wide", mouth_kind="o", hand="up", sway=((-2, 0), (2, 0))),
    dict(eyes_kind="half", look=(-1, 1), mouth_kind="flat", hand="none"),
    dict(eyes_kind="star", mouth_kind="talk", hand="point"),
]
S = 4
cols = 3
out = Image.new("RGB", (cols * (W * S + 6) + 6, ((len(POSES) + cols - 1) // cols) * (H * S + 6) + 6), (40, 40, 48))
for i, p in enumerate(POSES):
    bg = Image.new("RGBA", (W, H), (21, 17, 15, 255))
    bg.alpha_composite(compose(**p))
    out.paste(bg.convert("RGB").resize((W * S, H * S), Image.NEAREST), (6 + (i % cols) * (W * S + 6), 6 + (i // cols) * (H * S + 6)))
dst = Path(__file__).resolve().parents[1] / "preview" / "poses.png"
out.save(dst)
print(dst)
