#!/usr/bin/env python3
"""Writes the Northlight brand files in assets/brand from one definition of the mark.

The mark (docs/design/brand.md) is Moonrise, a half moon over the horizon with its reflection, on a 64-unit
grid. The wordmark is "Northlight" in Newsreader, turned into outlines so the files
need no font. The app icon PNGs are drawn from the same geometry.

Needs fontTools and Pillow, and the Newsreader variable font (SIL Open Font
License) from https://github.com/google/fonts/tree/main/ofl/newsreader:

    python scripts/brand-assets.py --font path/to/Newsreader[opsz,wght].ttf
"""

import argparse
import pathlib

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "brand"

# ----- the mark, on a 64-unit grid -------------------------------------------------------------
# Moonrise: a half moon risen over the horizon, its light broken on the water
# below in two fading strokes (which also read, quietly, as levels).
MOON = (32.0, 28.0, 15.0)  # the centre of the moon's flat base, and its radius
HORIZON = ((7.0, 35.0), (57.0, 35.0))
STROKE = 4.5
# The reflection: (x0, x1, y, opacity), drawn in the moon's colour.
REFLECTION = ((22.0, 42.0, 43.0, 0.55), (27.0, 37.0, 50.0, 0.30))
REFLECTION_STROKE = 3.5
BASELINE = 35.0  # the wordmark sits on the horizon line
WORDMARK_HEIGHT, WORDMARK_BASELINE = 48.0, 36.0  # the wordmark on its own

# Colours from docs/design/tokens.json (Machair dusk).
ON_DARK = {"moon": "#F1EEDF", "horizon": "#93ABC2", "text": "#E8E5DA"}
ON_LIGHT = {"moon": "#141815", "horizon": "#4F6A84", "text": "#141815"}
MONO = {"moon": "currentColor", "horizon": "currentColor", "text": "currentColor"}
PEAT = (0x14, 0x18, 0x15)
TURF = (0x1C, 0x21, 0x1D)
MOON_RGB = (0xF1, 0xEE, 0xDF)
OVERCAST_RGB = (0x93, 0xAB, 0xC2)


def mark_paths(colours, dx=0.0, dy=0.0):
    cx, cy, r = MOON
    (hx0, hy), (hx1, _) = HORIZON
    out = (
        f'<path d="M{cx - r + dx:g} {cy + dy:g} A{r:g} {r:g} 0 0 1 {cx + r + dx:g} {cy + dy:g} Z" '
        f'fill="{colours["moon"]}"/>'
        f'<path d="M{hx0 + dx:g} {hy + dy:g} H{hx1 + dx:g}" stroke="{colours["horizon"]}" '
        f'stroke-width="{STROKE:g}" stroke-linecap="round"/>'
    )
    for x0, x1, y, opacity in REFLECTION:
        out += (
            f'<path d="M{x0 + dx:g} {y + dy:g} H{x1 + dx:g}" stroke="{colours["moon"]}" '
            f'stroke-width="{REFLECTION_STROKE:g}" stroke-linecap="round" opacity="{opacity:g}"/>'
        )
    return out


def svg(width, height, body, title):
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:g}" height="{height:g}" '
        f'viewBox="0 0 {width:g} {height:g}" role="img" aria-label="{title}"><title>{title}</title>'
        f"{body}</svg>\n"
    )


# ----- the wordmark ----------------------------------------------------------------------------
def wordmark(font_path, text="Northlight", size=40.0, weight=420, optical=36):
    """The text as one SVG path (y down, baseline at 0) and its advance width."""
    font = TTFont(font_path)
    font = instancer.instantiateVariableFont(font, {"wght": weight, "opsz": optical})
    glyphs = font.getGlyphSet()
    cmap = font.getBestCmap()
    scale = size / font["head"].unitsPerEm
    pen = SVGPathPen(glyphs, ntos=lambda v: f"{v:.2f}".rstrip("0").rstrip("."))
    x = 0.0
    for ch in text:
        name = cmap[ord(ch)]
        glyph = glyphs[name]
        glyph.draw(TransformPen(pen, (scale, 0, 0, -scale, x, 0)))
        x += glyph.width * scale
    return pen.getCommands(), x


