# Orbit's face

The images Orbit's card draws its face with (screens/plugins.md, Orbit; design-system.md
"Rendered parts"). All were rendered for Northlight in Blender 4.2 from a model made for it,
by the scripts in `scripts/render/orbit`, which say how. The reflections are of a Poly Haven
HDRI (Lythwood Lounge, CC0). Nothing in them comes from a photograph or another product, and
no words are in them: the card prints them.

They are kept at twice the face's drawn size (1160 x 600), so they are sharp at 200%; the
reels' turning frames at the drawn size, since they are only seen moving.

| File | What it is |
|---|---|
| `deck.jpg` | The whole face, still: the plate, the reels at rest, the panel, knob bodies, keys unlit |
| `lit.jpg` | The panel from 340 px down with every key lit: a lit key draws its own patch of it |
| `pressed.jpg`, `litpressed.jpg` | The same panel with every key pushed in, unlit and lit: a key held down draws its patch |
| `lamp-lit.png` | The pilot lamp lit, with the plate round it |
| `pointer-<knob>.png` | Each knob's pointer, pointing up: the layer that turns |
| `hood-left.png`, `hood-right.png` | Each meter's pivot hood over the needle's foot, its bulbs' glow under it in the deck |
| `glass-left.png`, `glass-right.png` | Each meter's glass: its reflections, as colour with alpha |
| `reel-left-NN.jpg`, `reel-right-NN.jpg` | Each reel turned NN x 4 degrees anticlockwise (30 frames span the reel's 120-degree symmetry) |

The card falls back to flat token colours when a file is missing (the tests run that way),
so the face never depends on them to work.
