"""Packages the guitar pedals' renders as assets/pedals: each at twice the face's size
(sharp at 200%), the small parts' shadows faded out before the frame's edge, the
toggles' chrome brought down to the pedals' light. Corona's footswitch button and
jewel are drawn from assets/corona as they are.

    python package_assets.py <renders folder>

The folder holds the renders under the names the README's commands give them.
"""
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "corona"))
from package_assets import fade_edges, tone  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "assets", "pedals")
FINISHES = ("gravity", "flare", "comet", "tide", "polaris")


def save(img, name, size):
    if img.size != size:
        img = img.resize(size, Image.LANCZOS)
    path = os.path.join(OUT, name)
    if name.endswith(".jpg"):
        img.convert("RGB").save(path, quality=90, optimize=True, progressive=True)
    else:
        img.save(path, optimize=True)
    print(f"{name:22s} {size[0]} x {size[1]}  {os.path.getsize(path) // 1024} KB")


def main():
    src = sys.argv[1]

    def open_(name, mode="RGBA"):
        return Image.open(os.path.join(src, name)).convert(mode)

    os.makedirs(OUT, exist_ok=True)
    for finish in FINISHES:
        save(open_(f"enclosure-{finish}.png", "RGB"), f"enclosure-{finish}.jpg", (720, 1200))
    save(fade_edges(open_("knob-body.png")), "knob-body.png", (480, 720))
    # The pointer cropped to just past the flange (1.92 of the render's 3.0 units):
    # the knob's bounds, so hovering it repaints no more than the knob.
    pointer = open_("knob-pointer.png")
    side = round(pointer.width * 1.92 / 3.0)
    left = (pointer.width - side) // 2
    save(pointer.crop((left, left, left + side, left + side)), "knob-pointer.png", (320, 320))
    for state in ("up", "centre", "down"):
        save(fade_edges(tone(open_(f"toggle-{state}.png"), exposure=0.95, knee=0.6)), f"toggle-{state}.png",
             (238, 357))
    total = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    print("wrote", OUT, total // 1024, "KB")


if __name__ == "__main__":
    main()
