"""Packages Corona's renders as assets/corona: each at twice the face's size (sharp at
200%), the small parts' shadows faded out before the frame's edge, the lit jewel's
spill added as a soft red glow round it.

    python package_assets.py <renders folder>

The folder holds the renders under the names the README's commands give them.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "assets", "corona")


def fade_edges(img, frac=0.10):
    """Fades alpha to nothing over the outer `frac` of the frame (fade_edges.py), so a
    shadow's tail never ends in a hard edge."""
    a = np.asarray(img.convert("RGBA")).astype(np.float32)
    h, w = a.shape[:2]
    y, x = np.mgrid[0:h, 0:w]
    edge = np.minimum.reduce([x, y, w - 1 - x, h - 1 - y]).astype(np.float32) / (frac * min(w, h))
    a[..., 3] *= np.clip(edge, 0.0, 1.0) ** 1.5
    return Image.fromarray(a.round().astype(np.uint8), "RGBA")


def spill(img, centre, radius, colour=(255, 40, 20), strength=0.55):
    """The lamp's light falling on the plate round it: a soft glow of the lens's own
    brightness laid under the render."""
    a = np.asarray(img.convert("RGBA")).astype(np.float32) / 255.0
    h, w = a.shape[:2]
    y, x = np.mgrid[0:h, 0:w]
    lens = (np.hypot(x - centre[0], y - centre[1]) < radius).astype(np.float32)
    glow = Image.fromarray((lens * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(radius * 0.9))
    g = np.asarray(glow).astype(np.float32) / 255.0 * strength
    g = np.clip(g * 1.6, 0, strength)
    under = np.zeros_like(a)
    under[..., :3] = np.array(colour, np.float32) / 255.0
    under[..., 3] = g
    # img over glow
    ao = a[..., 3:4] + under[..., 3:4] * (1 - a[..., 3:4])
    co = (a[..., :3] * a[..., 3:4] + under[..., :3] * under[..., 3:4] * (1 - a[..., 3:4])) / np.maximum(ao, 1e-6)
    out = np.dstack([co, ao])
    return Image.fromarray((np.clip(out, 0, 1) * 255).round().astype(np.uint8), "RGBA")


def tone(img, exposure=0.72, knee=0.5, squeeze=0.55, tint=(1.0, 1.0, 1.0)):
    """Brings a chrome part down to the panel's light: lower exposure, and the
    highlights above `knee` compressed, so its speculars sit with the rest of the
    face instead of glaring (and sparkling when drawn small). `tint` warms it
    toward the plate's brass (aged nickel rather than cold steel)."""
    a = np.asarray(img.convert("RGBA")).astype(np.float32) / 255.0
    rgb = a[..., :3] * exposure * np.array(tint, np.float32)
    over = np.maximum(rgb - knee, 0.0)
    rgb = np.minimum(rgb, knee) + over * squeeze
    a[..., :3] = rgb
    return Image.fromarray((np.clip(a, 0, 1) * 255).round().astype(np.uint8), "RGBA")


def save(img, name, size):
    if img.size != size:
        img = img.resize(size, Image.LANCZOS)
    path = os.path.join(OUT, name)
    if name.endswith(".jpg"):
        img.convert("RGB").save(path, quality=88, optimize=True, progressive=True)
    else:
        img.save(path, optimize=True)
    print(f"{name:20s} {size[0]} x {size[1]}  {os.path.getsize(path) // 1024} KB")


def main():
    src = sys.argv[1]

    def open_(name, mode="RGBA"):
        return Image.open(os.path.join(src, name)).convert(mode)

    os.makedirs(OUT, exist_ok=True)
    save(open_("tolex.png", "RGB"), "tolex.jpg", (2320, 1200))
    save(open_("panel.png"), "panel.png", (2288, 548))
    save(open_("cab-panel.png"), "cab-panel.png", (888, 976))
    save(open_("grille.png"), "grille.png", (2288, 444))
    save(open_("badge.png"), "badge.png", (560, 160))
    save(open_("strip.png"), "strip.png", (2288, 232))
    save(open_("baffle.png"), "baffle.png", (1408, 976))
    for size in ("12", "10", "15"):
        save(open_(f"speaker-{size}.png"), f"speaker-{size}.png", (800, 800))
    save(fade_edges(open_("knob-body.png")), "knob-body.png", (640, 960))
    save(open_("knob-pointer.png"), "knob-pointer.png", (640, 640))
    for state in ("up", "down"):
        # The toggles sit on the champagne plate: warmed toward its brass.
        save(fade_edges(tone(open_(f"toggle-{state}.png"), exposure=1.0, knee=0.62, tint=(1.0, 0.94, 0.82))),
             f"toggle-{state}.png", (238, 357))
        save(fade_edges(tone(open_(f"button-{state}.png"))), f"button-{state}.png", (160, 160))
    save(fade_edges(open_("jewel-off.png")), "jewel-off.png", (130, 150))
    lit = spill(open_("jewel-lit.png"), (195, 195), 78)
    save(fade_edges(lit), "jewel-lit.png", (130, 150))
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    print("wrote", OUT, total // 1024, "KB")


if __name__ == "__main__":
    main()
