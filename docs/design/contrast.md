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
| textTertiary | surface1 | 4.31 | Hints in a card or a list row | 3:1 only |
| textTertiary | surface2 | 3.76 | Units and hints inside a control | 3:1 only |
| textTertiary | surface3 | 3.27 | Units and hints in a hovered control | 3:1 only |
| textTertiary | canvas | 4.93 | Ruler seconds, empty-state hints | 3:1 only |
| textTertiary | material.meterWell | 5.11 | A display's axis labels (micro) | 3:1 only |
| textDisabled | surface1 | 2.09 | Disabled (exempt from the requirement) | n/a |
| accent | surface0 | 7.55 | Active icons, links | yes |
| lineFocus | surface0 | 4.35 | Focus ring (non-text) | yes |
| textOnAccent | accent | 7.55 | Label on an accent fill | yes |
| textOnPrimary | primary | 14.22 | Primary button label | yes |
| textOnPrimary | primaryHover | 15.86 | Primary button label, hovered | yes |
| primary | surface0 | 14.22 | Primary button against the panel (non-text) | yes |
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
| hardware.legend | hardware.panel | 6.74 | Legends on a hardware face's panel | yes |
| hardware.ink | hardware.plate | 8.25 | Nameplate lettering | yes |
| hardware.displayText | hardware.displayCentre | 10.25 | An amber display's text | yes |
| hardware.displayDim | hardware.displayCentre | 5.50 | A display's preset number | 3:1 only |
| hardware.glassInk | hardware.glassEdge | 6.01 | Readings on a lit meter window, at its edge | yes |
| hardware.creamInk | hardware.creamPanel | 9.18 | Names on the console's cream panel | yes |
| hardware.creamInkSoft | hardware.creamPanel | 6.13 | Small legends on the cream panel | yes |
| hardware.sageInk | hardware.sagePanel | 6.65 | Control names on the sage panel | yes |
| hardware.sageInkSoft | hardware.sagePanel | 5.22 | Values and positions on the sage panel | yes |
| hardware.dialInk | hardware.dialFace | 9.64 | The meter's scale | yes |
| hardware.dialRed | hardware.dialFace | 4.67 | The meter's red zone | yes |
| hardware.coronaInk | hardware.coronaPanel | 8.11 | Control names on the amp's plate | yes |
| hardware.patinaInk | hardware.patinaPlate | 7.32 | Control names on Patina's stage plates | yes |
| hardware.patinaInkSoft | hardware.patinaPlate | 4.56 | Values and scales on Patina's stage plates | yes |
| hardware.patinaInk | hardware.patinaNameplate | 10.56 | The name on Patina's nameplate | yes |
| hardware.patinaPanelInk | hardware.patinaPanel | 8.33 | The model line on Patina's panel | yes |
| hardware.patinaPanelInkSoft | hardware.patinaPanel | 5.29 | Small print on Patina's panel | yes |
| hardware.coronaInkSoft | hardware.coronaPanel | 5.79 | Values and scales on the amp's plate | yes |
| hardware.pedalGravityInk | hardware.pedalGravity | 7.42 | Names on Gravity | yes |
| hardware.pedalGravityInkSoft | hardware.pedalGravity | 5.03 | Values on Gravity | yes |
| hardware.pedalFlareInk | hardware.pedalFlare | 4.87 | Names on Flare | yes |
| hardware.pedalFlareInkSoft | hardware.pedalFlare | 4.54 | Values on Flare | yes |
| hardware.pedalCometInk | hardware.pedalComet | 6.81 | Names on Comet | yes |
| hardware.pedalCometInkSoft | hardware.pedalComet | 4.87 | Values on Comet | yes |
| hardware.pedalTideInk | hardware.pedalTide | 8.51 | Names on Tide | yes |
| hardware.pedalTideInkSoft | hardware.pedalTide | 5.82 | Values on Tide | yes |
| hardware.pedalPolarisInk | hardware.pedalPolaris | 10.26 | Names on Polaris | yes |
| hardware.pedalPolarisInkSoft | hardware.pedalPolaris | 6.07 | Values on Polaris | yes |
| hardware.tunerText | hardware.tunerGlass | 16.86 | The tuner's note and cents | yes |
| hardware.tunerInTune | hardware.tunerGlass | 12.01 | The tuner's note in tune | yes |
| hardware.tunerOff | hardware.tunerGlass | 6.42 | The tuner's out-of-tune LED | yes |
| hardware.coronaInk | hardware.coronaBadge | 5.67 | The name on the amp's badge | yes |
| hardware.coronaStripInk | hardware.coronaStrip | 13.15 | Names on the utility strip | yes |
| hardware.coronaStripInkSoft | hardware.coronaStrip | 7.25 | Values on the utility strip | yes |
| hardware.coronaStripInk | hardware.coronaBaffle | 15.07 | A mic's label on the baffle | yes |
| hardware.coronaMarkerA | hardware.coronaCone | 6.86 | Mic A's marker on a speaker cone | yes |
| hardware.coronaMarkerB | hardware.coronaCone | 6.63 | Mic B's marker on a speaker cone | yes |
| hardware.orbitPlateInk | hardware.orbitPlate | 14.03 | Legends on Orbit's transport plate | yes |
| hardware.orbitPlateInk | hardware.orbitPlateDark | 7.53 | TAPE TIME on Orbit's plate in shadow | yes |
| hardware.orbitPlateInkSoft | hardware.orbitPlate | 7.58 | The kind and model line on Orbit's nameplate | yes |
| hardware.orbitPanelInk | hardware.orbitPanelRim | 6.60 | Names on Orbit's panel, where the rim light falls | yes |
| hardware.orbitPanelInkSoft | hardware.orbitPanelRim | 4.76 | Values and rows on Orbit's panel, in the rim light | yes |
| hardware.orbitHeadInk | hardware.orbitHead | 9.07 | The heads' names on Orbit's head block | yes |
| hardware.orbitDialInk | hardware.orbitDial | 4.81 | Orbit's meter scale | yes |
| hardware.orbitDialRed | hardware.orbitDial | 4.72 | Orbit's meter red zone | yes |
| hardware.orbitCounter | hardware.orbitCounterWell | 15.69 | Orbit's tape counter | yes |
| hardware.orbitKeyInk | hardware.orbitKey | 5.20 | A key's label on Orbit, unlit | yes |
| hardware.orbitKeyInkLit | hardware.orbitKeyLit | 8.31 | A key's label on Orbit, lit | yes |

