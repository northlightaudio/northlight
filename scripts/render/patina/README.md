# Patina's renders

The Blender scripts that render Patina's hardware face (design-system.md "Rendered
parts"), and the packaging that turns the renders into `assets/patina`. Re-run them
to change a part; nothing in the app runs them.

They share Corona's set-up (`scripts/render/corona/common.py`: the key from the top
of the unit, the lounge in the reflections only, shadow-catcher floors, the GPU
render), so Patina is lit exactly as Corona and the tape echo are. Needs what
Corona's do: Blender 4.2 (Cycles on a GPU), Python 3 with Pillow and NumPy, and the
HDRI `lythwood_lounge_2k.hdr` from Poly Haven (CC0) at `NORTHLIGHT_HDRI` (default
`H:\Tools\hdri\lythwood_lounge_2k.hdr`).

The look is our own: slate-green hammertone under grey-green satin enamel plates,
black crinkle bezels round amber windows, brushed aluminium,
bakelite knobs with spun aluminium caps and red pointers, amber push-buttons. No
lettering anywhere (all text is drawn live by the app).

| Script | Renders |
|---|---|
| `plate_render.py -- <panel\|module\|master\|nameplate\|window\|display> <out>` | A flat part and its shadow, head-on from 80 units, worn (`apply_wear`): `PATINA_WEAR` scales the wear (1 as shipped), `PATINA_SEED` varies it (the six module plates are seeds 1 to 6, `module-1.png` to `module-6.png`), `HAMMER_SCALE` sets the hammertone's dimples, `NAMEPLATE_SHEEN` the light the polished nameplate mirrors |
| `knob_render.py -- <body\|pointer> <out> [size]` | The knob's body and shadow; the pointer alone, the layer that turns (rendered at 1280) |
| `button_render.py -- <off\|on> <out> [width]` | A stage's push-button, dark and lit (`BUTTON_GLOW` sets the lit lens's emission; rendered at 480) |

Run each as `blender --background --factory-startup --python <script> -- <args>`,
writing each render under its asset's name (`panel.png`, `module.png`,
`knob-body.png`, `button-on.png` and so on). The flat parts were rendered with
`CORONA_SAMPLES=2`; `CORONA_SCALE=0.5` renders everything at half size for quick
looks.

`package_assets.py <renders folder>` writes `assets/patina` from the renders: the
panel as a JPEG, the knob at Corona's asset size, the small parts' shadows faded to
nothing before the frame's edge and the lit button's light added as a soft amber
glow round it (Corona's `fade_edges` and `spill`).