def lockup(colours, letters, advance, title):
    gap = 18.0
    width = 64 + gap + advance + 4
    body = mark_paths(colours) + (
        f'<path transform="translate({64 + gap:g} {BASELINE:g})" d="{letters}" fill="{colours["text"]}"/>'
    )
    return svg(width, 64, body, title)


def wordmark_svg(colours, letters, advance, title):
    """The wordmark alone: 48 units tall with the baseline at 36 (room for the g)."""
    body = f'<path transform="translate(0 {WORDMARK_BASELINE:g})" d="{letters}" fill="{colours["text"]}"/>'
    return svg(advance, WORDMARK_HEIGHT, body, title)


# ----- the app icon ----------------------------------------------------------------------------
def _stroke(draw, x0, x1, y, width, fill):
    """A horizontal stroke with round caps."""
    draw.rectangle([x0, y - width / 2, x1, y + width / 2], fill=fill)
    for x in (x0, x1):
        draw.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=fill)


def icon_png(size):
    """A rounded peat square with the mark, drawn at 4x and reduced (anti-aliased).
    Below 48 px the reflection is left out: it would only blur the horizon."""
    s = size * 4
    image = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([0, 0, s - 1, s - 1], radius=int(s * 0.22), fill=PEAT, outline=TURF, width=max(1, s // 64))
    # The 64-unit mark inside an 80% square.
    unit = s * 0.80 / 64.0
    o = s * 0.10
    cx, cy, r = MOON
    draw.pieslice([o + (cx - r) * unit, o + (cy - r) * unit, o + (cx + r) * unit, o + (cy + r) * unit], 180, 360, fill=MOON_RGB)
    (hx0, hy), (hx1, _) = HORIZON
    _stroke(draw, o + hx0 * unit, o + hx1 * unit, o + hy * unit, STROKE * unit, OVERCAST_RGB)
    if size >= 48:
        for x0, x1, y, opacity in REFLECTION:
            layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
            _stroke(ImageDraw.Draw(layer), o + x0 * unit, o + x1 * unit, o + y * unit, REFLECTION_STROKE * unit,
                    MOON_RGB + (int(round(255 * opacity)),))
            image = Image.alpha_composite(image, layer)
    return image.resize((size, size), Image.LANCZOS)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", required=True, help="Newsreader variable font (opsz, wght)")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)

    marks = {
        "northlight-mark.svg": (ON_DARK, "Northlight"),
        "northlight-mark-on-light.svg": (ON_LIGHT, "Northlight"),
        "northlight-mark-mono.svg": (MONO, "Northlight"),
    }
    for name, (colours, title) in marks.items():
        (OUT / name).write_text(svg(64, 64, mark_paths(colours), title), encoding="utf-8")

    letters, advance = wordmark(args.font)
    lockups = {
        "northlight-lockup.svg": ON_DARK,
        "northlight-lockup-on-light.svg": ON_LIGHT,
        "northlight-lockup-mono.svg": MONO,
    }
    for name, colours in lockups.items():
        (OUT / name).write_text(lockup(colours, letters, advance, "Northlight"), encoding="utf-8")

    wordmarks = {
        "northlight-wordmark.svg": ON_DARK,
        "northlight-wordmark-on-light.svg": ON_LIGHT,
        "northlight-wordmark-mono.svg": MONO,
    }
    for name, colours in wordmarks.items():
        (OUT / name).write_text(wordmark_svg(colours, letters, advance, "Northlight"), encoding="utf-8")

    for size in (16, 32, 48, 256, 1024):
        icon_png(size).save(OUT / f"northlight-icon-{size}.png")
    print(f"wrote {len(marks) + len(lockups) + len(wordmarks)} SVGs and 5 icon PNGs to {OUT}")


if __name__ == "__main__":
    main()
