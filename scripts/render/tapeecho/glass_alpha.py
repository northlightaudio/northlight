"""Turns the glass render (reflections against black, light that adds to what is
under the glass) into colour + alpha for a plain 'over' composite. Light adds in
linear space, so the alpha is solved there: exact over the cream dial (sRGB 0.78),
close over its darker ink."""
import sys
import numpy as np
from PIL import Image

DIAL = float(sys.argv[3]) if len(sys.argv) > 3 else 0.78   # what the glass usually lies over (sRGB)


def lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


src, dst = sys.argv[1], sys.argv[2]
refl = lin(np.asarray(Image.open(src).convert("RGB")).astype(np.float64) / 255.0)
target = srgb(lin(np.float64(DIAL)) + refl)                 # the dial with the reflection added
per_channel = (target - DIAL) / (1 - DIAL)
alpha = np.clip(per_channel.max(axis=2), 0, 1)
safe = np.maximum(alpha, 1e-4)[..., None]
colour = np.clip((target - DIAL * (1 - alpha[..., None])) / safe, 0, 1)
out = np.dstack([colour, alpha]) * 255.0
Image.fromarray(out.round().astype(np.uint8), "RGBA").save(dst, optimize=True)
inside = alpha[alpha > 0.005]
print("glass", dst, "alpha max", round(float(alpha.max()), 3), "median in window", round(float(np.median(inside)), 3))
