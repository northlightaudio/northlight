"""Blender (4.2) script: a stage's latching push-button, seen from above as the knobs
are (perspective from 8 units straight above, the key from the top of the unit, the
room in its reflections), its shadow caught on the enamel plate and otherwise
transparent. A rectangular nickel bezel round an amber lens, the kind of lit key a
broadcast desk switched its channels with.

  off  the lens dark, standing proud of the bezel
  on   pressed in a little and lit from inside (an emitting core behind frosted glass)

The bezel is 0.72 x 0.48 units (36 x 24 page px); the image is 1.2 x 0.9 units,
centred on the button.

Run: blender --background --factory-startup --python button_render.py -- <off|on> <out.png> [width_px]
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "corona"))
import common as C  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
STATE, OUT = argv[0], argv[1]
SIZE = int(argv[2]) if len(argv) > 2 else 480
FW, FH = 1.2, 0.9
ENAMEL = "#A3ABA3"
LIT = STATE == "on"
PRESS = 0.03 if LIT else 0.0

scene = C.reset()


def nickel(rough=(0.12, 0.22)):
    m = C.material("Nickel", C.lin("#CFCBC2"), metallic=1.0, rough=rough[0])
    nt = m.node_tree
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    nt.links.new(C.ramp(nt, C.noise(nt, co, 6.0, detail=5.0), 0.35, 0.7, rough[0], rough[1]),
                 nt.nodes["Principled BSDF"].inputs["Roughness"])
    return m


def slab(name, w, h, r, z0, z1, edge, mat):
    rings = [[(x, y, z0) for x, y in C.rrect(w, h, r)]]
    rings.append([(x, y, z1 - edge) for x, y in C.rrect(w, h, r)])
    for k in range(1, 7):
        a = math.radians(90 * k / 6)
        rings.append([(x, y, z1 - edge + edge * math.sin(a)) for x, y in C.rrect(w, h, r, edge * (1 - math.cos(a)))])
    ob = C.loft(scene, name, rings, mat)
    C.smooth_by_angle(ob, 40)
    return ob


def frame(name, w, h, r, inset, z0, z1, edge, mat):
    import bmesh
    bm = bmesh.new()
    prof = [(0.0, z0), (0.0, z1 - edge)]
    for k in range(1, 6):
        a = math.radians(90 * k / 5)
        prof.append((edge * (1 - math.cos(a)), z1 - edge + edge * math.sin(a)))
    for k in range(0, 6):
        a = math.radians(90 * k / 5)
        prof.append((inset - edge + edge * math.sin(a), z1 - edge + edge * math.cos(a)))
    prof.append((inset, z0))
    rings = [[bm.verts.new((x, y, z)) for x, y in C.rrect(w, h, r, i)] for i, z in prof]
    n = len(rings[0])
    for lo, hi in zip(rings, rings[1:] + [rings[0]]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    ob = C.link(scene, name, bm, mat)
    C.smooth_by_angle(ob, 40)
    return ob


# The bezel: a nickel frame on the plate.
frame("Bezel", 0.36, 0.24, 0.06, 0.06, 0.0, 0.1, 0.025, nickel())
# The throat, dark.
slab("Throat", 0.3, 0.18, 0.03, 0.0, 0.03, 0.005, C.material("Throat", (0.015, 0.015, 0.015), rough=0.6))
# The lens: frosted amber glass, its top slightly domed by a rounded edge.
glass = C.material("Lens", C.lin("#C8741E" if LIT else "#4A3014"), rough=0.32)
gp = glass.node_tree.nodes["Principled BSDF"]
gp.inputs["Transmission Weight"].default_value = 0.35 if LIT else 0.15
gp.inputs["Coat Weight"].default_value = 0.6
gp.inputs["Coat Roughness"].default_value = 0.08
if LIT:
    gp.inputs["Emission Color"].default_value = (*C.lin("#F09432"), 1)
    gp.inputs["Emission Strength"].default_value = float(os.environ.get("BUTTON_GLOW", "0.9"))
gnt = glass.node_tree
gco = gnt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
frost = C.node(gnt, "ShaderNodeBump", Strength=0.08, Distance=0.004)
gnt.links.new(C.noise(gnt, gco, 120.0, detail=3.0), frost.inputs["Height"])
gnt.links.new(frost.outputs["Normal"], gp.inputs["Normal"])
top = 0.16 - PRESS
slab("Lens", 0.285, 0.165, 0.03, 0.03, top, 0.04, glass)

C.shadow_floor(scene, ENAMEL, size=10)
C.camera_down(scene, FW, FH, dist=8.0)
C.area(scene, "Key", 2.5, 260, (0, 3.4, 4.0), (0, 0, 0.1))
C.area(scene, "Fill", 6.0, 40, (0, 0, 7.0), (0, 0, 0.1), glossy=False)
C.room(scene, strength=0.7)
C.render(scene, OUT, SIZE, round(SIZE * FH / FW), samples=768, transparent=True, denoise=True)
