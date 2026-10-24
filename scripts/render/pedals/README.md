# The guitar pedals' renders

The Blender scripts that render the guitar pedals' faces (design-system.md "Rendered
parts"), and the packaging that turns the renders into `assets/pedals`. Re-run them
to change a part; nothing in the app runs them.

They use Corona's lighting and helpers (`scripts/render/corona/common.py`) so the
whole Guitar group is lit alike, and Corona's toggle script for the toggles. Needs
Blender 4.2 (Cycles on a GPU), Python 3 with Pillow and NumPy, and the HDRI
`lythwood_lounge_2k.hdr` from Poly Haven (CC0) at `NORTHLIGHT_HDRI` (default
`H:\Tools\hdri\lythwood_lounge_2k.hdr`).

| Script | Renders |
|---|---|
| `enclosure_render.py -- <gravity\|flare\|comet\|tide\|polaris> <out> 100` | A whole face: the die-cast enclosure in that finish, worn, on the pedalboard's carpet, head-on (`PEDAL_WEAR`, `PEDAL_SEED` tune the wear) |
| `knob_render.py -- <body\|pointer> <out> 960` | The knob's body and shadow; its pointer line alone, the layer that turns |
| `../corona/toggle_render.py -- <up\|centre\|down> <out> 512` | The chrome bat toggle in each position |

Run each as `blender --background --factory-startup --python <script> -- <args>`,
writing `enclosure-<finish>.png`, `knob-body.png`, `knob-pointer.png` and
`toggle-<state>.png` into one folder, then `python package_assets.py <folder>`
writes `assets/pedals`: each at its asset size, the knob's and toggles' shadows
faded before the frame's edge, the pointer cropped to the knob, the toggles' chrome
toned down to the pedals' light. `CORONA_SCALE=0.5` renders at half size for quick
looks.
