# Patina's hardware face

The images Patina's card draws its face with (screens/plugins.md 7.2a,
design-system.md "Rendered parts"). All were rendered for Northlight in Blender 4.2
from models made for it, by the scripts in `scripts/render/patina`, which list how
each was made; the lighting and the reflections (a Poly Haven HDRI, Lythwood Lounge,
CC0) are Corona's. Nothing in them comes from a photograph or another product, and
none of them carries any lettering: every legend is drawn live by the app. The pilot
lamp is Corona's jewel (`assets/corona`).

They are kept at twice the face's drawn size (1160 x 660), so they are sharp at 200%.
The plates and bezels carry a 12 px margin (24 px in the file) on every side for
their shadow, so each is placed 12 px up and left of its rectangle on the face.

| File | Size | What it is |
|---|---|---|
| `panel.jpg` | 2320 x 1320 | Slate-green hammertone enamel: the panel everything sits on (drawn over the whole face) |
| `module.png` | 332 x 1144 | A stage's plate (142 x 548): grey-green satin enamel, four slotted screws |
| `master.png` | 480 x 1144 | The master plate (216 x 548), the same enamel |
| `nameplate.png` | 648 x 144 | The nameplate (300 x 48): brushed aluminium, a screw at each end |
| `window.png` | 300 x 240 | A stage window's bezel (126 x 96): black crinkle paint, its opening's corners 3 px |
| `display.png` | 648 x 136 | The preset display's bezel (300 x 44), the same |
| `knob-body.png`, `knob-pointer.png` | 640 x 960, 640 x 640 | The knob's body and shadow (a flared bakelite skirt, a fluted body, a spun aluminium cap); its red pointer, which turns |
| `button-off.png`, `button-on.png` | 240 x 180 | A stage's push-button, dark and lit with its glow (drawn 60 x 45 round its 36 x 24 bezel) |

The card falls back to flat token colours when a file is missing (the tests run that
way), so the face never depends on them to work.
