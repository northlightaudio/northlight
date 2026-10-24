"""Blender (4.2) script: Corona's round chrome footswitch push-button seen from above,
as the knobs are (perspective from 8 units straight above, the key from the top of
the unit, the room in its reflections), its shadow caught on the champagne plate
and otherwise transparent. A knurled chrome ring round a domed polished cap.

  up    the cap standing proud of the ring
  down  the cap pressed down into it

The ring is 1.0 unit across; the image is 1.6 units square, centred on the button.

Run: blender --background --factory-startup --python button_render.py -- <up|down> <out.png> [size_px]
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402
import bmesh  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
STATE, OUT = argv[0], argv[1]
SIZE = int(argv[2]) if len(argv) > 2 else 640
FRAME = 1.6
PRESS = 0.0 if STATE == "up" else 0.085

scene = C.reset()


def chrome(name, rough=(0.06, 0.16)):
    m = C.material(name, C.lin("#E2E0DC"), metallic=1.0, rough=rough[0])
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    r = C.ramp(nt, C.noise(nt, co, 5.0, detail=5.0), 0.35, 0.7, rough[0], rough[1])
    nt.links.new(r, p.inputs["Roughness"])
    return m


def knurled_ring(r_out, r_in, z0, z1, ridges, depth, mat, chamfer=0.02):
    """A ring knurled on its outside with sharp V-ridges that run on up through a
    broad chamfer, so they show from above as a ring of teeth catching the light."""
    bm = bmesh.new()
    n = ridges * 2
    outer = []
    for z, inset in ((z0, 0.0), (z1 - chamfer, 0.0), (z1, chamfer)):
        outer.append([bm.verts.new(((r_out - inset - (depth if i % 2 else 0.0)) * math.cos(2 * math.pi * i / n),
                                    (r_out - inset - (depth if i % 2 else 0.0)) * math.sin(2 * math.pi * i / n), z))
                      for i in range(n)])
    inner = [[bm.verts.new((r * math.cos(2 * math.pi * i / n), r * math.sin(2 * math.pi * i / n), z)) for i in range(n)]
             for r, z in ((r_in + 0.012, z1), (r_in, z1 - 0.012), (r_in, z0))]
    rings = outer + inner
    for lo, hi in zip(rings, rings[1:] + [rings[0]]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    ob = C.link(scene, "Ring", bm, mat)
    C.smooth(ob, False)
    return ob


shiny = chrome("Chrome")
knurl = chrome("Knurl", (0.12, 0.24))
# The threaded body's top nut sits on the plate; the knurled ring on it.
C.revolve(scene, "Base", [(0.0, 0.0), (0.56, 0.0), (0.56, 0.02), (0.54, 0.035), (0.0, 0.035)], chrome("Washer", (0.2, 0.32)))
knurled_ring(0.5, 0.34, 0.035, 0.2, 90, 0.018, knurl, chamfer=0.075)
# The plunger's throat, dark, and the cap in it.
C.revolve(scene, "Throat", [(0.335, 0.2), (0.335, 0.06), (0.0, 0.06)],
          C.material("Throat", (0.02, 0.02, 0.02), rough=0.6))
top = 0.31 - PRESS
cap = [(0.3, 0.12), (0.3, top - 0.05)]
for k in range(1, 7):                           # the rounded rim
    a = math.radians(90 * k / 6)
    cap.append((0.3 - 0.03 * (1 - math.cos(a)), top - 0.05 + 0.03 * math.sin(a)))
for k in range(1, 13):                          # the dome
    t = k / 12
    r = 0.27 * (1 - t)
    cap.append((r, top - 0.02 + 0.06 * (1 - (r / 0.27) ** 2)))
C.revolve(scene, "Cap", cap, shiny, 192)

C.shadow_floor(scene, C.CHAMPAGNE, size=10)
C.camera_down(scene, FRAME, FRAME, dist=8.0)
C.area(scene, "Key", 2.5, 260, (0, 3.4, 4.0), (0, 0, 0.2))
C.area(scene, "Fill", 6.0, 40, (0, 0, 7.0), (0, 0, 0.2), glossy=False)
C.room(scene, strength=0.8)
C.render(scene, OUT, SIZE, SIZE, samples=768, transparent=True, denoise=True)
