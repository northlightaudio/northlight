"""The front panel's wear, painted where it would really be, as a map in the board's own
coordinates (1160 x 600 page px, drawn at 2 px per page px). Channels:

  R  rubbing: rings round every knob and switch where fingers grip, a few fine scratches
  G  grime: settled along the outer edges and the trim seam
  B  chips: the enamel knocked off down to primer, mostly at corners and edges
  A  rust: a faint bloom round each chip

Run: python panel_wear.py <out.png>
"""
import math
import random
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1]
S = 2                                     # map px per page px
W, H = 1160 * S, 600 * S
SEAM = 264 * S                            # the trim's centre line
rng = random.Random(11)
np_rng = np.random.default_rng(11)

# Where things are on the page (centre x, centre y, radius of what the hand grips).
KNOBS = [(110, 354, 48), (278, 354, 48), (436, 354, 48), (594, 354, 48)] + [(76 + 126 * i, 504, 28) for i in range(9)]
TOGGLES = [(832, 351, 28), (958, 351, 28), (1084, 351, 28)]


def smooth_noise(scale, octaves=4, seed=0):
    """Value noise, summed octaves, 0..1, full map size."""
    g = np.random.default_rng(seed)
    acc = np.zeros((H, W))
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cells = max(2, int(scale * 2 ** o))
        small = g.random((max(2, cells * H // W), cells))
        layer = np.asarray(Image.fromarray((small * 255).astype(np.uint8)).resize((W, H), Image.BICUBIC)).astype(float) / 255
        acc += layer * amp
        total += amp
        amp *= 0.5
    return acc / total


yy, xx = np.mgrid[0:H, 0:W].astype(float)

# ----- R: rubbing round the controls -------------------------------------------------------
rub = np.zeros((H, W))
n1 = smooth_noise(60, 4, 1)
for cx, cy, r in KNOBS + TOGGLES:
    d = np.hypot(xx - cx * S, yy - cy * S) / S - r
    patch = np.clip(1 - np.maximum(d, 0) / 24, 0, 1) ** 2            # soft, fading a finger's width out
    rub = np.maximum(rub, patch * (0.15 + 0.6 * n1) * 0.6)
img = Image.new("L", (W, H), 0)
dr = ImageDraw.Draw(img)
for _ in range(36):                                                   # fine scratches, mostly near controls
    cx, cy, r = rng.choice(KNOBS + TOGGLES)
    a = rng.uniform(0, 2 * math.pi)
    rr = (r + rng.uniform(4, 30)) * S
    x0, y0 = cx * S + rr * math.cos(a), cy * S + rr * math.sin(a)
    t = rng.uniform(0, math.pi)
    ln = rng.uniform(8, 40) * S
    dr.line([(x0, y0), (x0 + ln * math.cos(t), y0 + ln * math.sin(t))], fill=rng.randint(40, 100), width=1)
rub = np.maximum(rub, np.asarray(img).astype(float) / 255)

# ----- G: grime at the outer edges and the seam ----------------------------------------------
edge = np.minimum.reduce([xx, W - 1 - xx, yy, H - 1 - yy]) / S       # page px to the board's edge
seam = np.abs(yy - SEAM) / S - 4                                     # page px from the trim's edge
n2 = smooth_noise(18, 5, 2)
grime = np.clip(1 - edge / 16, 0, 1) ** 2 * (0.5 + 0.5 * n2)
grime = np.maximum(grime, np.clip(1 - seam / 10, 0, 1) ** 2 * (0.35 + 0.4 * n2))
grime = np.maximum(grime, np.clip(1 - (H - 1 - yy) / S / 40, 0, 1) ** 2 * 0.35 * n2)  # dust along the foot

# ----- B, A: chips and their rust --------------------------------------------------------------
chips = Image.new("L", (W, H), 0)
cd = ImageDraw.Draw(chips)


def chip(x, y, size):
    pts = []
    k = rng.randint(7, 11)
    for i in range(k):
        a = 2 * math.pi * i / k + rng.uniform(-0.25, 0.25)
        rr = size * rng.uniform(0.45, 1.0)
        pts.append((x + rr * math.cos(a), y + rr * math.sin(a)))
    cd.polygon(pts, fill=255)


for cx, cy in ((0, 0), (W, 0), (0, H), (W, H)):                     # the corners take knocks
    for _ in range(rng.randint(1, 2)):
        chip(abs(cx - rng.uniform(4, 30) * S), abs(cy - rng.uniform(4, 30) * S), rng.uniform(2, 6) * S)
for _ in range(11):                                                  # and the edges
    side = rng.choice("tblr")
    t = rng.random()
    off = rng.uniform(2, 14) * S
    x, y = {"t": (t * W, off), "b": (t * W, H - off), "l": (off, t * H), "r": (W - off, t * H)}[side]
    chip(x, y, rng.uniform(1.2, 4) * S)
for _ in range(4):                                                   # the seam, where the trim was fitted
    chip(rng.uniform(0, W), SEAM + rng.choice((-1, 1)) * rng.uniform(5, 9) * S, rng.uniform(1, 2.5) * S)
for cx, cy, r in rng.sample(TOGGLES + KNOBS[4:], 2):                # a few by the controls
    a = rng.uniform(0, 2 * math.pi)
    chip(cx * S + (r + 12) * S * math.cos(a), cy * S + (r + 12) * S * math.sin(a), rng.uniform(1, 2) * S)
chip_a = np.asarray(chips).astype(float) / 255
rust = np.asarray(chips.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(4 * S))).astype(float) / 255
rust = np.clip(rust * 1.6 - chip_a, 0, 1) * (0.5 + 0.5 * smooth_noise(40, 3, 3))

out = np.dstack([rub, grime, chip_a, rust])
Image.fromarray((np.clip(out, 0, 1) * 255).round().astype(np.uint8), "RGBA").save(OUT)
print("wear map", OUT, W, H)
