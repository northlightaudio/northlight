#!/usr/bin/env python3
"""Contrast table for the design tokens (docs/design/accessibility-performance.md).

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
    ("textTertiary", "surface1", "Hints in a card or a list row", "hint"),
    ("textTertiary", "surface2", "Units and hints inside a control", "hint"),
    ("textTertiary", "surface3", "Units and hints in a hovered control", "hint"),
    ("textTertiary", "canvas", "Ruler seconds, empty-state hints", "hint"),
    ("textTertiary", "material.meterWell", "A display's axis labels (micro)", "hint"),
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
    # A hardware face (Patina's rack unit): its own group, "hardware.<key>".
    ("hardware.legend", "hardware.panel", "Legends on a hardware face's panel", "body"),
    ("hardware.displayText", "hardware.displayCentre", "An amber display's text", "body"),
    ("hardware.displayDim", "hardware.displayCentre", "A display's preset number", "hint"),
    ("hardware.glassInk", "hardware.glassEdge", "Readings on a lit meter window, at its edge", "body"),
    # The tape echo's console: ink printed on its enamel and on the meter's dial.
    ("hardware.creamInk", "hardware.creamPanel", "Names on the console's cream panel", "body"),
    ("hardware.creamInkSoft", "hardware.creamPanel", "Small legends on the cream panel", "body"),
    ("hardware.sageInk", "hardware.sagePanel", "Control names on the sage panel", "body"),
    ("hardware.sageInkSoft", "hardware.sagePanel", "Values and positions on the sage panel", "body"),
    ("hardware.dialInk", "hardware.dialFace", "The meter's scale", "body"),
    ("hardware.dialRed", "hardware.dialFace", "The meter's red zone", "body"),
    # Corona's amp: ink on its champagne plate and brass badge, legends on the
    # graphite strip and on the cabinet's baffle.
    ("hardware.coronaInk", "hardware.coronaPanel", "Control names on the amp's plate", "body"),
    ("hardware.patinaInk", "hardware.patinaPlate", "Control names on Patina's stage plates", "body"),
    ("hardware.patinaInkSoft", "hardware.patinaPlate", "Values and scales on Patina's stage plates", "body"),
    ("hardware.patinaInk", "hardware.patinaNameplate", "The name on Patina's nameplate", "body"),
    ("hardware.patinaPanelInkSoft", "hardware.patinaPanel", "Small print on Patina's panel", "body"),
    ("hardware.coronaInkSoft", "hardware.coronaPanel", "Values and scales on the amp's plate", "body"),
    # The guitar pedals: names and values printed on each enclosure, the tuner's display.
    ("hardware.pedalGravityInk", "hardware.pedalGravity", "Names on Gravity", "body"),
    ("hardware.pedalGravityInkSoft", "hardware.pedalGravity", "Values on Gravity", "body"),
    ("hardware.pedalFlareInk", "hardware.pedalFlare", "Names on Flare", "body"),
    ("hardware.pedalFlareInkSoft", "hardware.pedalFlare", "Values on Flare", "body"),
    ("hardware.pedalCometInk", "hardware.pedalComet", "Names on Comet", "body"),
    ("hardware.pedalCometInkSoft", "hardware.pedalComet", "Values on Comet", "body"),
    ("hardware.pedalTideInk", "hardware.pedalTide", "Names on Tide", "body"),
    ("hardware.pedalTideInkSoft", "hardware.pedalTide", "Values on Tide", "body"),
    ("hardware.pedalPolarisInk", "hardware.pedalPolaris", "Names on Polaris", "body"),
    ("hardware.pedalPolarisInkSoft", "hardware.pedalPolaris", "Values on Polaris", "body"),
    ("hardware.tunerText", "hardware.tunerGlass", "The tuner's note and cents", "body"),
    ("hardware.tunerInTune", "hardware.tunerGlass", "The tuner's note in tune", "body"),
    ("hardware.tunerOff", "hardware.tunerGlass", "The tuner's out-of-tune LED", "nontext"),
    ("hardware.coronaInk", "hardware.coronaBadge", "The name on the amp's badge", "body"),
    ("hardware.coronaStripInk", "hardware.coronaStrip", "Names on the utility strip", "body"),
    ("hardware.coronaStripInkSoft", "hardware.coronaStrip", "Values on the utility strip", "body"),
    ("hardware.coronaStripInk", "hardware.coronaBaffle", "A mic's label on the baffle", "body"),
    ("hardware.coronaMarkerA", "hardware.coronaCone", "Mic A's marker on a speaker cone", "nontext"),
    ("hardware.coronaMarkerB", "hardware.coronaCone", "Mic B's marker on a speaker cone", "nontext"),
    ("hardware.orbitPlateInk", "hardware.orbitPlate", "Legends on Orbit's transport plate", "body"),
    ("hardware.orbitPlateInk", "hardware.orbitPlateDark", "TAPE TIME on Orbit's plate in shadow", "body"),
    ("hardware.orbitPlateInkSoft", "hardware.orbitPlate", "The kind and model line on Orbit's nameplate", "body"),
    ("hardware.orbitPanelInk", "hardware.orbitPanelRim", "Names on Orbit's panel, where the rim light falls", "body"),
    ("hardware.orbitPanelInkSoft", "hardware.orbitPanelRim", "Values and rows on Orbit's panel, in the rim light", "body"),
    ("hardware.orbitHeadInk", "hardware.orbitHead", "The heads' names on Orbit's head block", "body"),
    ("hardware.orbitDialInk", "hardware.orbitDial", "Orbit's meter scale", "body"),
    ("hardware.orbitDialRed", "hardware.orbitDial", "Orbit's meter red zone", "body"),
    ("hardware.orbitCounter", "hardware.orbitCounterWell", "Orbit's tape counter", "body"),
    ("hardware.orbitKeyInk", "hardware.orbitKey", "A key's label on Orbit, unlit", "body"),
    ("hardware.orbitKeyInkLit", "hardware.orbitKeyLit", "A key's label on Orbit, lit", "body"),
    ("hardware.lumenPlateInk", "hardware.lumenPlate", "Names and captions on Lumen's preamp plate", "body"),
    ("hardware.lumenPlateInk", "hardware.lumenPlateDark", "Names on Lumen's plate where it is in shadow", "body"),
    ("hardware.lumenPlateInkSoft", "hardware.lumenPlate", "Values and positions on Lumen's plate", "body"),
    ("hardware.lumenCaseInk", "hardware.lumenCase", "The service trims' names on Lumen's case", "body"),
    ("hardware.lumenCaseInkSoft", "hardware.lumenCase", "The service trims' values on Lumen's case", "body"),
    ("hardware.lumenNameplateInk", "hardware.lumenNameplate", "LUMEN on its nameplate", "body"),
    ("hardware.lumenNameplateInkSoft", "hardware.lumenNameplate", "The kind and model line on Lumen's nameplate", "body"),
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
    variant = tokens.get("highContrast", {})
    name = tokens.get("meta", {}).get("name", "tokens")
    canvas = parse(colours["canvas"])

    def colour(key, high=False):
        # "hardware.legend" is a key of another group; a plain key is one of `color`.
        # With `high`, the highContrast variant's value wins where it has one.
        group, _, name = key.rpartition(".")
        group = group or "color"
        if high and name in variant.get(group, {}):
            return parse(variant[group][name])
        return parse(tokens[group][name])

    def opaque(key, high=False):
        return over(colour(key, high), canvas)

    def changed(key):
        group, _, name = key.rpartition(".")
        return name in variant.get(group or "color", {})

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
        fg = over(colour(fg_key), bg)
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

    # High contrast: every pair the variant changes. Its text carries essential
    # information at any size, so tertiary text must reach 4.5:1 here.
    if variant:
        lines += [
            "",
            "## High contrast",
            "",
            "The `highContrast` variant applied over the tokens (Preferences > General > Contrast, or a Windows "
            "contrast theme). Every pair it changes; tertiary text must pass 4.5:1 here, disabled text stays exempt.",
            "",
            "| Foreground | Background | Ratio | Use | Pass |",
            "|---|---|---|---|---|",
        ]
        for fg_key, bg_key, use, kind in PAIRS:
            if not (changed(fg_key) or changed(bg_key)):
                continue
            kind = "body" if kind == "hint" else kind
            bg = opaque(bg_key, True)
            fg = over(colour(fg_key, True), bg)
            r = ratio(fg, bg)
            if kind == "exempt":
                verdict = "n/a"
            else:
                verdict = "yes" if r >= THRESHOLDS[kind] else "no"
            if verdict == "no":
                failures.append((fg_key + " (high)", bg_key, r, THRESHOLDS[kind]))
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
