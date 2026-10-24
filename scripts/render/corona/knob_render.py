"""Blender (4.2) script: Corona's knob, in the two layers the card stacks (the tape
echo's knob_layers.py, its framing and camera):

  body     glossy black bakelite, fluted (rounded grooves, not a knurl), a bright
           polished chrome skirt ring at its base, the top slightly domed; lit from
           the top of the unit, its shadow caught on the champagne plate.
  pointer  the white inlaid line on the top, from near the centre toward the edge
           (pointing up at rest), lit evenly on transparency: the layer that turns.

Perspective from 8 units straight above (the plate spans 3.4 units across); the body
image is 1.5 times as tall as wide, the extra below for the shadow.

Run: blender --background --factory-startup --python knob_render.py -- <body|pointer> <out.png> [size_px]
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402
import bmesh  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
LAYER, OUT = argv[0], argv[1]
SIZE = int(argv[2]) if len(argv) > 2 else 1280
DIST, FRAME = 8.0, 3.4

FLUTES = 24
R_BASE, R_TOP = 0.74, 0.68          # the fluted body tapers a little
Z_SKIRT, Z_BODY = 0.1, 0.8          # the skirt ring's top, the body's shoulder
DOME = 0.05                         # how far the top rises in its middle
EDGE = 0.07                         # the shoulder's rounding

scene = C.reset()


def flute(theta):
    """0 on the lands, 1 at the bottom of a groove: rounded grooves, wide lands."""
    c = 0.5 + 0.5 * math.cos(FLUTES * theta)
    return c ** 3


def top_z(r):
    rt = R_TOP - EDGE
    return Z_BODY + DOME * max(0.0, 1 - (r / rt) ** 2)


def body_mesh(mat):
    n = FLUTES * 24
    rings = []
    # Up the fluted side.
    for k in range(9):
        t = k / 8
        z = Z_SKIRT - 0.02 + (Z_BODY - EDGE - Z_SKIRT + 0.02) * t
        r = R_BASE + (R_TOP - R_BASE) * t
        rings.append([(r - 0.04 * flute(2 * math.pi * i / n), z) for i in range(n)])
    # Over the rounded shoulder, the grooves running out onto the top.
    for k in range(1, 9):
        a = math.radians(90 * k / 8)
        inset = EDGE * (1 - math.cos(a))
        z = Z_BODY - EDGE + EDGE * math.sin(a)
        depth = 0.04 * (1 - k / 8) ** 3
        rings.append([(R_TOP - inset - depth * flute(2 * math.pi * i / n), z) for i in range(n)])
    # The slightly domed top.
    for k in range(1, 12):
        r = (R_TOP - EDGE) * (1 - k / 12)
        rings.append([(r, top_z(r)) for _ in range(n)])
    pts = [[(r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n), z)
            for i, (r, z) in enumerate(ring)] for ring in rings]
    ob = C.loft(scene, "Body", pts, mat)
    C.smooth(ob)
    return ob


if LAYER == "body":
    bakelite = C.material("Bakelite", (0.010, 0.0095, 0.009), rough=0.18, coat=0.7)
    nt = bakelite.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Coat Roughness"].default_value = 0.06
    p.inputs["Coat Weight"].default_value = 0.45
    p.inputs["Specular IOR Level"].default_value = 0.3
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    # Moulded, then handled: the gloss faintly uneven, a few fine scuffs.
    wear = C.ramp(nt, C.noise(nt, co, 6.0, detail=5.0), 0.3, 0.7, 0.14, 0.26)
    nt.links.new(wear, p.inputs["Roughness"])
    scuff = C.ramp(nt, C.noise(nt, co, 1.0, sx=3.0, sy=90.0, sz=3.0, detail=3.0), 0.68, 0.72, 0.03, 0.12)
    nt.links.new(scuff, p.inputs["Coat Roughness"])
    body_mesh(bakelite)

    chrome = C.material("Chrome", C.lin("#E4E2DE"), metallic=1.0, rough=0.07)
    cn = chrome.node_tree
    cp = cn.nodes["Principled BSDF"]
    co = cn.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    cr = C.ramp(cn, C.noise(cn, co, 4.0, detail=5.0), 0.35, 0.7, 0.05, 0.14)
    cn.links.new(cr, cp.inputs["Roughness"])
    # The skirt ring: a polished band, rounded over, flaring slightly at its foot.
    C.revolve(scene, "Skirt", [(0.0, 0.0), (0.93, 0.0), (0.935, 0.012), (0.93, 0.04), (0.915, 0.075),
                               (0.89, 0.095), (0.86, Z_SKIRT), (0.70, Z_SKIRT), (0.70, 0.0)], chrome, 256)
    C.shadow_floor(scene, C.CHAMPAGNE, size=14)
else:
    white = C.material("Inlay", C.lin("#F2EEE6"), rough=0.5)
    bm = bmesh.new()
    w = 0.028
    rows = []
    for k in range(13):
        y = 0.16 + (0.56 - 0.16) * k / 12
        z = top_z(y) + 0.003
        rows.append([bm.verts.new((-w, y, z)), bm.verts.new((w, y, z))])
    for a, b in zip(rows, rows[1:]):
        bm.faces.new((a[0], a[1], b[1], b[0]))
    # Rounded ends.
    for row, sign in ((rows[0], -1), (rows[-1], 1)):
        yc = row[0].co.y
        z = row[0].co.z
        fan = [bm.verts.new((w * math.cos(math.pi * j / 8), yc + sign * w * math.sin(math.pi * j / 8), z)) for j in range(9)]
        centre = bm.verts.new((0, yc, z))
        for j in range(8):
            bm.faces.new((centre, fan[j], fan[j + 1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    C.link(scene, "Pointer", bm, white)

# ----- camera ------------------------------------------------------------------------------------
TALL = 1.5 if LAYER == "body" else 1.0
C.camera_down(scene, FRAME, FRAME * TALL, centre=(0, -FRAME * (TALL - 1) / 2), dist=DIST)

if LAYER == "body":
    C.area(scene, "Key", 2.5, 260, (0, 3.4, 4.0), (0, 0, 0.4))
    # The fill stays out of the reflections: the domed top would mirror it as a grey disc.
    C.area(scene, "Fill", 6.0, 40, (0, 0, 7.0), (0, 0, 0.4), glossy=False)
    C.area(scene, "Rim", 1.5, 260 * 0.35, (0, 4.2, 1.2), (0, 0, 0.5), shadow=False)
    C.room(scene, strength=0.35)
else:
    C.area(scene, "Fill", 8.0, 160, (0, 0, 7.0), (0, 0, 0))
    C.flat_world(scene)

C.render(scene, OUT, SIZE, round(SIZE * TALL), samples=768, transparent=True, denoise=True)
