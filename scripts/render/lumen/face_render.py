"""Blender (4.2) script: Lumen's face, the front of a 1970s suitcase electric piano, and the
layers its card draws it with (screens/plugins.md 7.4h).

The face is 1160 x 600 px at 50 px per unit: 23.2 x 12 units, origin at its centre, x right, y
up, z towards the camera (1 unit is about 20 mm). Seen from 40 units straight in front, lit by a
key from the top of the unit, a warm fill from below and the lounge HDRI in reflections only.
The layout is mirrored in src/ui/strip/LumenFace.h: move a part in both.

Above a chrome trim, black tolex: the harp window (a chromed bezel and glass, four tine
assemblies inside it, each a brass tone bar over a steel tine whose tip sits at a pickup, lit
by a lamp inside), a black anodised nameplate, three service trims and the pilot lamp. Below
it, the brushed aluminium preamp plate: skirted bakelite knobs and the vibrato's two jewel
lamps. Nothing printed is rendered (the card prints every word), and the vibrato's lever is
the tape echo's render, drawn by the card.

  LUMEN_LAYER=deck      everything still: tines at rest, knob bodies, lamps dark
  LUMEN_LAYER=lit       the same with the pilot and vibrato lamps lit (a lit lamp's patch)
  LUMEN_LAYER=pointers  the knobs' pointers alone, each pointing up, as colour with alpha
  LUMEN_SWING=<s>       the tines bent, s from -1 (tips down) to 1 (tips up)

Run: blender --background --factory-startup --python face_render.py -- <out.png> [scale] [samples]
Env: LUMEN_GPU=1 renders on the GPU (default CPU).
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
OUT = argv[0]
SCALE = float(argv[1]) if len(argv) > 1 else 1.0
SAMPLES = int(argv[2]) if len(argv) > 2 else 128
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from render_config import hdri_path  # noqa: E402  (scripts/render: NORTHLIGHT_HDRI or render.local.json)
ROOM = hdri_path()
FACE_W, FACE_H = 23.2, 12.0
LAYER = os.environ.get("LUMEN_LAYER", "deck")
SWING = float(os.environ.get("LUMEN_SWING", "0"))
POINTERS = []

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
random.seed(23)

src = open(os.path.join(REPO, "scripts", "render", "tapeecho", "vu_render.py"), encoding="utf-8").read()
exec(src[src.index("# ----- geometry helpers"):src.index("liner_mat = plain(")])


def U(px):
    return (px - 580) / 50


def V(py):
    return (300 - py) / 50


def PX(px):
    return px / 50


# ----- materials ---------------------------------------------------------------------------------
def brushed(name, base=(0.80, 0.81, 0.82), rough=0.26):
    m = plain(name, base, metallic=1.0, rough=rough)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Anisotropic"].default_value = 0.75
    # The grain runs straight across the plate (a tangent round the y axis lies along x).
    tg = nt.nodes.new("ShaderNodeTangent")
    tg.direction_type = "RADIAL"
    tg.axis = "Y"
    nt.links.new(tg.outputs["Tangent"], p.inputs["Tangent"])
    co = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (0.6, 260.0, 1.0)
    nt.links.new(co.outputs["Object"], mp.inputs["Vector"])
    n = node(nt, "ShaderNodeTexNoise", Scale=3.0, Detail=12.0, Roughness=0.7)
    nt.links.new(mp.outputs["Vector"], n.inputs["Vector"])
    b = node(nt, "ShaderNodeBump", Strength=0.12, Distance=0.002)
    nt.links.new(n.outputs["Fac"], b.inputs["Height"])
    nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    rr = node(nt, "ShaderNodeMapRange", **{"From Min": 0.3, "From Max": 0.7, "To Min": rough - 0.05,
                                            "To Max": rough + 0.07})
    big = node(nt, "ShaderNodeTexNoise", Scale=2.0, Detail=4.0)
    nt.links.new(big.outputs["Fac"], rr.inputs["Value"])
    nt.links.new(rr.outputs["Result"], p.inputs["Roughness"])
    return m


def spun(name, base=(0.82, 0.82, 0.81), rough=0.2):
    """Aluminium turned on a lathe: the grain runs round the object's own z axis, in fine rings."""
    m = plain(name, base, metallic=1.0, rough=rough)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Anisotropic"].default_value = 0.85
    t = nt.nodes.new("ShaderNodeTangent")
    t.direction_type = "RADIAL"
    t.axis = "Z"
    nt.links.new(t.outputs["Tangent"], p.inputs["Tangent"])
    co = nt.nodes.new("ShaderNodeTexCoord")
    w = nt.nodes.new("ShaderNodeTexWave")
    w.wave_type = "RINGS"
    w.rings_direction = "Z"
    w.inputs["Scale"].default_value = 60.0
    w.inputs["Distortion"].default_value = 1.5
    nt.links.new(co.outputs["Object"], w.inputs["Vector"])
    b = node(nt, "ShaderNodeBump", Strength=0.03, Distance=0.001)
    nt.links.new(w.outputs["Fac"], b.inputs["Height"])
    nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    return m


