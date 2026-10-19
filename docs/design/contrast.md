# Contrast table

Generated from [tokens.json](tokens.json) (Aural Dark v1) with the WCAG 2.x relative
luminance formula; alpha colours are composited over the named background (which is
itself composited over `canvas` when translucent). Thresholds: 4.5:1 for text at
`body` and smaller, 3:1 for `title` and larger and for non-text.

| Foreground | Background | Ratio | Use | Pass |
|---|---|---|---|---|
| textPrimary | surface0 | 15.32 | Values, names on panels | yes |
| textPrimary | surface2 | 12.78 | Button labels | yes |
| textPrimary | surface3 | 11.29 | Hovered button labels | yes |
| textSecondary | surface0 | 6.96 | Labels on panels | yes |
| textSecondary | surface2 | 5.81 | Secondary text in controls | yes |
| textSecondary | surface3 | 5.13 | Hovered secondary | yes |
| textTertiary | surface0 | 3.58 | Hints, inactive tabs (13 px+ or non-essential) | 3:1 only |
| textTertiary | canvas | 3.80 | Ruler seconds, empty-state hints | 3:1 only |
| textDisabled | surface1 | 1.92 | Disabled (exempt from the requirement) | n/a |
| accent | surface0 | 7.26 | Active icons, links | yes |
| lineFocus | surface0 | 4.19 | Focus ring (non-text) | yes |
| textOnAccent | accent | 7.61 | Primary button label | yes |
| record | surface0 | 5.56 | Record icon, recording time | yes |
| warning | surface0 | 10.24 | Warnings | yes |
| success | surface0 | 9.20 | Confirmations | yes |
| danger | surface0 | 5.97 | Errors, destructive | yes |
| textOnColour | solo | 11.42 | Label on solo-on | yes |
| textOnColour | mute | 7.71 | Label on mute-on | yes |
| textOnColour | arm | 5.89 | Label on armed | yes |
| rulerText | surface0 | 6.96 | Bar numbers | yes |
| meterLow | canvas | 9.03 | Meter zone (non-text) | yes |
| meterMid | canvas | 11.67 | Meter zone (non-text) | yes |
| meterHot | canvas | 8.17 | Meter zone (non-text) | yes |

## Track palette

Each hue against `canvas` (for waveforms and stripes) and `textOnColour` on the hue
(for name caps at full colour). All twelve pass 4.5:1 both ways.

| Hue | On canvas | textOnColour on hue |
|---|---|---|
| Coral | 6.90 | 6.90 |
| Tangerine | 8.80 | 8.80 |
| Amber | 11.42 | 11.42 |
| Lime | 11.43 | 11.43 |
| Mint | 10.01 | 10.01 |
| Teal | 9.22 | 9.22 |
| Sky | 8.52 | 8.52 |
| Indigo | 6.48 | 6.48 |
| Violet | 7.04 | 7.04 |
| Magenta | 7.51 | 7.51 |
| Rose | 8.03 | 8.03 |
| Slate | 7.71 | 7.71 |

Clip bodies at 22% alpha and headers at 55% are not text carriers; clip names are
drawn in `textPrimary` on the header, which composites to at least 9:1 on every hue.

Regenerate with `scripts/contrast.py` (to be added in Phase 7) whenever the token
file changes.
