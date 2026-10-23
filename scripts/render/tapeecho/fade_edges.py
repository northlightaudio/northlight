"""Fades a render's alpha to nothing over the outer 10% of the frame, so a shadow's tail never ends in a hard edge."""
import sys
import numpy as np
from PIL import Image

path = sys.argv[1]
im = Image.open(path).convert("RGBA")
a = np.asarray(im).astype(np.float32)
h, w = a.shape[:2]
y, x = np.mgrid[0:h, 0:w]
edge = np.minimum.reduce([x, y, w - 1 - x, h - 1 - y]).astype(np.float32) / (0.10 * min(w, h))
fade = np.clip(edge, 0.0, 1.0) ** 1.5
a[..., 3] *= fade
Image.fromarray(a.astype(np.uint8)).save(path, optimize=True)
print("faded", path, "bottom edge max alpha", int(a[-1, :, 3].max()))
