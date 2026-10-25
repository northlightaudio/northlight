"""Packages Orbit's renders (render_all.py's folder) as assets/orbit: the deck whole, the
lit panel, and the small parts cut from their layers, each at twice the face's size (sharp
at 200%); the reels' turning frames at the face's size (they move).

    python package_assets.py <renders folder>

The cuts follow src/ui/strip/OrbitFace.h: move a part in both.
"""
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "assets", "orbit")
S = 2  # the renders are twice the face's size

LIT_TOP = 340                      # lit.jpg is the panel from here down
LAMP = (496, 140, 34)              # centre and side of the lamp's patch
METERS = ((127, 414), (333, 414))  # the dials' centres
COVER = (178 + 8, 106 + 8)         # a dial and its frame
KNOBS = {"input": (515, 415, 64), "bias": (515, 520, 44), "hiss": (934, 410, 44),
         "flutter": (934, 497.5, 44), "output": (1063, 415, 64), "mix": (1027, 520, 44)}
CAMERA = 40.0  # units in front of the face; each size of knob's cap stands this high (units):
CAP = {64: 0.465, 44: 0.385}


def on_cap(x, y, side):
    """Where a knob's pointer lands in the render: its cap, seen in perspective."""
    k = CAMERA / (CAMERA - CAP[side])
    return 580 + (x - 580) * k, 300 + (y - 300) * k


REELS = {"left": (190, 165), "right": (970, 165)}
REEL_SIDE = 300
FRAMES = 30


def box(cx, cy, w, h, scale=S, top=0):
    x0, y0 = round((cx - w / 2) * scale), round((cy - h / 2 - top) * scale)
    return x0, y0, x0 + round(w * scale), y0 + round(h * scale)


def save(img, name):
    path = os.path.join(OUT, name)
    if name.endswith(".jpg"):
        img.convert("RGB").save(path, quality=90, optimize=True)
    else:
        img.save(path, optimize=True)


def main():
    src = sys.argv[1]
    load = lambda name: Image.open(os.path.join(src, name))
    os.makedirs(OUT, exist_ok=True)
    deck, lit = load("deck.png").convert("RGB"), load("lit.png").convert("RGB")
    assert deck.size == (2320, 1200), deck.size
    save(deck, "deck.jpg")
    save(lit.crop((0, LIT_TOP * S, 2320, 1200)), "lit.jpg")
    for name in ("pressed", "litpressed"):  # every key held in, unlit and lit: the panel only
        save(load(f"{name}.png").convert("RGB").crop((0, LIT_TOP * S, 2320, 1200)), f"{name}.jpg")
    cx, cy, side = LAMP
    save(lit.crop(box(cx, cy, side, side)), "lamp-lit.png")
    pointers = load("pointers.png").convert("RGBA")
    for name, (x, y, side) in KNOBS.items():
        save(pointers.crop(box(*on_cap(x, y, side), side, side)), f"pointer-{name}.png")
    hood, glass = load("hood.png").convert("RGBA"), load("glass.png").convert("RGBA")
    for side_name, (x, y) in zip(("left", "right"), METERS):
        save(hood.crop(box(x, y, *COVER)), f"hood-{side_name}.png")
        save(glass.crop(box(x, y, *COVER)), f"glass-{side_name}.png")
    for k in range(FRAMES):
        frame = load(f"reel-{k:02d}.png").convert("RGB")  # the face's top, at 1x
        for side_name, (x, y) in REELS.items():
            save(frame.crop(box(x, y, REEL_SIDE, REEL_SIDE, scale=1)), f"reel-{side_name}-{k:02d}.jpg")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