## High contrast

The `highContrast` variant applied over the tokens (Preferences > General > Contrast, or a Windows contrast theme). Every pair it changes; tertiary text must pass 4.5:1 here, disabled text stays exempt.

| Foreground | Background | Ratio | Use | Pass |
|---|---|---|---|---|
| textSecondary | surface0 | 14.22 | Labels on panels | yes |
| textSecondary | surface2 | 11.31 | Secondary text in controls | yes |
| textSecondary | surface3 | 9.85 | Hovered secondary | yes |
| textTertiary | surface0 | 8.00 | Hints, inactive tabs (13 px+ or non-essential) | yes |
| textTertiary | surface1 | 7.29 | Hints in a card or a list row | yes |
| textTertiary | surface2 | 6.37 | Units and hints inside a control | yes |
| textTertiary | surface3 | 5.54 | Units and hints in a hovered control | yes |
| textTertiary | canvas | 8.35 | Ruler seconds, empty-state hints | yes |
| textTertiary | material.meterWell | 8.66 | A display's axis labels (micro) | yes |
| textDisabled | surface1 | 4.31 | Disabled (exempt from the requirement) | n/a |
| hardware.displayDim | hardware.displayCentre | 10.25 | A display's preset number | yes |
| hardware.creamInkSoft | hardware.creamPanel | 9.18 | Small legends on the cream panel | yes |
| hardware.sageInkSoft | hardware.sagePanel | 6.65 | Values and positions on the sage panel | yes |
| hardware.patinaInkSoft | hardware.patinaPlate | 7.32 | Values and scales on Patina's stage plates | yes |
| hardware.patinaPanelInkSoft | hardware.patinaPanel | 8.33 | Small print on Patina's panel | yes |
| hardware.coronaInkSoft | hardware.coronaPanel | 8.11 | Values and scales on the amp's plate | yes |
| hardware.pedalGravityInkSoft | hardware.pedalGravity | 7.42 | Values on Gravity | yes |
| hardware.pedalFlareInkSoft | hardware.pedalFlare | 4.87 | Values on Flare | yes |
| hardware.pedalCometInkSoft | hardware.pedalComet | 6.81 | Values on Comet | yes |
| hardware.pedalTideInkSoft | hardware.pedalTide | 8.51 | Values on Tide | yes |
| hardware.pedalPolarisInkSoft | hardware.pedalPolaris | 10.26 | Values on Polaris | yes |
| hardware.coronaStripInkSoft | hardware.coronaStrip | 13.15 | Values on the utility strip | yes |
| hardware.orbitPlateInkSoft | hardware.orbitPlate | 14.03 | The kind and model line on Orbit's nameplate | yes |
| hardware.orbitPanelInkSoft | hardware.orbitPanelRim | 6.60 | Values and rows on Orbit's panel, in the rim light | yes |

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
