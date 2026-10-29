# Lumen's renders

The Blender scene of Lumen's face (screens/plugins.md 7.4i; design-system.md "Rendered
parts") and the packaging that turns its renders into `assets/lumen`. Re-run them to change
the face; nothing in the app runs them.

Needs Blender 4.2 (Cycles) and Python 3 with Pillow. The HDRI `lythwood_lounge_2k.hdr`
(Poly Haven, CC0) is bundled in `scripts/render/hdri` and found without any setting
(`NORTHLIGHT_HDRI` points the scripts at another; scripts/render/render_config.py). The
scene borrows the tape echo's geometry and material helpers (`../tapeecho/vu_render.py`).

The face is one scene, 1160 x 600 px at 50 px per unit, seen from 40 units in front: black
pebbled tolex above a chrome trim, with the harp window (a chromed bezel and glass, four
tine assemblies on dark felt: a mounting block, a brass tone bar, a steel tine with its
tuning spring, a pickup's pole and copper coil on a black bobbin, lit warm from inside), a
black anodised nameplate, three service trims and the pilot lamp; below it, a brushed
aluminium preamp plate with skirted bakelite knobs (spun aluminium caps) and two amber
jewel lamps. It is lit by a key from the top of the unit, a warm fill from below and the
lounge in reflections only. Each tine is a curve bent as a clamped rod bends, so its swing
frames are exact.

| Command | Renders |
|---|---|
| `python render_all.py <folder>` | Every layer below, one Blender run each, at idle priority on the CPU |
| `face_render.py` with `LUMEN_LAYER=deck` | Everything still: tines at rest, knob bodies, lamps dark |
| `LUMEN_LAYER=lit` | The same with the pilot and vibrato lamps lit |
| `LUMEN_LAYER=pointers` | The knobs' pointer lines alone, each pointing up, with alpha |
| `LUMEN_SWING=<s>` | The tines bent (-1 tips down .. 1 up): render_all.py makes 9 frames |

Run one as `blender --background --factory-startup --python face_render.py -- <out.png>
<scale> <samples>`; `LUMEN_GPU=1` renders on the GPU. `--samples` and `--scale` on
render_all.py make a quick look.

`python render_all.py <folder> --remote` renders the set on another machine's GPU instead,
in the Blender container of `scripts/render/remote` (its README says how to set one up and
point the scripts at it). Render a whole set on one machine: the tines' frames must match
the still deck.

`package_assets.py <folder>` writes `assets/lumen` from the renders. The layout is mirrored
in `src/ui/strip/LumenFace.h`: a part moved in the scene moves there too.
