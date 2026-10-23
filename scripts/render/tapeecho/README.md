# The tape echo's renders

The Blender scripts that render the tape echo's console (screens/plugins.md 7.2a,
design-system.md "Rendered parts"), and the packaging that turns the renders into
`assets/tapeecho`. Re-run them to change a part; nothing in the app runs them.

Needs Blender 4.2 (Cycles on a GPU), Python 3 with Pillow and NumPy, and the HDRI
`lythwood_lounge_2k.hdr` from Poly Haven (CC0): set `NORTHLIGHT_HDRI` to its path. The
cover plate's printing uses Barlow Condensed from `assets/fonts`.

Every part is lit the same way: a key from the top of the unit, the lounge in its
reflections only (desaturated, so its warm lamps do not tint the metal), shadows
caught on a panel of the right colour. Knobs, levers and lamps are seen in
perspective from 8 units straight above; the window and its bezel from 20 above the
window's centre; the trim and the panels head-on. 1 unit is about 20 mm.

| Script | Renders |
|---|---|
| `knob_layers.py -- body <out> 640 260 40 0.5` | A knob's body (knurled bakelite, spun aluminium cap) and its shadow |
| `knob_layers.py -- pointer <out> 640` | The cream pointer alone, evenly lit, the layer that turns |
| `toggle_render.py -- <up\|centre\|down> <out> 512 <hdri> 0.8 0.5` | A bat-handle lever in each position (cream bakelite tip) |
| `lamp_render.py -- <off\|lit> <out>` | The jewel lamp; `LAMP_GLASS=red LAMP_BULB=1.6` for the red one |
| `vu_render.py -- <body\|hood\|glass> <out>` | The meter: body, the pivot's hood, the glass's reflections (`BEZEL_FINISH=alu`) |
| `bezel_render.py -- <out>` | The window's bezel (`BEZEL_FINISH=alu`) |
| `transport_render.py -- transport <out>` | The transport under the smoked lid, lit by its lamps |
| `transport_render.py -- <neon_R\|neon_1..4> <out>` | One neon's light alone (every other light out) |
| `transport_render.py -- hubs <out>` | The rollers' screw slots and drillings, the parts that turn |
| `transport_render.py -- perspex <out>` | The lid's reflections and scratches |
| `trim_render.py -- <out>` | The aluminium trim between the panels |
| `panel_wear.py <wear.png>`, then `panel_render.py -- <wear.png> <out> 100` | The enamel front, worn where hands go |

Run each as `blender --background --factory-startup --python <script> -- <args>`.
`fade_edges.py` fades a render's alpha to nothing at its frame (the knob body, the
levers, the lamps); `glass_alpha.py` turns a glass render (reflections against black)
into colour with alpha. The hubs are cut from their render at the centres the script
prints, the panel split at the trim (rows 0-520 cream, 536-1200 sage).

`package_assets.py <renders folder>` writes `assets/tapeecho` from the renders, named
as the table there lists them.
