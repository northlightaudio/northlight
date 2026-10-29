# Lumen's face

The images Lumen's card draws its face with (screens/plugins.md 7.4i; design-system.md
"Rendered parts"): the front of a 1970s suitcase electric piano. All were rendered for
Northlight in Blender 4.2 from a model made for it, by the scripts in
`scripts/render/lumen`, which say how. The reflections are of a Poly Haven HDRI (Lythwood
Lounge, CC0). Nothing in them comes from a photograph or another product, they carry no
maker's marks, and no words are in them: the card prints them. The vibrato's lever is the
tape echo's render (`assets/tapeecho`).

They are kept at twice the face's drawn size (1160 x 600), so they are sharp at 200%; the
tines' frames at the drawn size, since they are only seen moving.

| File | What it is |
|---|---|
| `deck.jpg` | The whole face, still: the tolex case, the harp window with the tines at rest, the nameplate, the preamp plate, the knob bodies, the lamps dark |
| `lit.jpg` | The same with the pilot lamp and the vibrato's two lamps lit: a lit lamp draws its patch of it, as bright as it is lit |
| `pointer-NN.png` | Each knob's pointer line, pointing up, as colour with alpha: the layer that turns (the plate's knobs in `src/ui/strip/LumenFace.h`'s order, then TUNE, AGE, VOICES) |
| `tine-N.jpg` | The harp window and its bezel, the tines bent from fully down (0) to fully up (8); 4 is at rest |

The card falls back to flat token colours when a file is missing (the tests run that way),
so the face never depends on them to work.