def tolex(name):
    """The case's vinyl: black, a fine pebbled grain, a soft sheen, rubbed glossier in patches."""
    m = plain(name, (0.014, 0.013, 0.013), rough=0.62)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Coat Weight"].default_value = 0.08
    co = nt.nodes.new("ShaderNodeTexCoord")
    vo = nt.nodes.new("ShaderNodeTexVoronoi")
    vo.inputs["Scale"].default_value = 70.0
    nt.links.new(co.outputs["Object"], vo.inputs["Vector"])
    n = node(nt, "ShaderNodeTexNoise", Scale=160.0, Detail=4.0, Roughness=0.6)
    nt.links.new(co.outputs["Object"], n.inputs["Vector"])
    add = nt.nodes.new("ShaderNodeMath")
    add.operation = "ADD"
    nt.links.new(vo.outputs["Distance"], add.inputs[0])
    nt.links.new(n.outputs["Fac"], add.inputs[1])
    b = node(nt, "ShaderNodeBump", Strength=0.45, Distance=0.01)
    nt.links.new(add.outputs["Value"], b.inputs["Height"])
    nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    rub = node(nt, "ShaderNodeTexNoise", Scale=1.2, Detail=3.0)
    rr = node(nt, "ShaderNodeMapRange", **{"From Min": 0.35, "From Max": 0.7, "To Min": 0.66, "To Max": 0.46})
    nt.links.new(rub.outputs["Fac"], rr.inputs["Value"])
    nt.links.new(rr.outputs["Result"], p.inputs["Roughness"])
    return m


def glowing(name, base, emit, strength):
    m = plain(name, base, rough=0.3)
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Emission Color"].default_value = (*emit, 1)
    p.inputs["Emission Strength"].default_value = strength
    return m


def glass(name, tint=(1, 1, 1), rough=0.02):
    m = plain(name, tint, rough=rough)
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Transmission Weight"].default_value = 1.0
    p.inputs["IOR"].default_value = 1.5
    return m


def felt(name):
    m = plain(name, (0.045, 0.022, 0.018), rough=0.95)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    n = node(nt, "ShaderNodeTexNoise", Scale=90.0, Detail=8.0, Roughness=0.8)
    b = node(nt, "ShaderNodeBump", Strength=0.3, Distance=0.01)
    nt.links.new(n.outputs["Fac"], b.inputs["Height"])
    nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    return m


case_mat = tolex("Tolex")
plate_mat = brushed("Plate", base=(0.74, 0.73, 0.70), rough=0.3)
cap_mat = spun("CapInsert")
chrome = plain("Chrome", (0.84, 0.83, 0.81), metallic=1.0, rough=0.1)
steel = plain("Steel", (0.62, 0.61, 0.59), metallic=1.0, rough=0.3)
tine_steel = plain("TineSteel", (0.78, 0.77, 0.75), metallic=1.0, rough=0.18)
dark_steel = plain("DarkSteel", (0.09, 0.09, 0.095), metallic=0.8, rough=0.4)
brass = plain("Brass", (0.80, 0.58, 0.28), metallic=1.0, rough=0.3)
copper = plain("Copper", (0.78, 0.40, 0.24), metallic=1.0, rough=0.38)
bobbin = plain("Bobbin", (0.02, 0.02, 0.022), rough=0.45)
felt_mat = felt("Felt")
well_mat = plain("Well", (0.01, 0.01, 0.01), rough=0.6)
knob_mat = bakelite("Knob")
pointer_mat = plain("Pointer", (0.025, 0.022, 0.02), rough=0.45)
nameplate_mat = plain("Nameplate", (0.018, 0.018, 0.02), metallic=0.6, rough=0.32)
glass_mat = glass("Glass")
LIT = LAYER == "lit"
# Lit, a jewel glows its own colour without clipping to white: a deep emission, kept low.
pilot_mat = glowing("Pilot", (0.3, 0.015, 0.008), (1.0, 0.04, 0.015), 1.4) if LIT else plain(
    "PilotOff", (0.2, 0.02, 0.012), rough=0.15)
