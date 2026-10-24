"""Blender (4.2) script: the guitar pedals' knob, in the two layers the card stacks (Corona's
knob_render.py, its framing, camera and lights):

  body     black moulded plastic: a flange at its foot, a tapered body knurled with
           straight ridges, a softly rounded edge onto a flat top; lit from the top
           of the unit, its shadow caught on a transparent floor.
  pointer  the white line on the top, from near the centre out to the edge (pointing
           up at rest), lit evenly on transparency: the layer that turns.

Perspective from 8 units straight above, the frame 3.0 units across (the flange is
1.6 across); the body image is 1.5 times as tall as wide, the extra below for the
shadow.

Run: blender --background --factory-startup --python knob_render.py -- <body|pointer> <out.png> [size_px]
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "corona"))
import common as C  # noqa: E402
import bmesh  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
LAYER, OUT = argv[0], argv[1]
SIZE = int(argv[2]) if len(argv) > 2 else 960
DIST, FRAME = 8.0, 3.0

R_FLANGE, Z_FLANGE = 0.8, 0.1
R_BASE, R_TOP = 0.68, 0.62      # the knurled body tapers
Z_TOP = 0.78
EDGE = 0.15
RIDGES = 40

scene = C.reset()


def knurl(theta):
    """0 on a ridge's crest, 1 in the groove between: sharp V grooves."""
    c = abs(math.sin(RIDGES * theta / 2))
    return 1.0 - c ** 0.6


def body_mesh(mat):
    n = RIDGES * 12
    rings = []
    # The flange: a short rounded skirt at the foot.
    for r, z in ((R_FLANGE - 0.01, 0.0), (R_FLANGE, 0.02), (R_FLANGE, Z_FLANGE - 0.03), (R_FLANGE - 0.03, Z_FLANGE),
                 (R_BASE + 0.02, Z_FLANGE)):
        rings.append([(r, z) for _ in range(n)])
    # Up the knurled body.
    for k in range(9):
        t = k / 8
        z = Z_FLANGE + (Z_TOP - EDGE - Z_FLANGE) * t
        r = R_BASE + (R_TOP - R_BASE) * t
        rings.append([(r - 0.025 * knurl(2 * math.pi * i / n), z) for i in range(n)])
    # Over the rounded edge, the grooves running out.
    for k in range(1, 7):
        a = math.radians(90 * k / 6)
        inset = EDGE * (1 - math.cos(a))
        z = Z_TOP - EDGE + EDGE * math.sin(a)
        depth = 0.025 * (1 - k / 6) ** 1.2
        rings.append([(R_TOP - inset - depth * knurl(2 * math.pi * i / n), z) for i in range(n)])
    # The flat top.
    for k in range(1, 6):
        r = (R_TOP - EDGE) * (1 - k / 6)
        rings.append([(r, Z_TOP) for _ in range(n)])
    pts = [[(r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n), z)
            for i, (r, z) in enumerate(ring)] for ring in rings]
    ob = C.loft(scene, "Body", pts, mat)
    C.smooth(ob)
    return ob


if LAYER == "body":
    plastic = C.material("Plastic", (0.008, 0.008, 0.009), rough=0.32, coat=0.1)
    nt = plastic.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Coat Roughness"].default_value = 0.18
    p.inputs["Specular IOR Level"].default_value = 0.25
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    # Moulded, then handled by a foot's worth of players: the sheen uneven, fine scuffs.
    wear = C.ramp(nt, C.noise(nt, co, 7.0, detail=5.0), 0.3, 0.7, 0.26, 0.42)
    nt.links.new(wear, p.inputs["Roughness"])
    scuff = C.ramp(nt, C.noise(nt, co, 1.0, sx=3.0, sy=90.0, sz=3.0, detail=3.0), 0.66, 0.72, 0.12, 0.4)
    nt.links.new(scuff, p.inputs["Coat Roughness"])
    body_mesh(plastic)
    C.shadow_floor(scene, "#303030", size=14)
else:
    white = C.material("Line", C.lin("#F2EEE6"), rough=0.5)
    bm = bmesh.new()
    w = 0.03
    y0, y1 = 0.1, R_TOP - EDGE + 0.02
    z = Z_TOP + 0.003
    quad = [bm.verts.new(v) for v in ((-w, y0, z), (w, y0, z), (w, y1, z), (-w, y1, z))]
    bm.faces.new(quad)
    # A rounded inner end.
    centre = bm.verts.new((0, y0, z))
    fan = [bm.verts.new((w * math.cos(math.pi * j / 8), y0 - w * math.sin(math.pi * j / 8), z)) for j in range(9)]
    for j in range(8):
        bm.faces.new((centre, fan[j + 1], fan[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    C.link(scene, "Pointer", bm, white)

# ----- camera ------------------------------------------------------------------------------------
TALL = 1.5 if LAYER == "body" else 1.0
C.camera_down(scene, FRAME, FRAME * TALL, centre=(0, -FRAME * (TALL - 1) / 2), dist=DIST)

if LAYER == "body":
    C.area(scene, "Key", 2.5, 260, (0, 3.4, 4.0), (0, 0, 0.4))
    C.area(scene, "Fill", 6.0, 40, (0, 0, 7.0), (0, 0, 0.4), glossy=False)
    C.area(scene, "Rim", 1.5, 260 * 0.35, (0, 4.2, 1.2), (0, 0, 0.5), shadow=False)
    C.room(scene, strength=0.35)
else:
    C.area(scene, "Fill", 8.0, 160, (0, 0, 7.0), (0, 0, 0))
    C.flat_world(scene)

C.render(scene, OUT, SIZE, round(SIZE * TALL), samples=768, transparent=True, denoise=True)
