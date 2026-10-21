# Contrast table

Generated from [tokens.json](tokens.json) (Machair Dusk v2) by `scripts/contrast.py` with the WCAG 2.x relative luminance formula; alpha colours are composited over the named background (which is itself composited over `canvas` when translucent). Thresholds: 4.5:1 for text at `body` and smaller, 3:1 for `title` and larger and for non-text.

| Foreground | Background | Ratio | Use | Pass |
|---|---|---|---|---|
| textPrimary | surface0 | 14.22 | Values, names on panels | yes |
| textPrimary | surface2 | 11.31 | Button labels | yes |
| textPrimary | surface3 | 9.85 | Hovered button labels | yes |
| textSecondary | surface0 | 8.00 | Labels on panels | yes |
| textSecondary | surface2 | 6.37 | Secondary text in controls | yes |
| textSecondary | surface3 | 5.54 | Hovered secondary | yes |
| textTertiary | surface0 | 4.72 | Hints, inactive tabs (13 px+ or non-essential) | 3:1 only |
| textTertiary | canvas | 4.93 | Ruler seconds, empty-state hints | 3:1 only |
| textDisabled | surface1 | 2.09 | Disabled (exempt from the requirement) | n/a |
| accent | surface0 | 7.55 | Active icons, links | yes |
| lineFocus | surface0 | 4.35 | Focus ring (non-text) | yes |
| textOnAccent | accent | 7.55 | Primary button label | yes |
| record | surface0 | 5.49 | Record icon, recording time | yes |
| warning | surface0 | 8.98 | Warnings | yes |
| success | surface0 | 7.67 | Confirmations | yes |
| danger | surface0 | 6.03 | Errors, destructive | yes |
| textOnColour | solo | 9.38 | Label on solo-on | yes |
| textOnColour | mute | 8.35 | Label on mute-on | yes |
| textOnColour | arm | 5.73 | Label on armed | yes |
| rulerText | surface0 | 8.00 | Bar numbers | yes |
| meterLow | canvas | 6.75 | Meter zone (non-text) | yes |
| meterMid | canvas | 9.38 | Meter zone (non-text) | yes |
| meterHot | canvas | 6.88 | Meter zone (non-text) | yes |

## Track palette

Each hue against `canvas` (for waveforms and stripes) and `textOnColour` on the hue (for name caps at full colour). Every hue must pass 4.5:1 both ways.

| Hue | On canvas | textOnColour on hue |
|---|---|---|
| Overcast | 7.89 | 7.89 |
| Machair | 7.12 | 7.12 |
| Straw | 8.89 | 8.89 |
| Heather | 8.23 | 8.23 |
| Rowan | 6.46 | 6.46 |
| Sea grass | 7.59 | 7.59 |
| Kelp | 5.71 | 5.71 |
| Loch | 6.53 | 6.53 |
| Gorse | 9.43 | 9.43 |
| Thrift | 7.13 | 7.13 |
| Lichen | 10.12 | 10.12 |
| Slate | 7.29 | 7.29 |

Clip bodies at 22% alpha and headers at 55% are not text carriers; clip names are drawn in `textPrimary` on the header, which composites to at least 9:1 on every hue.

Regenerate with `scripts/contrast.py` whenever the token file changes.
