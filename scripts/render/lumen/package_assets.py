"""Writes assets/lumen from a folder of renders (render_all.py's output).

    python package_assets.py <renders folder>

deck.jpg and lit.jpg are the still face and the face with its lamps lit (2320 x 1200);
pointer-NN.png each knob's pointer, cut square round where its cap lands (the order of
src/ui/strip/LumenFace.h: the plate's knobs, then the service trims); tine-N.jpg the harp
window's bezel and what is in it, the tines bent from fully down (0) to fully up (8), at
the face's drawn size (they are only seen moving).
"""
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.join(REPO, "assets", "lumen")
WIDTH, HEIGHT = 1160, 600
WINDOW = (30, 22, 620, 244)  # LumenFace.h windowBezel, as (x0, y0, x1, y1)
TINE_FRAMES = 9

# LumenFace.h: knobs (the plate) then serviceKnobs, with their pointers' squares.
PLATE = [(83, 348), (159, 348), (235, 348), (83, 478), (159, 478), (235, 478),
         (325, 348), (325, 478),
         (415, 348), (491, 348), (415, 478), (491, 478),
         (657, 348), (733, 348), (657, 478), (733, 478),
         (823, 348), (899, 348), (823, 478), (899, 478),
         (989, 348), (1065, 348), (1027, 478)]
SERVICE = [(700, 186), (790, 186), (880, 186)]


def on_cap(x, y, service):
    """LumenFace.h onCap: a cap standing proud of the face sits a little further out."""
    camera = 40.0
    standing = 0.3 if service else 0.36
    s = camera / (camera - standing)
    return WIDTH / 2 + (x - WIDTH / 2) * s, 300 + (y - 300) * s


def main():
    src = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    deck = Image.open(os.path.join(src, "deck.png")).convert("RGB")
    deck.save(os.path.join(OUT, "deck.jpg"), quality=92, subsampling=0)
    Image.open(os.path.join(src, "lit.png")).convert("RGB").save(os.path.join(OUT, "lit.jpg"), quality=92,
                                                                 subsampling=0)
    pointers = Image.open(os.path.join(src, "pointers.png")).convert("RGBA")
    places = [(x, y, False, 48) for x, y in PLATE] + [(x, y, True, 42) for x, y in SERVICE]
    for i, (x, y, service, size) in enumerate(places):
        cx, cy = on_cap(x, y, service)
        half = size  # the square is `size` face px, twice that in the render
        box = (round(cx * 2 - half), round(cy * 2 - half), round(cx * 2 + half), round(cy * 2 + half))
        pointers.crop(box).save(os.path.join(OUT, f"pointer-{i:02d}.png"), optimize=True)
    for k in range(TINE_FRAMES):
        frame = Image.open(os.path.join(src, f"tine-{k}.png")).convert("RGB")
        frame.crop(WINDOW).save(os.path.join(OUT, f"tine-{k}.jpg"), quality=92, subsampling=0)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
