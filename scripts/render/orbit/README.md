# Orbit's renders

The Blender scene of Orbit's face (screens/plugins.md, Orbit; design-system.md "Rendered
parts") and the packaging that turns its renders into `assets/orbit`. Re-run them to change
the face; nothing in the app runs them.

Needs Blender 4.2 (Cycles) and Python 3 with Pillow. The HDRI `lythwood_lounge_2k.hdr`
(Poly Haven, CC0) is bundled in `scripts/render/hdri` and found without any setting
(`NORTHLIGHT_HDRI` points the scripts at another; scripts/render/render_config.py). The
scene borrows the tape echo's geometry and material helpers (`../tapeecho/vu_render.py`).

The face is one scene, 1160 x 600 px at 50 px per unit, seen from 40 units in front: a
brushed aluminium transport plate with a black anodised nameplate, two 10.5-inch NAB metal
reels (flanges turned from a profile with a rolled edge and stiffening ribs, rounded
windows, smooth tape packs, the deck's locking adapters), the head block, guides, capstan
and pinch roller; a hammertone control panel with two VU meters (built as the tape echo's:
a bakelite bezel, a sunk dial lit by two bulbs under the pivot hood), knurled knobs and
ivory keys (backlit faintly when unchosen, amber when chosen). It is lit as a dim mastering
room: a warm spot pooled on the left reel, a cool rim from the right, a low warm fill along
the panel, and the lounge in reflections only.

Nothing printed is rendered: the card prints every word, the meters' scales and the
counter. `ORBIT_COMP=1` renders them too, with needles and lit keys, as a comp of the whole
face (it is how the face was designed).

| Command | Renders |
|---|---|
| `python render_all.py <folder>` | Every layer below, one Blender run each, at idle priority on the CPU |
| `face_render.py` with `ORBIT_LAYER=deck` | Everything still: plate, reels at rest, knob bodies, keys unlit |
| `ORBIT_LAYER=lit` | The same with every key and the pilot lamp lit |
| `ORBIT_LAYER=pointers` | The knobs' pointers alone, each pointing up, with alpha |
| `ORBIT_LAYER=hood`, `ORBIT_LAYER=glass` | The meters' shrouds, and their glass's reflections, with alpha |
| `ORBIT_REEL_ANGLE=<degrees>` | The reels turned anticlockwise: render_all.py makes 30 frames, 4 degrees apart |

Run one as `blender --background --factory-startup --python face_render.py -- <out.png>
<scale> <samples>`; `ORBIT_GPU=1` renders on the GPU, `ORBIT_BORDER=x0,y0,x1,y1` a part of
the frame, `ORBIT_MOOD=0` the tape echo's even light instead of the room.

`python render_all.py <folder> --remote` renders the set on another machine's GPU
instead, in the Blender container of `scripts/render/remote` (its README says how to set
one up and point the scripts at it): minutes on a GPU, against well over an hour on a CPU.
Blender itself is found as scripts/render/render_config.py says (`BLENDER`, or `blender`
on the PATH). Render a whole set on one machine: CPU and GPU noise differ a little, and the
reels' frames must match the still deck.

`package_assets.py <folder>` writes `assets/orbit` from the renders. The layout is mirrored in
`src/ui/strip/OrbitFace.h`: a part moved in the scene moves there too.
