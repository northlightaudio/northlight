"""Blender (4.2) script: Corona's tolex, the dark oxblood-brown leather-grain vinyl the
whole face sits on (1160 x 600 page px, 23.2 x 12 units), seen head-on and lit as
every part is: the key as a distant sun from the top of the unit, a soft fall-off
from the top, the room in its sheen.

The grain: pebbles in two sizes (a levant grain), creased between, a satin vinyl
sheen on their tops; the colour a little uneven, faint scuffs where it has been
handled and carried.

Run: blender --background --factory-startup --python tolex_render.py -- <out.png> [px_per_unit]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402
import bmesh  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
OUT = argv[0]
PX = float(argv[1]) if len(argv) > 1 else 100.0
BW, BH = 1160 / 50, 600 / 50
DIST = 60.0
LIFT = float(os.environ.get("TOLEX_LIFT", "1.3"))     # base colour multiplier, to land on the token

scene = C.reset()

m = C.material("Tolex", (0, 0, 0))
nt = m.node_tree
L = nt.links.new
p = nt.nodes["Principled BSDF"]
co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]


def voronoi(scale, feature, rnd=1.0):
    v = nt.nodes.new("ShaderNodeTexVoronoi")
    v.feature = feature
    v.inputs["Scale"].default_value = scale
    v.inputs["Randomness"].default_value = rnd
    # A little noise in the lookup so the cells are not polygons.
    warp = C.noise(nt, co, 9.0, detail=3.0)
    off = nt.nodes.new("ShaderNodeVectorMath")
    off.operation = "MULTIPLY_ADD"
    L(warp, off.inputs[0])
    off.inputs[1].default_value = (0.02, 0.02, 0.0)
    L(co, off.inputs[2])
    L(off.outputs["Vector"], v.inputs["Vector"])
    return v


fine = voronoi(15.0, "DISTANCE_TO_EDGE")
coarse = voronoi(4.2, "DISTANCE_TO_EDGE", 0.9)
# Pebble heights: rounded away from each crease.
fine_h = C.ramp(nt, fine.outputs["Distance"], 0.0, 0.22)
fine_h = C.math_node(nt, "POWER", fine_h, 0.6)
coarse_h = C.ramp(nt, coarse.outputs["Distance"], 0.0, 0.08)
crease = C.math_node(nt, "MULTIPLY", fine_h, coarse_h, clamp=True)       # 0 in a crease
height = C.math_node(nt, "MULTIPLY_ADD", fine_h, 0.6, C.math_node(nt, "MULTIPLY", coarse_h, 0.4))
micro = C.noise(nt, co, 220.0, detail=4.0)
height = C.math_node(nt, "MULTIPLY_ADD", micro, 0.08, height)
bump = C.node(nt, "ShaderNodeBump", Strength=0.55, Distance=0.012)
L(height, bump.inputs["Height"])
L(bump.outputs["Normal"], p.inputs["Normal"])
L(bump.outputs["Normal"], p.inputs["Coat Normal"])

# Colour: the token, a little uneven, darker down in the creases, faint pale scuffs.
base = [c * LIFT for c in C.lin(C.TOLEX)]
mott = C.ramp(nt, C.noise(nt, co, 0.6, detail=4.0), 0.3, 0.7, 0.9, 1.08)
deep = C.ramp(nt, crease, 0.0, 0.5, 0.62, 1.0)
shade = C.math_node(nt, "MULTIPLY", mott, deep)
cm = nt.nodes.new("ShaderNodeCombineColor")
for ch in ("Red", "Green", "Blue"):
    L(shade, cm.inputs[ch])
col = nt.nodes.new("ShaderNodeMix")
col.data_type = "RGBA"
col.blend_type = "MULTIPLY"
col.inputs["Factor"].default_value = 1.0
col.inputs["A"].default_value = (*base, 1)
L(cm.outputs["Color"], col.inputs["B"])
scuff_mask = C.ramp(nt, C.noise(nt, co, 1.6, sx=1.0, sy=3.0, detail=8.0, rough=0.7), 0.6, 0.78, 0.0, 1.0)
scuff_fine = C.ramp(nt, C.noise(nt, co, 40.0, sx=4.0, sy=1.0, detail=6.0), 0.5, 0.7)
scuff = C.math_node(nt, "MULTIPLY", scuff_mask, scuff_fine, clamp=True)
scuff = C.math_node(nt, "MULTIPLY", scuff, C.ramp(nt, crease, 0.1, 0.6), clamp=True)   # only on the pebble tops
worn = nt.nodes.new("ShaderNodeMix")
worn.data_type = "RGBA"
worn.inputs["B"].default_value = (*[min(1.0, c * 1.9 + 0.006) for c in base], 1)
L(col.outputs["Result"], worn.inputs["A"])
L(C.math_node(nt, "MULTIPLY", scuff, 0.35), worn.inputs["Factor"])
L(worn.outputs["Result"], p.inputs["Base Color"])

# Satin vinyl: glossier on the pebble tops and where rubbed, dull in the creases.
rough = C.ramp(nt, crease, 0.0, 0.6, 0.78, 0.5)
rough = C.math_node(nt, "MULTIPLY_ADD", scuff, -0.12, rough, clamp=True)
L(rough, p.inputs["Roughness"])
p.inputs["Specular IOR Level"].default_value = float(os.environ.get("TOLEX_SPEC", "0.12"))
p.inputs["Coat Weight"].default_value = 0.04
p.inputs["Coat Roughness"].default_value = 0.42

bm = bmesh.new()
for x, y in ((-BW / 2, -BH / 2), (BW / 2, -BH / 2), (BW / 2, BH / 2), (-BW / 2, BH / 2)):
    bm.verts.new((x, y, 0))
bm.faces.new(bm.verts[:])
C.link(scene, "Tolex", bm, m)

C.camera_down(scene, BW, BH, dist=DIST)
C.key_sun(scene, energy=2.3, angle=30)
C.area(scene, "TopFall", 30.0, 900, (0, BH / 2 + 4, 7), (0, 0, 0), shape="RECTANGLE", size_y=3.0, glossy=False)
# A broad soft source high over the top of the unit, for the vinyl's sheen.
C.area(scene, "Sheen", 30.0, float(os.environ.get("TOLEX_SHEEN", "60")), (0, 8, 30), (0, 0, 0), shape="RECTANGLE", size_y=12.0, diffuse=False, shadow=False)  # noqa
C.room(scene, strength=float(os.environ.get("TOLEX_ROOM", "0.3")))
C.render(scene, OUT, round(BW * PX), round(BH * PX), samples=384, transparent=False, denoise=False)
