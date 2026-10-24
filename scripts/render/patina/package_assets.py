"""Packages Patina's renders as assets/patina: each at twice the face's size (sharp at
200%), the small parts' shadows faded out before the frame's edge, the lit button's
spill added as a soft amber glow round it, the panel as a JPEG.

    python package_assets.py <renders folder>

The folder holds the renders under the names the README's commands give them.
"""
import os
import sys

from PIL import Image, ImageEnhance

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "corona"))
from package_assets import fade_edges, spill  # noqa: E402  (Corona's)

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "assets", "patina")


def main(src):
    os.makedirs(OUT, exist_ok=True)

    def load(name):
        return Image.open(os.path.join(src, name)).convert("RGBA")

    def save(img, name):
        path = os.path.join(OUT, name)
        if name.endswith(".jpg"):
            img.convert("RGB").save(path, quality=90, optimize=True, progressive=True)
        else:
            img.save(path, optimize=True)
        print("wrote", path, img.size)

    # The plates are rendered at their asset size already.
    save(load("panel.png"), "panel.jpg")
    for name in ["master", "window", "display"] + ["module-%d" % i for i in range(1, 7)]:
        save(load(name + ".png"), name + ".png")
    # The steel takes a green cast from the room's light: most of it taken out,
    # so it reads as steel against the green panel.
    save(ImageEnhance.Color(load("nameplate.png")).enhance(0.45), "nameplate.png")

    # The knob at Corona's size: the body 640 x 960, the pointer 640 square.
    save(fade_edges(load("knob-body.png").resize((640, 960), Image.LANCZOS)), "knob-body.png")
    save(load("knob-pointer.png").resize((640, 640), Image.LANCZOS), "knob-pointer.png")

    # The button at 240 x 180 (1.2 x 0.9 units, drawn 60 x 45), its lit lens's light
    # falling on the plate round it.
    off = fade_edges(load("button-off.png").resize((240, 180), Image.LANCZOS), 0.12)
    on = load("button-on.png").resize((240, 180), Image.LANCZOS)
    on = fade_edges(spill(on, (120, 90), 52, colour=(255, 150, 50), strength=0.4), 0.12)
    save(off, "button-off.png")
    save(on, "button-on.png")


if __name__ == "__main__":
    main(sys.argv[1])
