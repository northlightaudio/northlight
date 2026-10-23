"""Packages the tape echo's renders as assets/tapeecho: each at twice the face's size
(sharp at 200%), the neons' light turned into colour with alpha.

    python package_assets.py <renders folder>

The folder holds the renders under the names the README's commands give them.
"""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "assets", "tapeecho")


def lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def light_to_over(img, base):
    """Light rendered against black (it adds to what is under it) as colour with alpha
    for a plain 'over', solved in linear light for an underlay of sRGB `base`."""
    added = lin(np.asarray(img.convert("RGB")).astype(np.float64) / 255.0)
    target = srgb(lin(np.float64(base)) + added)
    alpha = np.clip(((target - base) / (1 - base)).max(axis=2), 0, 1)
    colour = np.clip((target - base * (1 - alpha[..., None])) / np.maximum(alpha, 1e-4)[..., None], 0, 1)
    return Image.fromarray((np.dstack([colour, alpha]) * 255).round().astype(np.uint8), "RGBA")


def save(img, name, size):
    if img.size != size:
        img = img.resize(size, Image.LANCZOS)
    path = os.path.join(OUT, name)
    if name.endswith(".jpg"):
        img.convert("RGB").save(path, quality=90, optimize=True)
    else:
        img.save(path, optimize=True)


def main():
    src = sys.argv[1]
    open_ = lambda name, mode="RGBA": Image.open(os.path.join(src, name)).convert(mode)
    os.makedirs(OUT, exist_ok=True)
    save(open_("panel-cream.jpg", "RGB"), "panel-cream.jpg", (2320, 520))
    save(open_("panel-sage.jpg", "RGB"), "panel-sage.jpg", (2320, 664))
    save(open_("trim.png"), "trim.png", (2320, 40))
    save(open_("display-bezel.png"), "display-bezel.png", (1364, 554))
    save(open_("transport.png", "RGB"), "transport.jpg", (1248, 388))
    save(open_("perspex.png"), "perspex.png", (1248, 388))
    for k in "R1234":
        save(light_to_over(open_(f"neon-{k}.png", "RGB"), 0.2), f"neon-{k}.png", (1248, 388))
    save(open_("hub-pinch.png"), "hub-pinch.png", (40, 40))
    save(open_("hub-tension.png"), "hub-tension.png", (48, 48))
    for name in ("vu-body.png", "vu-hood.png", "vu-glass.png"):
        save(open_(name), name, (560, 460))
    save(open_("knob-body.png"), "knob-body.png", (400, 600))
    save(open_("knob-pointer.png"), "knob-pointer.png", (400, 400))
    for state in ("up", "centre", "down"):
        save(open_(f"lever-{state}.png"), f"lever-{state}.png", (238, 357))
    for state in ("off", "amber", "red"):
        save(open_(f"lamp-{state}.png"), f"lamp-{state}.png", (130, 150))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
