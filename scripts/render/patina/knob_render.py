"""Blender (4.2) script: Patina's knob, in the two layers the card stacks (Corona's
knob_render.py, its framing and camera):

  body     a broadcast knob: a flared black bakelite skirt, a fluted body over it,
           a satin aluminium cap set into its top (spun, so its sheen is a ring);
           lit from the top of the unit, its shadow caught on the enamel plate.
  pointer  the red line engraved across the cap from near its centre to its rim
           and on down the body's shoulder (pointing up at rest), lit evenly on
           transparency: the layer that turns.

Perspective from 8 units straight above (the plate spans 3.4 units across); the body
image is 1.5 times as tall as wide, the extra below for the shadow. The skirt is
1.9 units across: 56% of the frame's width.

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
SIZE = int(argv[2]) if len(argv) > 2 else 1280
DIST, FRAME = 8.0, 3.4
ENAMEL = "#A3ABA3"

FLUTES = 18
R_SKIRT = 0.95                       # the skirt's foot
R_BODY, R_TOP = 0.66, 0.62           # the fluted body, tapering a little
Z_SKIRT, Z_BODY = 0.16, 0.86         # the skirt's top, the body's shoulder
EDGE = 0.06
R_CAP, CAP_SINK = 0.47, 0.025        # the cap's radius, how far it sits below the rim

scene = C.reset()


def flute(theta):
    c = 0.5 + 0.5 * math.cos(FLUTES * theta)
    return c ** 3


def body_mesh(mat):
    n = FLUTES * 24
    rings = []
    # The skirt: a flare from the foot up to the body.
    for k in range(10):
        t = k / 9
        r = R_SKIRT - (R_SKIRT - R_BODY - 0.02) * (t ** 0.6)
        z = 0.0 + Z_SKIRT * math.sin(t * math.pi / 2)
        rings.append([(r, z) for _ in range(n)])
    # Up the fluted side.
    for k in range(9):
        t = k / 8
        z = Z_SKIRT + 0.01 + (Z_BODY - EDGE - Z_SKIRT - 0.01) * t
        r = R_BODY + (R_TOP - R_BODY) * t
        rings.append([(r - 0.035 * flute(2 * math.pi * i / n), z) for i in range(n)])
    # Over the rounded shoulder, the grooves running out.
    for k in range(1, 9):
        a = math.radians(90 * k / 8)
        inset = EDGE * (1 - math.cos(a))
        z = Z_BODY - EDGE + EDGE * math.sin(a)
        depth = 0.035 * (1 - k / 8) ** 3
        rings.append([(R_TOP - inset - depth * flute(2 * math.pi * i / n), z) for i in range(n)])
    # The flat rim, then down into the cap's seat.
    for r, z in ((R_CAP + 0.05, Z_BODY), (R_CAP + 0.01, Z_BODY - 0.005), (R_CAP + 0.004, Z_BODY - CAP_SINK - 0.01)):
        rings.append([(r, z) for _ in range(n)])
    pts = [[(r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n), z)
            for i, (r, z) in enumerate(ring)] for ring in rings]
    ob = C.loft(scene, "Body", pts, mat, cap_top=False)
    C.smooth(ob)
    return ob


def cap_z():
    return Z_BODY - CAP_SINK


if LAYER == "body":
    bakelite = C.material("Bakelite", (0.012, 0.011, 0.010), rough=0.2, coat=0.6)
    nt = bakelite.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Coat Roughness"].default_value = 0.08
    p.inputs["Specular IOR Level"].default_value = 0.3
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    nt.links.new(C.ramp(nt, C.noise(nt, co, 6.0, detail=5.0), 0.3, 0.7, 0.16, 0.3), p.inputs["Roughness"])
    body_mesh(bakelite)

    # The cap: satin aluminium, spun (anisotropic round the axis).
    alu = C.material("Cap", C.lin("#C9CBC8"), metallic=1.0, rough=0.28)
    an = alu.node_tree
    ap = an.nodes["Principled BSDF"]
    ap.inputs["Anisotropic"].default_value = 0.8
    ap.inputs["Anisotropic Rotation"].default_value = 0.25    # tangent round the axis
    aco = an.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    vm = an.nodes.new("ShaderNodeVectorMath")
    vm.operation = "LENGTH"
    an.links.new(aco, vm.inputs[0])
    rings = an.nodes.new("ShaderNodeTexNoise")
    rings.inputs["Scale"].default_value = 300.0
    rings.inputs["Detail"].default_value = 2.0
    an.links.new(vm.outputs["Value"], rings.inputs["Vector"])
    bump = C.node(an, "ShaderNodeBump", Strength=0.25, Distance=0.002)
    an.links.new(rings.outputs["Fac"], bump.inputs["Height"])
    an.links.new(bump.outputs["Normal"], ap.inputs["Normal"])
    z = cap_z()
    C.revolve(scene, "Cap", [(0.0, z + 0.008), (R_CAP - 0.02, z + 0.006), (R_CAP, z - 0.004), (R_CAP, z - 0.03),
                              (0.0, z - 0.03)], alu, 256)
    C.shadow_floor(scene, ENAMEL, size=14)
else:
    red = C.material("Pointer", C.lin("#B8301F"), rough=0.45)
    bm = bmesh.new()
    w = 0.03
    rows = []
    z = cap_z() + 0.012
    for k in range(13):
        y = 0.12 + (R_CAP - 0.04 - 0.12) * k / 12
        rows.append([bm.verts.new((-w, y, z)), bm.verts.new((w, y, z))])
    for a, b in zip(rows, rows[1:]):
        bm.faces.new((a[0], a[1], b[1], b[0]))
    yc = rows[0][0].co.y
    fan = [bm.verts.new((w * math.cos(math.pi * j / 8), yc - w * math.sin(math.pi * j / 8), z)) for j in range(9)]
    centre = bm.verts.new((0, yc, z))
    for j in range(8):
        bm.faces.new((centre, fan[j], fan[j + 1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    C.link(scene, "Pointer", bm, red)

TALL = 1.5 if LAYER == "body" else 1.0
C.camera_down(scene, FRAME, FRAME * TALL, centre=(0, -FRAME * (TALL - 1) / 2), dist=DIST)
if LAYER == "body":
    C.area(scene, "Key", 2.5, 260, (0, 3.4, 4.0), (0, 0, 0.4))
    C.area(scene, "Fill", 6.0, 40, (0, 0, 7.0), (0, 0, 0.4), glossy=False)
    C.area(scene, "Rim", 1.5, 260 * 0.35, (0, 4.2, 1.2), (0, 0, 0.5), shadow=False)
    C.room(scene, strength=0.45)
else:
    C.area(scene, "Fill", 8.0, 160, (0, 0, 7.0), (0, 0, 0))
    C.flat_world(scene)

C.render(scene, OUT, SIZE, round(SIZE * TALL), samples=768, transparent=True, denoise=True)
