"""Makes the grille cloth's weave as two tiling images for grille_render.py: its colour
and its height. Our own cloth, not any maker's: flat ribbon threads in a 2/2
basketweave, the warp wheat and the weft oxblood, each thread a little different
in tone, with fibres along its length and dark gaps between threads where the
black baffle shows through.

    python weave_texture.py <colour.png> <height.png> [threads] [pitch_px]

One tile is `threads` x `threads` threads (8, a multiple of 4), `pitch_px` pixels
per thread (64).
"""
import sys

import numpy as np
from PIL import Image

colour_out, height_out = sys.argv[1], sys.argv[2]
N = int(sys.argv[3]) if len(sys.argv) > 3 else 8
P = int(sys.argv[4]) if len(sys.argv) > 4 else 64
S = N * P
rng = np.random.default_rng(7)

WHEAT = np.array([0.78, 0.64, 0.45])     # sRGB
WARP_PLY = np.array([0.62, 0.47, 0.32])
OXBLOOD = np.array([0.44, 0.19, 0.14])
WEFT_PLY = np.array([0.60, 0.43, 0.30])
GAP = np.array([0.07, 0.05, 0.045])


def periodic_noise(shape, cells, octaves=4):
    """Tileable value noise (sum of octaves), 0..1."""
    h, w = shape
    out = np.zeros(shape)
    amp, total = 1.0, 0.0
    for o in range(octaves):
        cy, cx = cells[0] * 2 ** o, cells[1] * 2 ** o
        grid = rng.random((cy, cx))
        ys = np.arange(h) * cy / h
        xs = np.arange(w) * cx / w
        y0, x0 = np.floor(ys).astype(int), np.floor(xs).astype(int)
        fy, fx = ys - y0, xs - x0
        fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
        y1, x1 = (y0 + 1) % cy, (x0 + 1) % cx
        y0, x0 = y0 % cy, x0 % cx
        a = grid[np.ix_(y0, x0)]
        b = grid[np.ix_(y0, x1)]
        c = grid[np.ix_(y1, x0)]
        d = grid[np.ix_(y1, x1)]
        top = a + (b - a) * fx[None, :]
        bot = c + (d - c) * fx[None, :]
        out += amp * (top + (bot - top) * fy[:, None])
        total += amp
        amp *= 0.5
    return out / total


y, x = np.mgrid[0:S, 0:S]
warp_i, weft_i = x // P, y // P          # warp threads run vertically (index by column)
u = (x % P + 0.5) / P * 2 - 1            # across a warp thread, -1..1
v = (y % P + 0.5) / P * 2 - 1            # across a weft thread
# Position along each thread within its float of two crossings, 0..1.
along_warp = ((y % (2 * P)) + 0.5) / (2 * P)
along_weft = ((x % (2 * P)) + 0.5) / (2 * P)
warp_over = ((warp_i // 2 + weft_i // 2) % 2) == 0

WIDTH = 0.86                             # a thread fills this much of its pitch; the rest is gap


def ribbon(t):
    """A flat ribbon's cross-section: flat-topped, rounded at its edges, 0 outside."""
    t = np.abs(t) / WIDTH
    return np.where(t < 1, np.sqrt(np.clip(1 - t ** 6, 0, 1)), 0.0)


warp_h = ribbon(u) * (0.65 + 0.35 * np.sin(np.pi * along_warp))
weft_h = ribbon(v) * (0.65 + 0.35 * np.sin(np.pi * along_weft))
top_h = np.where(warp_over, warp_h + 0.25 * (warp_h > 0), weft_h + 0.25 * (weft_h > 0))
under_h = np.where(warp_over, weft_h, warp_h) * 0.55
height = np.maximum(top_h, under_h)
show_warp = np.where(warp_over, warp_h > 0, ~(weft_h > 0) & (warp_h > 0))
show_weft = ~show_warp & ((weft_h > 0) | (warp_h > 0))

# Tone per thread, fibres along each thread, a soft slub.
warp_tone = 1 + 0.07 * (rng.random(N) - 0.5) * 2
weft_tone = 1 + 0.07 * (rng.random(N) - 0.5) * 2
fib_v = periodic_noise((S, S), (N * 1, N * 8), 4)          # streaks along y: warp fibres
fib_h = periodic_noise((S, S), (N * 8, N * 1), 4)          # along x: weft fibres
warp_col = WHEAT[None, None, :] * (warp_tone[warp_i] * (0.86 + 0.28 * fib_v))[..., None]
weft_col = OXBLOOD[None, None, :] * (weft_tone[weft_i] * (0.84 + 0.32 * fib_h))[..., None]
# Each thread is two plies twisted together, so the colours fleck into each other:
# the warp a wheat and a darker wheat, the weft an oxblood and a muted wheat.
twist_warp = (y / P * 5.0 + u * 1.2 + 0.6 * fib_v) % 1.0
twist_weft = (x / P * 5.0 + v * 1.2 + 0.6 * fib_h) % 1.0
ply_warp = (0.65 * (0.5 + 0.5 * np.cos(2 * np.pi * twist_warp)) ** 3)[..., None]
ply_weft = (0.6 * (0.5 + 0.5 * np.cos(2 * np.pi * twist_weft)) ** 3)[..., None]
warp_col = warp_col * (1 - ply_warp) + warp_col * (WARP_PLY / WHEAT)[None, None, :] * ply_warp
weft_col = weft_col * (1 - ply_weft) + weft_col * (WEFT_PLY / OXBLOOD)[None, None, :] * ply_weft
groove = np.where(show_warp, np.abs(twist_warp - 0.5), np.abs(twist_weft - 0.5))
height = height - 0.06 * np.clip(groove * 4 - 1.6, 0, 1) * (height > 0)
col = np.where(show_warp[..., None], warp_col, np.where(show_weft[..., None], weft_col, GAP[None, None, :]))
# Threads darken where they dive under a crossing, and lower threads are in shade.
shade = 0.55 + 0.45 * np.clip(height / 1.25, 0, 1)
col = col * shade[..., None]
col = np.where((height <= 0)[..., None], GAP[None, None, :], col)

fuzz = periodic_noise((S, S), (N * 8, N * 8), 3)
height = np.clip(height / 1.25 + 0.04 * (fuzz - 0.5), 0, 1)

Image.fromarray((np.clip(col, 0, 1) * 255).round().astype(np.uint8), "RGB").save(colour_out)
Image.fromarray((height * 65535).round().astype(np.uint16)).save(height_out)
print("weave", colour_out, height_out, S, "px tile, mean sRGB", (col.reshape(-1, 3).mean(0) * 255).round())
