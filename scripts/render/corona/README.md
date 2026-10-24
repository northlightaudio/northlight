# Corona's renders

The Blender scripts that render Corona's hardware face (the amp page and the cabinet
page; design-system.md "Rendered parts"), and the packaging that turns the renders
into `assets/corona`. Re-run them to change a part; nothing in the app runs them.

Needs Blender 4.2 (Cycles on a GPU), Python 3 with Pillow and NumPy, and the HDRI
`lythwood_lounge_2k.hdr` from Poly Haven (CC0): set `NORTHLIGHT_HDRI` to its path
(it defaults to `H:\Tools\hdri\lythwood_lounge_2k.hdr`).

Every part is lit the way the tape echo's are, from one place, `common.py`: a key from
the top of the unit, the lounge in the reflections only (desaturated, so its warm
lamps do not tint the metal), shadows caught on a floor of what the part sits on (the
tolex under the plates, the champagne plate under the knob, toggle, button and lamp,
the cloth under the badge). The flat parts are seen head-on from 80 units (so they
keep their outline to a pixel); knobs, toggles, buttons and lamps in perspective from
8 units straight above; the speakers orthographically. 1 unit is 50 page px (about 20 mm). The look is our own: oxblood
tolex, champagne-gold anodised plates, a wheat and oxblood weave, brass piping, no
lettering anywhere (all text is drawn live by the app).

| Script | Renders |
|---|---|
| `tolex_render.py -- <out> [px_per_unit]` | The tolex the whole face sits on (`TOLEX_LIFT`, `TOLEX_SPEC`, `TOLEX_SHEEN`, `TOLEX_ROOM` tune it) |
| `weave_texture.py <colour.png> <height.png>` | Plain Python: the grille cloth's tiling weave, its colour and height |
| `plate_render.py -- <panel\|cab-panel\|strip\|baffle\|badge> <out>` | A plate and its shadow (`PLATE_COLOUR` for the champagne, `PLATE_SHEEN`) |
| `plate_render.py -- grille <out> <colour.png> <height.png>` | The grille: the weave in its frame with the brass piping |
| `speaker_render.py -- <12\|10\|15> <out> [size]` | A speaker straight on, the rim filling the width (rendered at 1600) |
| `knob_render.py -- <body\|pointer> <out> [size]` | The knob's body and shadow; the pointer alone, the layer that turns (rendered at 1280) |
| `toggle_render.py -- <up\|down> <out> 512 <hdri> 0.8 0.5` | The all-chrome bat-handle toggle in each position |
| `button_render.py -- <up\|down> <out> [size]` | The chrome footswitch button, up and pressed (rendered at 640) |
| `lamp_render.py -- <off\|lit> <out>` | The red jewel pilot lamp in its chrome bezel (`LAMP_BULB` sets the bulb) |

Run each as `blender --background --factory-startup --python <script> -- <args>`,
writing each render under the name the table in `assets/corona/README.md` gives it
(`panel.png`, `speaker-12.png`, `knob-body.png`, `jewel-lit.png` and so on;
`tolex.png` for the tolex). The plates, tolex and speakers were rendered with
`CORONA_SAMPLES=4` (four times the samples, so the undenoised brushing comes out
clean and compresses); `CORONA_SCALE=0.5` renders everything at half size for quick
looks.

`package_assets.py <renders folder>` writes `assets/corona` from the renders: each
scaled to its asset size, the small parts' shadows faded to nothing before the
frame's edge (as the tape echo's `fade_edges.py` does), the lit jewel's spill added
as a soft red glow round it, the tolex as a JPEG.

The tone step in `package_assets.py` brings the toggles' and buttons' chrome down to
the panel's light. The mics on the cabinet page are drawn markers, not renders.