amber_mat = glowing("Amber", (0.45, 0.16, 0.02), (1.0, 0.36, 0.04), 1.2) if LIT else plain(
    "AmberOff", (0.24, 0.12, 0.03), rough=0.15)


# ----- shapes ------------------------------------------------------------------------------------
def box(name, x0, x1, y0, y1, z0, z1, mat, bev=0.0, seg=3):
    bpy.ops.mesh.primitive_cube_add(size=1, location=((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
    ob = bpy.context.active_object
    ob.name = name
    ob.scale = (x1 - x0, y1 - y0, z1 - z0)
    bpy.ops.object.transform_apply(scale=True)
    ob.data.materials.append(mat)
    if bev:
        bevel(ob, bev, seg)
        smooth_by_angle(ob)
    return ob


def cbox(name, cx, cy, w, h, z0, z1, mat, bev=0.0, rot=0.0):
    ob = box(name, -w / 2, w / 2, -h / 2, h / 2, z0, z1, mat, bev)
    ob.location = (cx, cy, 0)
    ob.rotation_euler = (0, 0, rot)
    return ob


def cyl(name, x, y, r, z0, z1, mat, bev=0.0, verts=96):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=z1 - z0, location=(x, y, (z0 + z1) / 2))
    ob = bpy.context.active_object
    ob.name = name
    ob.data.materials.append(mat)
    if bev:
        bevel(ob, bev, 3)
    smooth_by_angle(ob)
    return ob


def xcyl(name, x0, x1, y, z, r, mat, bev=0.0, verts=64):
    """A cylinder lying along x, from x0 to x1."""
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=x1 - x0,
                                        location=((x0 + x1) / 2, y, z), rotation=(0, math.pi / 2, 0))
    ob = bpy.context.active_object
    ob.name = name
    ob.data.materials.append(mat)
    if bev:
        bevel(ob, bev, 3)
    smooth_by_angle(ob)
    return ob


def bar(name, x0, y0, x1, y1, w, z0, z1, mat):
    L = math.hypot(x1 - x0, y1 - y0)
    return cbox(name, (x0 + x1) / 2, (y0 + y1) / 2, L, w, z0, z1, mat, rot=math.atan2(y1 - y0, x1 - x0))


def polar(cx, cy, r, deg):
    a = math.radians(deg)
    return cx + r * math.sin(a), cy + r * math.cos(a)


def screw(x, y, z=0.0, r=0.09):
    cyl("Screw", x, y, r, z, z + 0.045, steel, bev=r * 0.4, verts=48)
    cbox("Slot", x, y, r * 1.8, r * 0.26, z + 0.035, z + 0.05, well_mat, rot=random.uniform(0, math.pi))


def frame(name, outer, inner, z0, z1, mat, bev=0.03):
    """A rectangular frame (x0, y0, x1, y1 in units), outer minus inner."""
    ob = box(name, outer[0], outer[2], outer[1], outer[3], z0, z1, mat)
    cut = box("Cut", inner[0], inner[2], inner[1], inner[3], z0 - 1, z1 + 1, well_mat)
    cut.hide_render = True
    cut.hide_viewport = True
    m = ob.modifiers.new("hole", "BOOLEAN")
    m.operation = "DIFFERENCE"
    m.object = cut
    m.solver = "EXACT"
    if bev:
        bevel(ob, bev, 4)
        smooth_by_angle(ob)
    return ob


def rect_units(r):
    """A rectangle in face px (x, y, w, h) as units (x0, y0, x1, y1)."""
    x, y, w, h = r
    return (U(x), V(y + h), U(x + w), V(y))


# ----- the case, trim and plate ------------------------------------------------------------------
WIN_BEZEL = (30, 22, 590, 222)
WIN_GLASS = (44, 36, 562, 194)
case = frame("Case", (-FACE_W / 2 - 0.2, V(254), FACE_W / 2 + 0.2, FACE_H / 2 + 0.2),
             rect_units(WIN_GLASS), -0.3, 0.0, case_mat, bev=0.0)
box("Trim", -FACE_W / 2, FACE_W / 2, V(264), V(254), -0.3, 0.07, chrome, bev=0.035, seg=4)
box("Plate", -FACE_W / 2, FACE_W / 2, -FACE_H / 2 - 0.2, V(264), -0.3, 0.0, plate_mat, bev=0.02)
for sx, sy in ((18, 282), (1142, 282), (18, 582), (1142, 582)):
    screw(U(sx), V(sy))

# ----- the harp window ---------------------------------------------------------------------------
frame("Bezel", rect_units(WIN_BEZEL), rect_units(WIN_GLASS), -0.05, 0.09, chrome, bev=0.04)
gx0, gy0, gx1, gy1 = rect_units(WIN_GLASS)
DEPTH = 1.4
box("Back", gx0 - 0.3, gx1 + 0.3, gy0 - 0.3, gy1 + 0.3, -DEPTH - 0.1, -DEPTH, felt_mat)
for x0, x1, y0, y1 in ((gx0 - 0.3, gx0, gy0, gy1), (gx1, gx1 + 0.3, gy0, gy1),
                       (gx0 - 0.3, gx1 + 0.3, gy0 - 0.3, gy0), (gx0 - 0.3, gx1 + 0.3, gy1, gy1 + 0.3)):
    box("Wall", x0, x1, y0, y1, -DEPTH, -0.05, plain("Wall", (0.02, 0.018, 0.017), rough=0.8))
pane = box("Glass", gx0 - 0.05, gx1 + 0.05, gy0 - 0.05, gy1 + 0.05, -0.02, -0.005, glass_mat)
pane.visible_shadow = False

# The tines: four assemblies, the longest (lowest note) at the bottom.
TZ = -0.75                          # the tines' depth in the window
SWING_UNITS = PX(7)                 # a full swing moves a tip 7 px
TINES = ((200, 520), (158, 482), (116, 444), (74, 406))   # (row y, tip x) in face px
for row_y, tip_x in TINES:
    y = V(row_y)
    x_clamp = U(58)
    x_tip = U(tip_x)
    # The mounting block and its screw.
    box("Mount", U(50), U(86), y - PX(14), y + PX(18), TZ - 0.4, TZ + 0.12, dark_steel, bev=0.03)
    screw(U(68), y + PX(6), TZ + 0.12, 0.07)
    # The tone bar over the tine, from the block to about 62% of its length.
    bar_end = U(86) + (x_tip - U(86)) * 0.62
    box("ToneBar", U(84), bar_end, y + PX(8), y + PX(15), TZ - 0.12, TZ + 0.02, brass, bev=0.03)
    # The tine: a thin steel rod bent as a clamped rod bends, its tip moving most.
    cu = bpy.data.curves.new("Tine", "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = PX(1.7)
    cu.bevel_resolution = 4
    cu.use_fill_caps = True
    sp = cu.splines.new("POLY")
    n = 32
    sp.points.add(n - 1)

    def bend(t):
        return SWING * SWING_UNITS * t * t * (3 - t) * 0.5

    x0 = U(86)
    for i in range(n):
        t = i / (n - 1)
        sp.points[i].co = (x0 + (x_tip - x0) * t, y + bend(t), TZ, 1)
    tine = bpy.data.objects.new("Tine", cu)
    scene.collection.objects.link(tine)
    tine.data.materials.append(tine_steel)
    # Its tuning spring, a few turns round it two thirds out.
    ts = 0.7
    for k in range(5):
        t = ts + k * 0.012
        bpy.ops.mesh.primitive_torus_add(major_radius=PX(2.6), minor_radius=PX(0.8),
                                         location=(x0 + (x_tip - x0) * t, y + bend(t), TZ),
                                         rotation=(0, math.pi / 2, 0), major_segments=24, minor_segments=8)
        bpy.context.active_object.data.materials.append(steel)
    # The pickup: a pole piece facing the tip, the coil wound on a black bobbin.
    px0 = x_tip + PX(10)
    xcyl("Pole", x_tip + PX(4), px0 + PX(4), y, TZ, PX(3.2), steel)
    xcyl("BobbinFront", px0, px0 + PX(4), y, TZ, PX(13), bobbin, bev=0.01)
    xcyl("Coil", px0 + PX(4), px0 + PX(30), y, TZ, PX(11.5), copper)
    xcyl("BobbinBack", px0 + PX(30), px0 + PX(34), y, TZ, PX(13), bobbin, bev=0.01)
    box("PickupBracket", px0 + PX(8), px0 + PX(28), y - PX(18), y - PX(12), TZ - 0.4, TZ - 0.1, dark_steel)

# A lamp inside the window, warm, from above the tines.
inner = bpy.data.lights.new("HarpLamp", "AREA")
inner.energy = float(os.environ.get("LUMEN_HARP_LAMP", "60"))
inner.color = (1.0, 0.78, 0.52)
inner.size = 6.0
inner.size_y = 0.4
inner.shape = "RECTANGLE"
io = bpy.data.objects.new("HarpLamp", inner)
io.location = ((gx0 + gx1) / 2, gy1 - 0.2, -0.25)
io.rotation_euler = (math.radians(-55), 0, 0)
scene.collection.objects.link(io)

# ----- nameplate, service trims, pilot lamp ------------------------------------------------------
NP = rect_units((650, 28, 480, 96))
box("Nameplate", NP[0], NP[2], NP[1], NP[3], 0.0, 0.06, nameplate_mat, bev=0.03)
for sx in (662, 1118):
    screw(U(sx), V(76), 0.06, 0.06)


def lamp(px, py, mat, r=0.2):
    cyl("LampBezel", U(px), V(py), r + 0.08, 0.0, 0.09, chrome, bev=0.03)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=r, location=(U(px), V(py), 0.1))
    jewel = bpy.context.active_object
    jewel.scale = (1, 1, 0.7)
    jewel.data.materials.append(mat)


lamp(1090, 186, pilot_mat, 0.22)
for lx in (566, 598):
    lamp(lx, 478, amber_mat, 0.15)


# ----- knobs -------------------------------------------------------------------------------------
def knob(px, py, service):
    cx, cy = U(px), V(py)
    r = PX(17) if service else PX(20)
    h = 0.18 if service else 0.24
    bm = bmesh.new()
    n = 120
    ring = [(cx + (r if i % 2 == 0 else r * 0.965) * math.cos(2 * math.pi * i / n),
             cy + (r if i % 2 == 0 else r * 0.965) * math.sin(2 * math.pi * i / n)) for i in range(n)]
    lo = [bm.verts.new((x, y, 0.0)) for x, y in ring]
    hi = [bm.verts.new((cx + (x - cx) * 0.94, cy + (y - cy) * 0.94, h)) for x, y in ring]
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    link("Skirt", bm, knob_mat)
    cyl("Cap", cx, cy, r * 0.7, h, h + 0.1, knob_mat, bev=0.04)
    # The cap's spun aluminium insert, which the pointer line is printed on.
    insert = cyl("Insert", 0, 0, r * 0.56, h + 0.085, h + 0.115, cap_mat, bev=0.01)
    insert.location = (cx, cy, insert.location.z)
    px0, py0 = polar(cx, cy, r * 0.12, 0)
    px1, py1 = polar(cx, cy, r * 0.64, 0)
    POINTERS.append(bar("Pointer", px0, py0, px1, py1, 0.045 if not service else 0.04, h + 0.1, h + 0.125,
                        pointer_mat))


PLATE_KNOBS = [(83, 348), (159, 348), (235, 348), (83, 478), (159, 478), (235, 478),
               (325, 348), (325, 478),
               (415, 348), (491, 348), (415, 478), (491, 478),
               (657, 348), (733, 348), (657, 478), (733, 478),
               (823, 348), (899, 348), (823, 478), (899, 478),
               (989, 348), (1065, 348), (1027, 478)]
SERVICE_KNOBS = [(700, 186), (790, 186), (880, 186)]
for kx, ky in PLATE_KNOBS:
    knob(kx, ky, False)
for kx, ky in SERVICE_KNOBS:
    knob(kx, ky, True)

# ----- camera ------------------------------------------------------------------------------------
DIST = 40.0
cam_data = bpy.data.cameras.new("Camera")
cam_data.type = "PERSP"
cam_data.sensor_fit = "HORIZONTAL"
cam_data.sensor_width = 36
cam_data.lens = DIST * 36 / FACE_W
cam_data.clip_end = 200
cam = bpy.data.objects.new("Camera", cam_data)
cam.location = (0, 0, DIST)
scene.collection.objects.link(cam)
scene.camera = cam

# ----- light: a key from the top of the unit, a warm fill from below, the room in reflections ----
sun_data = bpy.data.lights.new("Key", "SUN")
sun_data.energy = float(os.environ.get("LUMEN_KEY", "2.4"))
sun_data.angle = math.radians(24)
sun = bpy.data.objects.new("Key", sun_data)
sun.rotation_mode = "QUATERNION"
sun.rotation_quaternion = (Vector((0, 0, 0.3)) - Vector((-1.2, 3.4, 4.0))).to_track_quat("-Z", "Y")
scene.collection.objects.link(sun)

fill = bpy.data.lights.new("Fill", "AREA")
fill.energy = float(os.environ.get("LUMEN_FILL", "900"))
fill.color = (1.0, 0.74, 0.48)
fill.size = 10.0
fo = bpy.data.objects.new("Fill", fill)
fo.location = (2.0, -15.0, 6.0)
fo.rotation_mode = "QUATERNION"
fo.rotation_quaternion = (Vector((0.0, -3.0, 0.0)) - fo.location).to_track_quat("-Z", "Y")
scene.collection.objects.link(fo)

image = bpy.data.images.load(ROOM)
world = bpy.data.worlds.new("World")
world.use_nodes = True
wn = world.node_tree
wc = wn.nodes.new("ShaderNodeTexCoord")
wm = wn.nodes.new("ShaderNodeMapping")
wm.inputs["Rotation"].default_value = (0.0, 0.0, math.radians(200))
env = wn.nodes.new("ShaderNodeTexEnvironment")
env.image = image
wn.links.new(wc.outputs["Generated"], wm.inputs["Vector"])
wn.links.new(wm.outputs["Vector"], env.inputs["Vector"])
hue = wn.nodes.new("ShaderNodeHueSaturation")
hue.inputs["Saturation"].default_value = 0.5
wn.links.new(env.outputs["Color"], hue.inputs["Color"])
bg = wn.nodes["Background"]
bg.inputs["Strength"].default_value = 0.7
wn.links.new(hue.outputs["Color"], bg.inputs["Color"])
dim = wn.nodes.new("ShaderNodeBackground")
dim.inputs["Strength"].default_value = 0.15
wn.links.new(hue.outputs["Color"], dim.inputs["Color"])
lp = wn.nodes.new("ShaderNodeLightPath")
mixw = wn.nodes.new("ShaderNodeMixShader")
wn.links.new(lp.outputs["Is Glossy Ray"], mixw.inputs["Fac"])
wn.links.new(dim.outputs["Background"], mixw.inputs[1])
wn.links.new(bg.outputs["Background"], mixw.inputs[2])
wn.links.new(mixw.outputs["Shader"], wn.nodes["World Output"].inputs["Surface"])
scene.world = world

# ----- render ------------------------------------------------------------------------------------
scene.render.engine = "CYCLES"
scene.cycles.samples = SAMPLES
scene.cycles.use_denoising = True
scene.cycles.transparent_max_bounces = 16
scene.cycles.device = "CPU"
if os.environ.get("LUMEN_GPU"):
    prefs = bpy.context.preferences.addons["cycles"].preferences
    for device_type in ("OPTIX", "CUDA"):
        try:
            prefs.compute_device_type = device_type
            prefs.get_devices()
            if any(d.type == device_type for d in prefs.devices):
                for d in prefs.devices:
                    d.use = d.type == device_type
                scene.cycles.device = "GPU"
                break
        except TypeError:
            continue
else:
    scene.render.threads_mode = "FIXED"
    scene.render.threads = int(os.environ.get("LUMEN_THREADS", "8"))
scene.render.resolution_x = round(1160 * SCALE)
scene.render.resolution_y = round(600 * SCALE)
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGB"
scene.view_settings.view_transform = "Standard"

# ----- the layers ----------------------------------------------------------------------------------
if LAYER in ("deck", "lit"):
    for ob in POINTERS:
        ob.hide_render = True            # their own layer
elif LAYER == "pointers":
    for ob in scene.objects:
        if ob.type in {"MESH", "CURVE", "FONT"} and ob not in POINTERS:
            ob.visible_camera = False    # still there in the light, the shadows and the reflections
    scene.render.film_transparent = True
    scene.render.image_settings.color_mode = "RGBA"
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("rendered", OUT)
