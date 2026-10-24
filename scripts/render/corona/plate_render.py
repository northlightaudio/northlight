"""Blender (4.2) script: Corona's flat parts, each seen head-on and lit as every part is
(the key as a distant sun from the top of the unit, a soft fall-off from the top, a
broad soft source high above for the metal's sheen, the room in the reflections),
its shadow caught on a floor of what it sits on and otherwise transparent.

  panel      the amp's control plate, 1120 x 250 page px: brushed champagne-gold
             anodised aluminium, grain horizontal, corners 6 px, a rounded chamfer
             catching the top light, four black oxide slotted screws 16 px in.
  cab-panel  the cabinet page's plate, 420 x 464, the same finish and screws.
  strip      the utility strip, 1120 x 92: dark graphite anodised aluminium, faint
             brushing, four hex-socket button screws.
  baffle     the cabinet's baffle, 680 x 464: matte black paint over plywood, the
             grain faint through it, a thin raised lip round its edge.
  grille     the grille, 1120 x 198: the cloth (weave_texture.py) recessed in a dark
             frame with brass piping round it.
  badge      the blank brass badge, 280 x 80 with its shadow inside that, sitting
             on the cloth: rounded ends, brushed, slightly domed, two rivets.

Every plate but the badge has a 12 px margin on every side for its shadow. Images
are at twice the page size (100 px per unit). The camera is 80 units away, so the
plate keeps its outline to a pixel while the sheen still drifts across it.

Run: blender --background --factory-startup --python plate_render.py -- <kind> <out.png> [weave_colour.png weave_height.png]
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402
import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
KIND, OUT = argv[0], argv[1]
WEAVE = argv[2:4]
PX = 100.0                       # image px per unit (twice the page's 50)
MARGIN = 12 / 50                 # 12 page px
SHEEN = float(os.environ.get("PLATE_SHEEN", "1.0"))

SIZES = {                        # page px
    "panel": (1120, 250), "cab-panel": (420, 464), "strip": (1120, 92),
    "baffle": (680, 464), "grille": (1120, 198), "badge": (280, 80),
}
pw, ph = SIZES[KIND]
W2, H2 = pw / 50 / 2, ph / 50 / 2          # half size in units
scene = C.reset()
L = None


# ----- materials --------------------------------------------------------------------------------
def anodised(name, colour, rough=(0.26, 0.36), brush=0.25, flecks=0.0):
    """Brushed, anodised aluminium: grain along X (anisotropic, the highlights drawn
    across it), the brushing a fine streaked bump with roughness and tone varying
    along it, a few long scratches, faint mottling in the dye."""
    m = C.material(name, C.lin(colour), metallic=1.0)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    streak = C.noise(nt, co, 1.0, sx=0.6, sy=520.0, detail=10.0, rough=0.62)
    streak2 = C.noise(nt, co, 1.0, sx=3.0, sy=1500.0, detail=4.0, rough=0.5)
    scratch = C.ramp(nt, C.noise(nt, co, 1.0, sx=0.08, sy=180.0, detail=3.0), 0.7, 0.74)
    h = C.math_node(nt, "MULTIPLY_ADD", streak2, 0.5, streak)
    h = C.math_node(nt, "MULTIPLY_ADD", scratch, -0.6, h)
    bump = C.node(nt, "ShaderNodeBump", Strength=brush, Distance=0.002)
    nt.links.new(h, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    p.inputs["Anisotropic"].default_value = 0.75
    tan = C.node(nt, "ShaderNodeCombineXYZ", X=1.0, Y=0.0, Z=0.0)
    nt.links.new(tan.outputs["Vector"], p.inputs["Tangent"])
    r = C.ramp(nt, streak, 0.3, 0.7, rough[0], rough[1])
    nt.links.new(r, p.inputs["Roughness"])
    mott = C.ramp(nt, C.noise(nt, co, 0.5, detail=4.0), 0.3, 0.7, 0.93, 1.05)
    tone = C.ramp(nt, streak, 0.2, 0.8, 0.96, 1.03)
    t = C.math_node(nt, "MULTIPLY", mott, tone)
    cm = nt.nodes.new("ShaderNodeCombineColor")
    for ch in ("Red", "Green", "Blue"):
        nt.links.new(t, cm.inputs[ch])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.blend_type = "MULTIPLY"
    mix.inputs["Factor"].default_value = 1.0
    mix.inputs["A"].default_value = (*C.lin(colour), 1)
    nt.links.new(cm.outputs["Color"], mix.inputs["B"])
    nt.links.new(mix.outputs["Result"], p.inputs["Base Color"])
    return m


def black_oxide():
    m = C.material("BlackOxide", (0.05, 0.048, 0.046), metallic=0.9, rough=0.3)
    nt = m.node_tree
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    r = C.ramp(nt, C.noise(nt, co, 40.0, detail=6.0), 0.3, 0.7, 0.24, 0.42)
    nt.links.new(r, nt.nodes["Principled BSDF"].inputs["Roughness"])
    return m


def brass(name="Brass", brushed=False, tarnish=0.6):
    m = C.material(name, C.lin("#C9A262"), metallic=1.0, rough=0.24)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    # Tarnish: duller, darker patches.
    tarn = C.ramp(nt, C.noise(nt, co, 3.0, detail=6.0), 0.45, 0.7)
    col = nt.nodes.new("ShaderNodeMix")
    col.data_type = "RGBA"
    col.inputs["A"].default_value = (*C.lin("#C9A262"), 1)
    col.inputs["B"].default_value = (*C.lin("#8E6E3E"), 1)
    nt.links.new(C.math_node(nt, "MULTIPLY", tarn, tarnish), col.inputs["Factor"])
    nt.links.new(col.outputs["Result"], p.inputs["Base Color"])
    rr = C.ramp(nt, tarn, 0.0, 1.0, 0.22, 0.22 + 0.3 * tarnish)
    if brushed:
        streak = C.noise(nt, co, 1.0, sx=0.8, sy=420.0, detail=10.0, rough=0.62)
        bump = C.node(nt, "ShaderNodeBump", Strength=0.35, Distance=0.002)
        nt.links.new(streak, bump.inputs["Height"])
        nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
        p.inputs["Anisotropic"].default_value = 0.7
        tan = C.node(nt, "ShaderNodeCombineXYZ", X=1.0, Y=0.0, Z=0.0)
        nt.links.new(tan.outputs["Vector"], p.inputs["Tangent"])
        rr = C.math_node(nt, "MULTIPLY_ADD", streak, 0.1, rr)
    nt.links.new(rr, p.inputs["Roughness"])
    return m


def painted_ply():
    """Matte black paint over plywood: the grain shows through as a faint ripple."""
    m = C.material("BlackPaint", C.lin("#121110"), rough=0.8)
    m.node_tree.nodes["Principled BSDF"].inputs["Specular IOR Level"].default_value = 0.3
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    warp = C.noise(nt, co, 0.7, sx=0.15, sy=1.0, detail=3.0)
    wave = nt.nodes.new("ShaderNodeTexWave")
    wave.wave_type = "BANDS"
    wave.bands_direction = "Y"
    wave.inputs["Scale"].default_value = 2.2
    wave.inputs["Distortion"].default_value = 6.0
    wave.inputs["Detail"].default_value = 3.0
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (0.25, 1.0, 1.0)
    nt.links.new(co, mp.inputs["Vector"])
    vm = nt.nodes.new("ShaderNodeVectorMath")
    vm.operation = "ADD"
    nt.links.new(mp.outputs["Vector"], vm.inputs[0])
    wc = C.node(nt, "ShaderNodeCombineXYZ")
    nt.links.new(warp, wc.inputs["Y"])
    nt.links.new(wc.outputs["Vector"], vm.inputs[1])
    nt.links.new(vm.outputs["Vector"], wave.inputs["Vector"])
    fibre = C.noise(nt, co, 1.0, sx=1.0, sy=90.0, detail=6.0)
    h = C.math_node(nt, "MULTIPLY_ADD", wave.outputs["Fac"], 0.7, C.math_node(nt, "MULTIPLY", fibre, 0.3))
    peel = C.noise(nt, co, 60.0, detail=3.0)
    h = C.math_node(nt, "MULTIPLY_ADD", peel, 0.25, h)
    bump = C.node(nt, "ShaderNodeBump", Strength=0.18, Distance=0.003)
    nt.links.new(h, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    tone = C.ramp(nt, wave.outputs["Fac"], 0.0, 1.0, 0.85, 1.12)
    cm = nt.nodes.new("ShaderNodeCombineColor")
    for ch in ("Red", "Green", "Blue"):
        nt.links.new(tone, cm.inputs[ch])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.blend_type = "MULTIPLY"
    mix.inputs["Factor"].default_value = 1.0
    mix.inputs["A"].default_value = (*C.lin("#121110"), 1)
    nt.links.new(cm.outputs["Color"], mix.inputs["B"])
    nt.links.new(mix.outputs["Result"], p.inputs["Base Color"])
    rr = C.ramp(nt, peel, 0.3, 0.7, 0.74, 0.86)
    nt.links.new(rr, p.inputs["Roughness"])
    return m


def cloth():
    m = C.material("Cloth", (0.5, 0.4, 0.3), rough=0.85)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    THREAD = 0.1                              # units per thread (10 image px)
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1 / (8 * THREAD), 1 / (8 * THREAD), 1)
    nt.links.new(co, mp.inputs["Vector"])
    ci = nt.nodes.new("ShaderNodeTexImage")
    ci.image = bpy.data.images.load(WEAVE[0])
    ci.interpolation = "Cubic"
    hi = nt.nodes.new("ShaderNodeTexImage")
    hi.image = bpy.data.images.load(WEAVE[1])
    hi.image.colorspace_settings.name = "Non-Color"
    hi.interpolation = "Cubic"
    for t in (ci, hi):
        nt.links.new(mp.outputs["Vector"], t.inputs["Vector"])
    # The cloth is never quite even: a soft, broad unevenness in its tone.
    tone = C.ramp(nt, C.noise(nt, co, 0.8, detail=4.0), 0.3, 0.7, 0.9, 1.06)
    tint = nt.nodes.new("ShaderNodeMix")
    tint.data_type = "RGBA"
    tint.blend_type = "MULTIPLY"
    tint.inputs["Factor"].default_value = 1.0
    nt.links.new(ci.outputs["Color"], tint.inputs["A"])
    tc = nt.nodes.new("ShaderNodeCombineColor")
    for ch in ("Red", "Green", "Blue"):
        nt.links.new(tone, tc.inputs[ch])
    nt.links.new(tc.outputs["Color"], tint.inputs["B"])
    nt.links.new(tint.outputs["Result"], p.inputs["Base Color"])
    bump = C.node(nt, "ShaderNodeBump", Strength=0.9, Distance=0.02)
    nt.links.new(hi.outputs["Color"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    p.inputs["Sheen Weight"].default_value = 0.6
    p.inputs["Sheen Roughness"].default_value = 0.4
    p.inputs["Specular IOR Level"].default_value = 0.3
    return m


# ----- geometry ---------------------------------------------------------------------------------
def plate(name, w, h, r, z0, z1, edge, mat):
    """A rounded-rectangle slab from z0 to z1, its top edge rounded over `edge`."""
    rings = [[(x, y, z0) for x, y in C.rrect(w, h, r)]]
    rings.append([(x, y, z1 - edge) for x, y in C.rrect(w, h, r)])
    for k in range(1, 7):
        a = math.radians(90 * k / 6)
        inset = edge * (1 - math.cos(a))
        z = z1 - edge + edge * math.sin(a)
        rings.append([(x, y, z) for x, y in C.rrect(w, h, r, inset)])
    ob = C.loft(scene, name, rings, mat)
    C.smooth_by_angle(ob, 40)
    return ob


def ring_frame(name, w, h, r, inset, z0, z1, edge, mat):
    """A rounded-rectangle frame `inset` wide from z0 to z1, both top edges rounded."""
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


def tube_along(name, path, radius, z, mat, segs=16):
    """A round cord swept along a closed path of (x, y) points at height z."""
    bm = bmesh.new()
    n = len(path)
    rings = []
    for i in range(n):
        p0, p1 = Vector((*path[i - 1], 0)), Vector((*path[(i + 1) % n], 0))
        t = (p1 - p0).normalized()
        out = Vector((t.y, -t.x, 0))
        c = Vector((*path[i], z))
        rings.append([bm.verts.new(c + radius * (math.cos(2 * math.pi * k / segs) * out
                                                  + math.sin(2 * math.pi * k / segs) * Vector((0, 0, 1))))
                      for k in range(segs)])
    for i in range(n):
        a, b = rings[i], rings[(i + 1) % n]
        for k in range(segs):
            m = (k + 1) % segs
            bm.faces.new((a[k], a[m], b[m], b[k]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    ob = C.link(scene, name, bm, mat)
    C.smooth(ob)
    return ob


def dedupe(path, eps=1e-5):
    out = []
    for p in path:
        if not out or (abs(p[0] - out[-1][0]) > eps or abs(p[1] - out[-1][1]) > eps):
            out.append(p)
    if abs(out[0][0] - out[-1][0]) < eps and abs(out[0][1] - out[-1][1]) < eps:
        out.pop()
    return out


def screw(x, y, z, kind, mat, angle):
    """A small screw head on the plate at (x, y, z): a slotted pan head, or a
    button head with a hex socket."""
    R = 0.11
    prof = [(0.0, z + 0.05), (R * 0.45, z + 0.048), (R * 0.75, z + 0.04), (R * 0.93, z + 0.026),
            (R, z + 0.012), (R, z), (0.0, z)] if kind == "slot" else \
           [(0.0, z + 0.042), (R * 0.5, z + 0.039), (R * 0.8, z + 0.03), (R * 0.96, z + 0.016),
            (R, z + 0.006), (R, z), (0.0, z)]
    head = C.revolve(scene, "Screw", prof, mat, segments=64)
    head.location = (x, y, 0)
    if kind == "slot":
        bpy.ops.mesh.primitive_cube_add(size=1, location=(x, y, z + 0.052))
        cut = bpy.context.active_object
        cut.scale = (R * 2.4, 0.026, 0.06)
        cut.rotation_euler = (0, 0, angle)
    else:
        bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.042, depth=0.08, location=(x, y, z + 0.055))
        cut = bpy.context.active_object
        cut.rotation_euler = (0, 0, angle)
    cut.hide_render = True
    cut.display_type = "WIRE"
    b = head.modifiers.new("cut", "BOOLEAN")
    b.operation = "DIFFERENCE"
    b.solver = "EXACT"
    b.object = cut
    return head


def screws(w, h, z, kind, mat, inset=16 / 50):
    angles = (0.4, 1.9, 2.7, 1.1)
    for (sx, sy), a in zip(((-1, 1), (1, 1), (1, -1), (-1, -1)), angles):
        screw(sx * (w - inset), sy * (h - inset), z, kind, mat, a)


# ----- the part ---------------------------------------------------------------------------------
R6 = 6 / 50
floor_colour = C.TOLEX
top = 0.16
if KIND in ("panel", "cab-panel"):
    z0, top = 0.08, 0.16                      # on stand-offs, so it casts a soft shadow
    plate("Plate", W2, H2, R6, z0, top, 0.035, anodised("Champagne", os.environ.get("PLATE_COLOUR", "#D2C29E"), brush=0.45))
    screws(W2, H2, top, "slot", black_oxide())
elif KIND == "strip":
    z0, top = 0.08, 0.16
    plate("Strip", W2, H2, R6, z0, top, 0.03, anodised("Graphite", "#2B2A2A", rough=(0.34, 0.46), brush=0.15))
    screws(W2, H2, top, "hex", black_oxide(), inset=14 / 50)
elif KIND == "baffle":
    top = 0.12
    plate("Board", W2, H2, R6, 0.0, top, 0.012, painted_ply())
    ring_frame("Lip", W2, H2, R6, 0.1, top - 0.01, top + 0.09, 0.025, painted_ply())
    top += 0.09
elif KIND == "grille":
    frame_w, pipe_r = 0.16, 0.05
    top = 0.24
    ring_frame("Frame", W2, H2, R6, frame_w, 0.0, top, 0.03, painted_ply())
    path = dedupe(C.rrect(W2, H2, R6, frame_w + pipe_r * 0.6, segs=16))
    tube_along("Piping", path, pipe_r, top - 0.01, brass())
    bm = bmesh.new()
    for x, y in ((-W2, -H2), (W2, -H2), (W2, H2), (-W2, H2)):
        bm.verts.new((x, y, 0.02))
    bm.faces.new(bm.verts[:])
    C.link(scene, "Cloth", bm, cloth())
elif KIND == "badge":
    floor_colour = C.CLOTH
    bw, bh = 268 / 50 / 2, 68 / 50 / 2
    cy = 0.04                                  # a touch high in its frame: the shadow falls below
    z0 = 0.05
    rings = [[(x, y + cy, z0) for x, y in C.rrect(bw, bh, bh, segs=24)]]
    rings.append([(x, y + cy, z0 + 0.025) for x, y in C.rrect(bw, bh, bh, segs=24)])
    for k in range(1, 5):                      # the rounded rim
        a = math.radians(90 * k / 4)
        e = 0.02
        rings.append([(x, y + cy, z0 + 0.025 + e * math.sin(a)) for x, y in C.rrect(bw, bh, bh, e * (1 - math.cos(a)), segs=24)])
    for k in range(1, 9):                      # the dome
        t = k / 8
        inset = 0.02 + t * (bh - 0.1)
        rings.append([(x, y + cy, z0 + 0.045 + 0.04 * (1 - (1 - t) ** 2)) for x, y in C.rrect(bw, bh, bh, inset, segs=24)])
    body = C.loft(scene, "Badge", rings, brass("BrushedBrass", brushed=True, tarnish=0.2))
    C.smooth_by_angle(body, 50)
    for sx in (-1, 1):
        x = sx * (bw - bh * 0.95)
        zt = z0 + 0.078                        # the dome's height out near the ends
        C.revolve(scene, "Rivet", [(0.0, zt + 0.04), (0.05, zt + 0.035), (0.085, zt + 0.018), (0.1, zt), (0.0, zt)],
                  brass("Rivet")).location = (x, cy, 0)
    top = z0 + 0.09

C.shadow_floor(scene, floor_colour, size=60)

# ----- camera and light -------------------------------------------------------------------------
margin = 0.0 if KIND == "badge" else MARGIN
fw, fh = 2 * (W2 + margin), 2 * (H2 + margin)
C.camera_down(scene, fw, fh, plane_z=top, dist=80.0)
C.key_sun(scene, energy=2.3, angle=float(os.environ.get("PLATE_SUN_ANGLE", "12")))
C.area(scene, "TopFall", 30.0, 900, (0, 6 + 4, 7), (0, 0, 0), shape="RECTANGLE", size_y=3.0, glossy=False)
C.area(scene, "Sheen", 34.0, 1800 * SHEEN, (0, 9, 30), (0, 0, 0), shape="RECTANGLE", size_y=14.0,
       diffuse=False, shadow=False)
C.room(scene)
C.render(scene, OUT, round(fw * PX), round(fh * PX), samples=1024, transparent=True, denoise=os.environ.get("PLATE_DENOISE", "0") == "1")
