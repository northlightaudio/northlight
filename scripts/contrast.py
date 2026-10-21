#!/usr/bin/env python3
"""Contrast table for the design tokens (docs/design/11-accessibility-performance.md).

Reads docs/design/tokens.json, composites alpha colours over their background
(which is itself composited over `canvas` when translucent), computes the WCAG 2.x
contrast ratio for every text/surface pair the design uses and for the track
palette, and prints the Markdown table (or writes it with --out). Exit code 1
when a pair that must pass does not, so CI can run it.

    python scripts/contrast.py                 # print the table
    python scripts/contrast.py --out docs/design/contrast.md
    python scripts/contrast.py --check         # exit 1 on a failing pair
"""

import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOKENS = ROOT / "docs" / "design" / "tokens.json"

# (foreground, background, use, text size class): "body" needs 4.5, "large" and
# "nontext" need 3, "hint" is tertiary text (3:1 only), "exempt" is disabled text.
PAIRS = [
    ("textPrimary", "surface0", "Values, names on panels", "body"),
    ("textPrimary", "surface2", "Button labels", "body"),
    ("textPrimary", "surface3", "Hovered button labels", "body"),
    ("textSecondary", "surface0", "Labels on panels", "body"),
    ("textSecondary", "surface2", "Secondary text in controls", "body"),
    ("textSecondary", "surface3", "Hovered secondary", "body"),
    ("textTertiary", "surface0", "Hints, inactive tabs (13 px+ or non-essential)", "hint"),
    ("textTertiary", "canvas", "Ruler seconds, empty-state hints", "hint"),
    ("textDisabled", "surface1", "Disabled (exempt from the requirement)", "exempt"),
    ("accent", "surface0", "Active icons, links", "body"),
    ("lineFocus", "surface0", "Focus ring (non-text)", "nontext"),
    ("textOnAccent", "accent", "Label on an accent fill", "body"),
    ("textOnPrimary", "primary", "Primary button label", "body"),
    ("textOnPrimary", "primaryHover", "Primary button label, hovered", "body"),
    ("primary", "surface0", "Primary button against the panel (non-text)", "nontext"),
    ("record", "surface0", "Record icon, recording time", "body"),
    ("warning", "surface0", "Warnings", "body"),
    ("success", "surface0", "Confirmations", "body"),
    ("danger", "surface0", "Errors, destructive", "body"),
    ("textOnColour", "solo", "Label on solo-on", "body"),
    ("textOnColour", "mute", "Label on mute-on", "body"),
    ("textOnColour", "arm", "Label on armed", "body"),
    ("rulerText", "surface0", "Bar numbers", "body"),
    ("meterLow", "canvas", "Meter zone (non-text)", "nontext"),
    ("meterMid", "canvas", "Meter zone (non-text)", "nontext"),
    ("meterHot", "canvas", "Meter zone (non-text)", "nontext"),
]

THRESHOLDS = {"body": 4.5, "large": 3.0, "nontext": 3.0, "hint": 3.0, "exempt": 0.0}


def parse(hex_colour):
    """#RRGGBB or #RRGGBBAA -> (r, g, b, a) in 0..1."""
    h = hex_colour.lstrip("#")
    if len(h) == 6:
        h += "FF"
    if len(h) != 8:
        raise ValueError(f"not a colour: {hex_colour}")
    r, g, b, a = (int(h[i : i + 2], 16) / 255.0 for i in (0, 2, 4, 6))
    return r, g, b, a


def over(fg, bg):
    """Composites fg (with alpha) over an opaque bg."""
    r, g, b, a = fg
    return tuple(c * a + d * (1.0 - a) for c, d in zip((r, g, b), bg[:3])) + (1.0,)


def luminance(rgb):
    def channel(c):
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4

    r, g, b = (channel(c) for c in rgb[:3])
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(fg, bg):
    lighter, darker = sorted((luminance(fg), luminance(bg)), reverse=True)
    return (lighter + 0.05) / (darker + 0.05)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tokens", default=str(TOKENS))
    parser.add_argument("--out", help="write the Markdown here instead of printing it")
    parser.add_argument("--check", action="store_true", help="exit 1 when a required pair fails")
    args = parser.parse_args()

    tokens = json.loads(pathlib.Path(args.tokens).read_text(encoding="utf-8"))
    colours = tokens["color"]
    name = tokens.get("meta", {}).get("name", "tokens")
    canvas = parse(colours["canvas"])

    def opaque(key):
        return over(parse(colours[key]), canvas)

    lines = [
        "# Contrast table",
        "",
        f"Generated from [tokens.json](tokens.json) ({name} v{tokens.get('meta', {}).get('version', 1)}) by "
        "`scripts/contrast.py` with the WCAG 2.x relative luminance formula; alpha colours are composited "
        "over the named background (which is itself composited over `canvas` when translucent). Thresholds: "
        "4.5:1 for text at `body` and smaller, 3:1 for `title` and larger and for non-text.",
        "",
        "| Foreground | Background | Ratio | Use | Pass |",
        "|---|---|---|---|---|",
    ]
    failures = []
    for fg_key, bg_key, use, kind in PAIRS:
        bg = opaque(bg_key)
        fg = over(parse(colours[fg_key]), bg)
        r = ratio(fg, bg)
        threshold = THRESHOLDS[kind]
        if kind == "exempt":
            verdict = "n/a"
        elif kind == "hint":
            verdict = "3:1 only" if r >= 3.0 else "no"
        else:
            verdict = "yes" if r >= threshold else "no"
        if verdict == "no":
            failures.append((fg_key, bg_key, r, threshold))
        lines.append(f"| {fg_key} | {bg_key} | {r:.2f} | {use} | {verdict} |")

    lines += [
        "",
        "## Track palette",
        "",
        "Each hue against `canvas` (for waveforms and stripes) and `textOnColour` on the hue (for name caps at "
        "full colour). Every hue must pass 4.5:1 both ways.",
        "",
        "| Hue | On canvas | textOnColour on hue |",
        "|---|---|---|",
    ]
    on_colour = parse(colours["textOnColour"])
    for entry in tokens["track"]["palette"]:
        hue = over(parse(entry["value"]), canvas)
        a = ratio(hue, canvas)
        b = ratio(over(on_colour, hue), hue)
        if a < 4.5 or b < 4.5:
            failures.append((entry["name"], "canvas/textOnColour", min(a, b), 4.5))
        lines.append(f"| {entry['name']} | {a:.2f} | {b:.2f} |")
    lines += [
        "",
        "Clip bodies at 22% alpha and headers at 55% are not text carriers; clip names are drawn in "
        "`textPrimary` on the header, which composites to at least 9:1 on every hue.",
        "",
        "Regenerate with `scripts/contrast.py` whenever the token file changes.",
        "",
    ]
    text = "\n".join(lines)
    if args.out:
        pathlib.Path(args.out).write_text(text, encoding="utf-8", newline="\n")
    else:
        sys.stdout.write(text)
    if failures:
        for fg, bg, r, threshold in failures:
            sys.stderr.write(f"FAIL {fg} on {bg}: {r:.2f} < {threshold}\n")
        if args.check:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
