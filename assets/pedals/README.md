# The guitar pedals' faces

The images the guitar pedals' cards (Gravity, Flare, Comet, Tide, Polaris) draw their
faces with (design-system.md "Rendered parts"). All were rendered for Northlight in
Blender 4.2 from models made for it, by the scripts in `scripts/render/pedals`, which
list how each was made. The reflections are of a Poly Haven HDRI (Lythwood Lounge,
CC0). Nothing in them comes from a photograph or another product, and none carries
any lettering: every legend is drawn live by the app.

They are kept at twice the face's drawn size (360 x 600), so they are sharp at 200%.
The footswitch button and the lamp are Corona's (`assets/corona`).

| File | Size | What it is |
|---|---|---|
| `enclosure-gravity.jpg` | 720 x 1200 | The whole face: the enclosure in cobalt metal-flake on the pedalboard's carpet |
| `enclosure-flare.jpg` | 720 x 1200 | The same in signal orange sand-texture powder coat |
| `enclosure-comet.jpg` | 720 x 1200 | The same in deep green wrinkle finish |
| `enclosure-tide.jpg` | 720 x 1200 | The same in sea-glass gloss enamel |
| `enclosure-polaris.jpg` | 720 x 1200 | The same in graphite hammertone |
| `knob-body.png` | 480 x 720 | The knob's body and its shadow (the flange is 0.533 of the width) |
| `knob-pointer.png` | 320 x 320 | Its white line, which turns, cropped to 1.2 times the flange |
| `toggle-up.png`, `toggle-centre.png`, `toggle-down.png` | 238 x 357 | Corona's chrome bat toggle in each position, toned for the pedals |

The cards fall back to flat token colours when a file is missing (the tests run that
way), so the faces never depend on them to work.
