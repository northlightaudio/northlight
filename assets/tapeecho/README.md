# The tape echo's console

The images the tape echo's card draws its console with (screens/plugins.md 7.2a,
design-system.md "Rendered parts"). All were rendered for Northlight in Blender 4.2
from models made for it, by the scripts in `scripts/render/tapeecho`, which list how
each was made. The reflections are of a Poly Haven HDRI (Lythwood Lounge, CC0); the
cover plate's printing is Barlow Condensed (`assets/fonts`, SIL OFL). Nothing in them
comes from a photograph or another product.

They are kept at twice the face's drawn size (1160 x 600), so they are sharp at 200%.

| File | What it is |
|---|---|
| `panel-cream.jpg`, `panel-sage.jpg` | The enamel front above and below the trim, worn where hands go |
| `trim.png` | The brushed aluminium trim between the panels, with its shadow |
| `display-bezel.png` | The aluminium bezel round the transport's window |
| `transport.jpg` | The tape transport under its smoked lid, lit by its lamps |
| `neon-R.png`, `neon-1.png` ... `neon-4.png` | Each head's neon: its light alone, as colour with alpha |
| `hub-pinch.png`, `hub-tension.png` | The rollers' screw slots and drillings, which turn |
| `perspex.png` | The lid's reflections and scratches |
| `vu-body.png`, `vu-hood.png`, `vu-glass.png` | The meter: its body and lit dial, the pivot's hood, the glass |
| `knob-body.png`, `knob-pointer.png` | A knob's body and shadow; its pointer, which turns |
| `lever-up.png`, `lever-centre.png`, `lever-down.png` | A lever in each position |
| `lamp-off.png`, `lamp-amber.png`, `lamp-red.png` | The loop gain lamp, dark, lit and singing |

The card falls back to flat token colours when a file is missing (the tests run that
way), so the face never depends on them to work.
