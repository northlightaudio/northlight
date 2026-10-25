"""Blender (4.2) script: Orbit's face, a studio reel-to-reel in a dim mastering room, and the
layers its card draws it with (screens/plugins.md, Orbit).

The face is 1160 x 600 px at 50 px per unit: 23.2 x 12 units, origin at its centre, x right, y
up, z towards the camera (1 unit is about 20 mm). Seen from 40 units straight in front. Lit by a
warm spot pooled on the left reel, a cool rim from the right, a low warm fill along the panel
and the lounge HDRI in reflections only (ORBIT_MOOD=0: the tape echo's even key instead).

Nothing printed is rendered: the card prints every word, the meters' scales and the counter, so
they stay sharp at any size. ORBIT_COMP=1 renders them too, with needles and lit keys, as a
design comp of the whole face.

  ORBIT_LAYER=deck      everything still: the panels, reels at rest, knob bodies, keys unlit
  ORBIT_LAYER=lit       the same with every key and the pilot lamp lit (a lit key's patch)
  ORBIT_LAYER=pointers  the knobs' pointers alone, each pointing up, as colour with alpha
  ORBIT_LAYER=hood      the meters' shrouds alone (drawn over the needles)
  ORBIT_LAYER=glass     the meters' glass alone: its reflections as colour with alpha
  ORBIT_REEL_ANGLE=<d>  both reels turned <d> degrees anticlockwise (the turning frames)

Run: blender --background --factory-startup --python face_render.py -- <out.png> [scale] [samples]
Env: ORBIT_GPU=1 renders on the GPU (default CPU, so a game keeps the GPU);
     ORBIT_BORDER=x0,y0,x1,y1 renders only that part of the frame (0..1, y up).
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
COMP = os.environ.get("ORBIT_COMP") == "1"
LAYER = os.environ.get("ORBIT_LAYER", "deck")
REEL_ANGLE = float(os.environ.get("ORBIT_REEL_ANGLE", "0"))
MOOD = os.environ.get("ORBIT_MOOD", "1") == "1"
if MOOD:
    os.environ["ORBIT_MOOD"] = "1"
else:
    os.environ.pop("ORBIT_MOOD", None)
POINTERS, SHROUDS, GLASSES = [], [], []

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
random.seed(11)

os.environ["BEZEL_FINISH"] = "alu"
src = open(os.path.join(REPO, "scripts", "render", "tapeecho", "vu_render.py"), encoding="utf-8").read()
exec(src[src.index("# ----- geometry helpers"):src.index("liner_mat = plain(")])

BOLD = bpy.data.fonts.load(os.path.join(REPO, "assets", "fonts", "BarlowCondensed-Bold.ttf"))
SEMI = bpy.data.fonts.load(os.path.join(REPO, "assets", "fonts", "BarlowCondensed-SemiBold.ttf"))


def U(px):
    return (px - 580) / 50


def V(py):
    return (300 - py) / 50


# ----- materials ---------------------------------------------------------------------------------
def brushed(name, base=(0.80, 0.81, 0.82), rough=0.26):
    m = plain(name, base, metallic=1.0, rough=rough)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Anisotropic"].default_value = 0.75
    co = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (0.6, 260.0, 1.0)
    nt.links.new(co.outputs["Object"], mp.inputs["Vector"])
    n = node(nt, "ShaderNodeTexNoise", Scale=3.0, Detail=12.0, Roughness=0.7)
    nt.links.new(mp.outputs["Vector"], n.inputs["Vector"])
    b = node(nt, "ShaderNodeBump", Strength=0.12, Distance=0.002)
    nt.links.new(n.outputs["Fac"], b.inputs["Height"])
    nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    rr = node(nt, "ShaderNodeMapRange", **{"From Min": 0.3, "From Max": 0.7, "To Min": rough - 0.05, "To Max": rough + 0.07})
    big = node(nt, "ShaderNodeTexNoise", Scale=2.0, Detail=4.0)
    nt.links.new(big.outputs["Fac"], rr.inputs["Value"])
    nt.links.new(rr.outputs["Result"], p.inputs["Roughness"])
    return m


def spun(name, base=(0.80, 0.81, 0.82), rough=0.2):
    """Aluminium turned on a lathe: the grain runs round the object's own z axis."""
    m = plain(name, base, metallic=1.0, rough=rough)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Anisotropic"].default_value = 0.85
    t = nt.nodes.new("ShaderNodeTangent")
    t.direction_type = "RADIAL"
    t.axis = "Z"
    nt.links.new(t.outputs["Tangent"], p.inputs["Tangent"])
    rr = node(nt, "ShaderNodeMapRange", **{"From Min": 0.3, "From Max": 0.7, "To Min": rough - 0.04, "To Max": rough + 0.08})
    big = node(nt, "ShaderNodeTexNoise", Scale=1.5, Detail=4.0)
    nt.links.new(big.outputs["Fac"], rr.inputs["Value"])
    nt.links.new(rr.outputs["Result"], p.inputs["Roughness"])
    return m


def hammertone(name, base):
    m = plain(name, base, metallic=0.35, rough=0.42)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    n = node(nt, "ShaderNodeTexNoise", Scale=22.0, Detail=3.0, Roughness=0.55)
    v = nt.nodes.new("ShaderNodeTexVoronoi")
    v.inputs["Scale"].default_value = 30.0
    mx = node(nt, "ShaderNodeMath")
    mx.operation = "ADD"
    nt.links.new(n.outputs["Fac"], mx.inputs[0])
    nt.links.new(v.outputs["Distance"], mx.inputs[1])
    b = node(nt, "ShaderNodeBump", Strength=0.35, Distance=0.01)
    nt.links.new(mx.outputs["Value"], b.inputs["Height"])
    nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    return m


def glowing(name, base, emit, strength):
    m = plain(name, base, rough=0.35)
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


plate_mat = brushed("Plate")
panel_mat = hammertone("Panel", (0.075, 0.08, 0.085))
chrome = plain("Chrome", (0.82, 0.81, 0.79), metallic=1.0, rough=0.12)
steel = plain("Steel", (0.62, 0.61, 0.59), metallic=1.0, rough=0.3)
mumetal = plain("HeadCan", (0.72, 0.71, 0.68), metallic=1.0, rough=0.22)
hub_mat = plain("Hub", (0.02, 0.02, 0.021), rough=0.35)
tape_mat = plain("Tape", (0.11, 0.055, 0.025), rough=0.45)
rubber = plain("Rubber", (0.012, 0.012, 0.012), rough=0.75)
block_mat = plain("Block", (0.05, 0.052, 0.055), metallic=0.4, rough=0.38)
ink_dark = plain("InkDark", (0.018, 0.018, 0.02), rough=0.55)
ink_mid = plain("InkMid", (0.12, 0.125, 0.13), rough=0.5)
ink_cream = plain("InkCream", (0.72, 0.67, 0.56), rough=0.55)
ink_soft = plain("InkSoft", (0.36, 0.345, 0.31), rough=0.55)
rule_mat = plain("Rule", (0.2, 0.205, 0.21), rough=0.5)
knob_mat = bakelite("Knob")
pointer_mat = plain("Pointer", (0.85, 0.80, 0.68), rough=0.4)
key_mat = plain("Key", (0.92, 0.89, 0.80), rough=0.32)
# Unchosen keys glow faintly from behind (standby), so their labels read in the dark room.
key_mat.node_tree.nodes["Principled BSDF"].inputs["Emission Color"].default_value = (1.0, 0.92, 0.80, 1)
key_mat.node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = float(
    os.environ.get("ORBIT_KEY_GLOW", "0.2"))
key_mat.node_tree.nodes["Principled BSDF"].inputs["Coat Weight"].default_value = 0.4
key_lit = glowing("KeyLit", (0.85, 0.5, 0.18), (1.0, 0.5, 0.12), 0.7)
key_ink = plain("KeyInk", (0.03, 0.025, 0.02), rough=0.5)
well_mat = plain("Well", (0.01, 0.01, 0.01), rough=0.6)
vu_face = glowing("VuFace", (0.95, 0.88, 0.70), (1.0, 0.80, 0.52), float(os.environ.get("ORBIT_VU_GLOW", "0.08")))
vu_bezel = bakelite("VuBezel")
vu_ink = plain("VuInk", (0.03, 0.025, 0.02), rough=0.6)
vu_red = plain("VuRed", (0.55, 0.06, 0.03), rough=0.6)
bezel_black = plain("BezelBlack", (0.012, 0.012, 0.013), rough=0.3)
glass_mat = glass("Glass")
lamp_mat = glowing("Lamp", (0.35, 0.02, 0.01), (1.0, 0.06, 0.02), 1.6)
digit_mat = glowing("Digits", (0.9, 0.85, 0.72), (1.0, 0.92, 0.75), 1.4)
counter_black = plain("CounterBlack", (0.008, 0.008, 0.008), rough=0.5)


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


def bar(name, x0, y0, x1, y1, w, z0, z1, mat):
    L = math.hypot(x1 - x0, y1 - y0)
    return cbox(name, (x0 + x1) / 2, (y0 + y1) / 2, L, w, z0, z1, mat, rot=math.atan2(y1 - y0, x1 - x0))


def text(body, x, y, size, mat, font=SEMI, align="CENTER", z=0.0, extrude=0.003, spacing=1.0, rot=0.0, line=1.0):
    if not COMP:
        return None  # the card prints it
    cu = bpy.data.curves.new("Text", "FONT")
    cu.body = body
    cu.font = font
    cu.size = size
    cu.align_x = align
    cu.align_y = "CENTER"
    cu.extrude = extrude
    cu.space_character = spacing
    cu.space_line = line
    ob = bpy.data.objects.new("Text", cu)
    ob.location = (x, y, z + extrude)
    ob.rotation_euler = (0, 0, rot)
    scene.collection.objects.link(ob)
    ob.data.materials.append(mat)
    return ob


def polar(cx, cy, r, deg):
    a = math.radians(deg)
    return cx + r * math.sin(a), cy + r * math.cos(a)


def band(name, cx, cy, r0, r1, a0, a1, z, mat, n=32):
    bm = bmesh.new()
    inner = [bm.verts.new((*polar(cx, cy, r0, a0 + (a1 - a0) * i / n), z)) for i in range(n + 1)]
    outer = [bm.verts.new((*polar(cx, cy, r1, a0 + (a1 - a0) * i / n), z)) for i in range(n + 1)]
    for i in range(n):
        bm.faces.new((inner[i], inner[i + 1], outer[i + 1], outer[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    ob = link(name, bm, mat)
    for f in ob.data.polygons:
        if f.normal.z < 0:
            ob.data.flip_normals()
            break
    return ob


def screw(x, y, z=0.0, r=0.09):
    cyl("Screw", x, y, r, z, z + 0.045, steel, bev=r * 0.4, verts=48)
    cbox("Slot", x, y, r * 1.8, r * 0.26, z + 0.035, z + 0.05, well_mat, rot=random.uniform(0, math.pi))


# ----- the transport plate, trim and control panel ----------------------------------------------
box("Plate", -FACE_W / 2, FACE_W / 2, V(330), FACE_H / 2 + 0.2, -0.3, 0.0, plate_mat, bev=0.02)
trim = box("Trim", -FACE_W / 2, FACE_W / 2, V(337), V(328), -0.3, 0.07, chrome, bev=0.035, seg=4)
box("Panel", -FACE_W / 2, FACE_W / 2, -FACE_H / 2 - 0.2, V(337), -0.3, 0.0, panel_mat, bev=0.02)
for sx, sy in ((-11.25, 5.65), (11.25, 5.65), (-11.25, 0.0), (11.25, 0.0), (-11.25, -5.65), (11.25, -5.65)):
    screw(sx, sy)

# ----- reels: 10.5-inch NAB metal reels ---------------------------------------------------------
REEL_R = 2.76
HUB_R = 1.18                      # the hub the tape winds on (4.5 in of 10.5)
BORE_R = 0.8                      # the NAB centre hole
SPOKE_W, WIN_R0, WIN_R1 = 1.15, 1.36, 2.36


def tape_pack(name):
    """Wound tape seen edge on: smooth oxide, fine winding rings, bands where the tension changed."""
    m = plain(name, (0.075, 0.037, 0.018), rough=0.32)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord")
    fine = nt.nodes.new("ShaderNodeTexWave")
    fine.wave_type = "RINGS"
    fine.rings_direction = "Z"
    fine.inputs["Scale"].default_value = 9.0
    fine.inputs["Distortion"].default_value = 0.6
    fine.inputs["Detail"].default_value = 2.0
    nt.links.new(co.outputs["Object"], fine.inputs["Vector"])
    bump = node(nt, "ShaderNodeBump", Strength=0.08, Distance=0.002)
    nt.links.new(fine.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    bands = nt.nodes.new("ShaderNodeTexWave")
    bands.wave_type = "RINGS"
    bands.rings_direction = "Z"
    bands.inputs["Scale"].default_value = 3.2
    bands.inputs["Distortion"].default_value = 2.0
    bands.inputs["Detail"].default_value = 4.0
    nt.links.new(co.outputs["Object"], bands.inputs["Vector"])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.inputs["A"].default_value = (0.068, 0.034, 0.017, 1)
    mix.inputs["B"].default_value = (0.095, 0.048, 0.024, 1)
    nt.links.new(bands.outputs["Fac"], mix.inputs["Factor"])
    nt.links.new(mix.outputs["Result"], p.inputs["Base Color"])
    rr = node(nt, "ShaderNodeMapRange", **{"To Min": 0.26, "To Max": 0.4})
    nt.links.new(bands.outputs["Fac"], rr.inputs["Value"])
    nt.links.new(rr.outputs["Result"], p.inputs["Roughness"])
    return m


def machined(name):
    """Spun aluminium with the lathe's fine rings in it."""
    m = spun(name, base=(0.80, 0.80, 0.81), rough=0.22)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord")
    w = nt.nodes.new("ShaderNodeTexWave")
    w.wave_type = "RINGS"
    w.rings_direction = "Z"
    w.inputs["Scale"].default_value = 60.0
    w.inputs["Distortion"].default_value = 1.5
    nt.links.new(co.outputs["Object"], w.inputs["Vector"])
    b = node(nt, "ShaderNodeBump", Strength=0.025, Distance=0.001)
    nt.links.new(w.outputs["Fac"], b.inputs["Height"])
    nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    return m


reel_alu = machined("ReelAlu")
pack_mat = tape_pack("TapePack")
anodised = plain("Anodised", (0.03, 0.03, 0.032), metallic=0.8, rough=0.35)


def flange_profile():
    """The flange's mid-surface as (r, dz), dz towards the outside: a hub rib, an outer
    stiffening rib, then the edge rolled back towards the tape."""
    pts = [(BORE_R, 0.0), (0.95, 0.0), (1.02, 0.032), (1.12, 0.032), (1.2, 0.0),
           (2.44, 0.0), (2.5, 0.024), (2.58, 0.024), (2.64, 0.0), (2.69, 0.0)]
    for i in range(1, 11):
        t = math.radians(150 * i / 10)
        pts.append((2.69 + 0.05 * math.sin(t), -0.05 + 0.05 * math.cos(t)))
    return pts


def window_cutter(cx, cy, rot, z0, z1):
    def edge_angle(r):
        return 60 - math.degrees(math.asin(SPOKE_W / (2 * r)))

    a1, a0 = edge_angle(WIN_R1), edge_angle(WIN_R0)
    pts = [polar(cx, cy, WIN_R1, rot - a1 + 2 * a1 * i / 32) for i in range(33)]
    pts += [polar(cx, cy, WIN_R0, rot + a0 - 2 * a0 * i / 16) for i in range(17)]
    bm = bmesh.new()
    lo = [bm.verts.new((x, y, z0)) for x, y in pts]
    hi = [bm.verts.new((x, y, z1)) for x, y in pts]
    bm.faces.new(lo)
    bm.faces.new(hi)
    n = len(pts)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    ob = link("Cutter", bm, well_mat)
    rb = ob.modifiers.new("round", "BEVEL")       # the pressed windows' rounded corners
    rb.width = 0.16
    rb.segments = 8
    rb.limit_method = "ANGLE"
    rb.angle_limit = math.radians(25)
    rb.affect = "EDGES"
    ob.hide_render = True
    ob.hide_viewport = True
    return ob


def flange(cx, cy, z0, outward, rot, name):
    bm = bmesh.new()
    prof = flange_profile()
    verts = [bm.verts.new((cx + r, cy, z0 + outward * dz)) for r, dz in prof]
    edges = [bm.edges.new((verts[i], verts[i + 1])) for i in range(len(verts) - 1)]
    bmesh.ops.spin(bm, geom=verts + edges, cent=(cx, cy, z0), axis=(0, 0, 1),
                   angle=2 * math.pi, steps=256, use_merge=True)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    ob = link(name, bm, reel_alu)
    for poly in ob.data.polygons:
        poly.use_smooth = True
    so = ob.modifiers.new("thick", "SOLIDIFY")
    so.thickness = 0.03
    so.offset = 0.0
    for k in range(3):
        c = window_cutter(cx, cy, rot + 120 * k, z0 - 0.2, z0 + 0.2)
        m = ob.modifiers.new("win", "BOOLEAN")
        m.operation = "DIFFERENCE"
        m.object = c
        m.solver = "EXACT"
    bv = ob.modifiers.new("edges", "BEVEL")
    bv.width = 0.008
    bv.segments = 2
    bv.limit_method = "ANGLE"
    bv.angle_limit = math.radians(40)
    return ob


def reel(cx, cy, pack_r, rot):
    cyl("Turntable", cx, cy, 0.95, 0.0, 0.3, anodised, bev=0.03)
    flange(cx, cy, 0.38, -1, rot, "FlangeBack")
    flange(cx, cy, 1.03, 1, rot, "FlangeFront")
    cyl("Pack", cx, cy, pack_r, 0.415, 0.995, pack_mat, verts=256)
    hub = cyl("Hub", cx, cy, HUB_R, 0.395, 1.015, reel_alu, verts=128)
    bore = cyl("Bore", cx, cy, 0.76, 0.2, 1.3, reel_alu, verts=96)
    bore.hide_render = True
    bore.hide_viewport = True
    m = hub.modifiers.new("bore", "BOOLEAN")
    m.operation = "DIFFERENCE"
    m.object = bore
    m.solver = "EXACT"
    for k in range(3):                       # the NAB keyways
        a = rot + 30 + 120 * k
        kx, ky = polar(cx, cy, 0.8, a)
        kw = cbox("Keyway", kx, ky, 0.16, 0.2, 0.2, 1.3, well_mat, rot=-math.radians(a))
        kw.hide_render = True
        kw.hide_viewport = True
        m = hub.modifiers.new("key", "BOOLEAN")
        m.operation = "DIFFERENCE"
        m.object = kw
        m.solver = "EXACT"
    # The deck's NAB adapter: an anodised body locked by three chrome dogs, a knurled cap.
    cyl("Adapter", cx, cy, 0.74, 0.3, 1.08, anodised, bev=0.02)
    for k in range(3):
        a = rot + 30 + 120 * k
        dx, dy = polar(cx, cy, 0.76, a)
        cbox("Dog", dx, dy, 0.13, 0.2, 0.7, 1.1, chrome, bev=0.02, rot=-math.radians(a))
    bm = bmesh.new()
    n = 72
    ring = [(cx + (0.34 if i % 2 == 0 else 0.325) * math.cos(2 * math.pi * i / n),
             cy + (0.34 if i % 2 == 0 else 0.325) * math.sin(2 * math.pi * i / n)) for i in range(n)]
    lo = [bm.verts.new((x, y, 1.08)) for x, y in ring]
    hi = [bm.verts.new((x, y, 1.26)) for x, y in ring]
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    link("LockKnob", bm, chrome)
    cyl("LockCap", cx, cy, 0.26, 1.26, 1.3, chrome, bev=0.02)


LX, RX, RY = U(190), U(970), V(165)
reel(LX, RY, 2.24, -15 - REEL_ANGLE)  # rot is clockwise; the reels turn anticlockwise
reel(RX, RY, 1.75, 40 - REEL_ANGLE)

# ----- tape path, guides, heads -----------------------------------------------------------------
TAPE_Y = V(244)
TZ0, TZ1 = 0.44, 0.98
GUIDE_R = 0.24
gl, gr = (U(392), TAPE_Y + GUIDE_R), (U(768), TAPE_Y + GUIDE_R)
path = [(LX + 0.2, RY - 2.24), (gl[0], TAPE_Y), (gr[0], TAPE_Y), (RX - 0.2, RY - 1.75)]
for (x0, y0), (x1, y1) in zip(path, path[1:]):
    bar("Tape", x0, y0, x1, y1, 0.014, TZ0, TZ1, tape_mat)
for gx, gy in (gl, gr):
    cyl("Guide", gx, gy, GUIDE_R, 0.0, 1.06, chrome, bev=0.02)
    for z in (TZ0 - 0.06, TZ1):
        cyl("GuideFlange", gx, gy, GUIDE_R + 0.08, z, z + 0.05, chrome, bev=0.015)
    cyl("GuideCap", gx, gy, 0.1, 1.06, 1.1, steel)
cap_x = U(736)
cyl("Capstan", cap_x, TAPE_Y - 0.13, 0.12, 0.0, 1.08, chrome)
cyl("Pinch", cap_x, TAPE_Y + 0.33, 0.32, 0.36, 1.06, rubber, bev=0.04)
cyl("PinchHub", cap_x, TAPE_Y + 0.33, 0.12, 1.06, 1.12, chrome, bev=0.02)

box("HeadBlock", U(450), U(710), V(312), TAPE_Y - 0.2, 0.0, 0.3, block_mat, bev=0.06)
for i, hx in enumerate((U(503), U(580), U(657))):
    cbox("HeadMount", hx, TAPE_Y - 0.55, 1.2, 0.4, 0.3, 0.46, block_mat, bev=0.04)
    head = box("Head", hx - 0.3, hx + 0.3, TAPE_Y - 0.72, TAPE_Y, 0.3, 1.04, mumetal, bev=0.14, seg=6)
    cbox("Gap", hx, TAPE_Y - 0.004, 0.012, 0.012, TZ0, TZ1, well_mat)
    screw(hx - 0.48, TAPE_Y - 0.55, 0.46, 0.055)
    screw(hx + 0.48, TAPE_Y - 0.55, 0.46, 0.055)
for label, hx in (("ERASE", U(503)), ("RECORD", U(580)), ("PLAY", U(657))):
    text(label, hx, V(296), 0.26, ink_cream, z=0.3, spacing=1.1)

# ----- nameplate, counter, lamp ------------------------------------------------------------------
# The nameplate: black anodised, so its printing reads in the spot and in the shadow alike.
nameplate = plain("Nameplate", (0.018, 0.018, 0.02), metallic=0.6, rough=0.32)
box("Nameplate", U(390), U(770), V(112), V(20), 0.0, 0.06, nameplate, bev=0.03)
for sx in (U(402), U(758)):
    screw(sx, V(66), 0.06, 0.06)
text("ORBIT", 0, V(50), 1.05, ink_cream, font=BOLD, spacing=1.35, z=0.06)
text("STUDIO TAPE MACHINE  Â·  Â½-INCH TWO-TRACK", 0, V(84), 0.32, ink_soft, spacing=1.15, z=0.06)
CY = V(140)
box("CounterBezel", -1.2, 1.2, CY - 0.36, CY + 0.36, 0.0, 0.06, chrome, bev=0.03)
box("CounterWell", -1.08, 1.08, CY - 0.26, CY + 0.26, 0.0, 0.07, counter_black)
text("00:03:42", 0, CY, 0.48, digit_mat, font=SEMI, z=0.07, spacing=1.25)
g = box("CounterGlass", -1.1, 1.1, CY - 0.28, CY + 0.28, 0.12, 0.13, glass_mat)
g.visible_shadow = False
text("TAPE\nTIME", 1.58, CY, 0.24, ink_soft, align="LEFT", spacing=1.1, line=0.85)
cyl("LampBezel", -1.68, CY, 0.24, 0.0, 0.08, chrome, bev=0.03)
bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.17, location=(-1.68, CY, 0.12))
jewel = bpy.context.active_object
LIT = COMP or LAYER == "lit"
jewel.data.materials.append(lamp_mat if LIT else plain("LampOff", (0.18, 0.02, 0.01), rough=0.15))
text("NORTHLIGHT  Â·  MODEL OR-2", 0, V(102), 0.25, ink_soft, font=BOLD, spacing=1.3, z=0.06)

# ----- the control panel ------------------------------------------------------------------------
TICK_ANGLES = [-135 + 27 * i for i in range(11)]


def knob(cx, cy, r, turn, label, value, big):
    for a in TICK_ANGLES:
        tx, ty = polar(cx, cy, r + 0.14, a)
        cbox("Tick", tx, ty, 0.03, 0.1 if big else 0.07, 0.0, 0.004, ink_soft, rot=-math.radians(a))
    h = 0.34 if big else 0.26
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
    cap = cyl("Cap", cx, cy, r * 0.72, h, h + 0.12, knob_mat, bev=0.05)
    if not COMP:
        turn = 0.0  # the pointers layer: each pointing up, the card turns it
    px0, py0 = polar(cx, cy, r * 0.12, turn)
    px1, py1 = polar(cx, cy, r * 0.66, turn)
    POINTERS.append(bar("Pointer", px0, py0, px1, py1, 0.05 if big else 0.04, h + 0.1, h + 0.125, pointer_mat))
    return cap


KEY_W, KEY_H = 1.28, 0.6  # every key is 64 x 30 px


def key(cx, cy, label, on):
    box("KeyWell", cx - KEY_W / 2 - 0.06, cx + KEY_W / 2 + 0.06, cy - KEY_H / 2 - 0.06, cy + KEY_H / 2 + 0.06,
        0.0, 0.03, well_mat, bev=0.02)
    lit = on if COMP else LAYER == "lit"
    box("Key", cx - KEY_W / 2, cx + KEY_W / 2, cy - KEY_H / 2, cy + KEY_H / 2, 0.0, 0.24,
        key_lit if lit else key_mat, bev=0.05, seg=4)
    text(label, cx, cy, 0.27, key_ink, z=0.24, extrude=0.002, spacing=1.05)


def caption(name, cx, w, y=-1.28):
    text(name, cx, y, 0.28, ink_cream, font=BOLD, spacing=1.25)
    tw = 0.2 * len(name) + 0.3
    side = (w - tw) / 2 - 0.1
    if side > 0.1 and COMP:  # the card draws the rules beside its captions
        box("Rule", cx - w / 2, cx - w / 2 + side, y - 0.01, y + 0.01, 0.0, 0.004, rule_mat)
        box("Rule", cx + w / 2 - side, cx + w / 2, y - 0.01, y + 0.01, 0.0, 0.004, rule_mat)


def legend(name, value, cx, y):
    text(name, cx, y, 0.3, ink_cream, spacing=1.1)
    text(value, cx, y - 0.32, 0.27, ink_soft, spacing=1.0)


# VU meters
VU_ANGLES = [(-46, "20", 1), (-30, "10", 1), (-21, "7", 1), (-13, "5", 1), (-3, "3", 1), (3, None, 0),
             (9, None, 0), (17, "0", 2), (26, None, 2), (35, None, 2), (45, "+3", 2)]


VU_WIN_W, VU_WIN_H = 3.56, 2.12   # the dial's window (178 x 106 px)
VU_FRAME = 0.16                    # the bezel round it
VU_TOP = 0.40                      # the bezel stands this proud; the dial lies at the panel
VU_BULB = float(os.environ.get("ORBIT_VU_BULB", "1100"))
BEZELS = []


def vu_outline(cx, cy, inset, r=0.1, segs=10):
    """The bezel's outline inset by `inset`, as points (rounded corners, a fixed count)."""
    w, h = VU_WIN_W / 2 + VU_FRAME - inset, VU_WIN_H / 2 + VU_FRAME - inset
    r = max(r - inset, 0.02)
    pts = []
    for qx, qy, a0 in ((w - r, h - r, 0), (-(w - r), h - r, 90), (-(w - r), -(h - r), 180), (w - r, -(h - r), 270)):
        for i in range(segs + 1):
            a = math.radians(a0 + 90 * i / segs)
            pts.append((cx + qx + r * math.cos(a), cy + qy + r * math.sin(a)))
    return pts


def vu(cx, cy, needle, side):
    """A VU meter as the tape echo's is built: a black bakelite bezel chamfered into its
    window, the cream dial sunk in it, the needle's pivot hood standing off the dial's
    foot with two warm bulbs under it (their light pooling up the dial), and glass."""
    profile = [(0.0, 0.0), (0.0, VU_TOP - 0.06), (0.03, VU_TOP), (VU_FRAME - 0.035, VU_TOP),
               (VU_FRAME, VU_TOP - 0.04), (VU_FRAME, 0.02)]
    bm = bmesh.new()
    rings = [[bm.verts.new((x, y, z)) for x, y in vu_outline(cx, cy, d)] for d, z in profile]
    n = len(rings[0])
    for lower, upper in zip(rings, rings[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((lower[i], lower[j], upper[j], upper[i]))
    for i in range(n):  # the underside
        j = (i + 1) % n
        bm.faces.new((rings[-1][i], rings[-1][j], rings[0][j], rings[0][i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bezel = link("VuBezel", bm, vu_bezel)
    bevel(bezel, 0.01, 3)
    smooth_by_angle(bezel)
    BEZELS.append(bezel)
    fz = 0.06
    box("VuDial", cx - VU_WIN_W / 2 - 0.02, cx + VU_WIN_W / 2 + 0.02, cy - VU_WIN_H / 2 - 0.02,
        cy + VU_WIN_H / 2 + 0.02, 0.0, fz, vu_face)
    for sx in (-1.45, 1.45):  # the zero-adjust screws
        screw(cx + sx, cy - 0.78, fz, 0.06)
    px, py = cx, cy - 1.54
    if COMP:  # the card draws the scale and the needle
        band("VuArc", px, py, 1.75, 1.79, -46, 17, fz + 0.002, vu_ink)
        band("VuRed", px, py, 1.74, 1.84, 17, 45, fz + 0.002, vu_red)
        for a, lab, kind in VU_ANGLES:
            r0 = 1.72 if lab else 1.78
            x0, y0 = polar(px, py, r0, a)
            x1, y1 = polar(px, py, 1.92, a)
            bar("VuTick", x0, y0, x1, y1, 0.026, fz, fz + 0.003, vu_red if kind == 2 else vu_ink)
            if lab:
                lx, ly = polar(px, py, 2.06, a)
                text(lab, lx, ly, 0.22, vu_red if kind == 2 else vu_ink, z=fz, extrude=0.001, rot=-math.radians(a))
        text("VU", cx, cy - 0.5, 0.36, vu_ink, font=BOLD, z=fz, extrude=0.001, spacing=1.3)
        text(side, cx - 1.4, cy - 0.5, 0.3, vu_ink, font=BOLD, z=fz, extrude=0.001)
        nx0, ny0 = polar(px, py, 0.7, needle)
        nx1, ny1 = polar(px, py, 2.0, needle)
        bar("Needle", nx0, ny0, nx1, ny1, 0.024, fz + 0.03, fz + 0.04, vu_ink)
    # The hood: a trapezoid shelf off the dial's foot, its back running in under the bezel.
    bm = bmesh.new()
    back, front = cy - VU_WIN_H / 2 - 0.12, cy - VU_WIN_H / 2 + 0.45
    lo = fz + 0.13
    corners = [(cx - 0.85, back, lo), (cx + 0.85, back, lo), (cx + 0.55, front, lo), (cx - 0.55, front, lo),
               (cx - 0.85, back, fz + 0.31), (cx + 0.85, back, fz + 0.31), (cx + 0.55, front, fz + 0.23),
               (cx - 0.55, front, fz + 0.23)]
    v = [bm.verts.new(c) for c in corners]
    for face in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
        bm.faces.new([v[i] for i in face])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    hood = link("VuHood", bm, vu_bezel)
    bevel(hood, 0.025, 4)
    smooth_by_angle(hood)
    SHROUDS.append(hood)
    # The bulbs under it, lighting the dial from its foot.
    for bx in (-0.6, 0.6):
        ld = bpy.data.lights.new("VuBulb", "POINT")
        ld.energy = VU_BULB
        ld.shadow_soft_size = 0.12
        ld.color = (1.0, 0.56, 0.22)
        bo = bpy.data.objects.new("VuBulb", ld)
        bo.location = (cx + bx, cy - VU_WIN_H / 2 + 0.1, fz + 0.11)
        scene.collection.objects.link(bo)
    pts = vu_outline(cx, cy, VU_FRAME - 0.02)
    bm = bmesh.new()
    lo_ring = [bm.verts.new((x, y, VU_TOP - 0.05)) for x, y in pts]
    hi_ring = [bm.verts.new((x, y, VU_TOP - 0.045)) for x, y in pts]
    bm.faces.new(list(reversed(lo_ring)))
    bm.faces.new(hi_ring)
    for i in range(len(pts)):
        j = (i + 1) % len(pts)
        bm.faces.new((lo_ring[i], lo_ring[j], hi_ring[j], hi_ring[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    g = link("VuGlass", bm, glass_mat)
    g.visible_shadow = False
    GLASSES.append(g)


MY = V(414)
vu(U(127), MY, 6, "L")
vu(U(333), MY, 1, "R")
# The layout below is mirrored in src/ui/strip/OrbitFace.h: move a part in both.
KY = V(500)
text("METER", U(110), KY, 0.28, ink_soft, spacing=1.1)
key(U(172), KY, "INPUT", False)
key(U(244), KY, "TAPE", True)
key(U(316), KY, "OUTPUT", False)
text("0 VU = +6 dB OVER 185 nWb/m", U(230), V(536), 0.24, ink_soft, spacing=1.08)

# RECORD
RX_ = U(515)
caption("RECORD", RX_, 2.6)
knob(RX_, -2.3, 0.56, 38, "INPUT", "+4.5 dB", True)
legend("INPUT", "+4.5 dB", RX_, -3.3)
knob(RX_, -4.4, 0.38, 0, "BIAS", "CAL", False)
legend("BIAS", "CAL", RX_, -5.06)

# MACHINE
MX = U(736)
caption("MACHINE", MX, 5.36)
rows = [("SPEED", [("7Â½", False), ("15", True), ("30", False)]),
        ("EQ", [("NAB", True), ("CCIR", False)]),
        ("LEVEL", [("+3", False), ("+6", True), ("+9", False)]),
        ("TAPE", [("CLASSIC", True), ("HOT", False), ("MODERN", False)])]
for (name, keys), ry in zip(rows, (-2.04, -2.88, -3.72, -4.56)):
    text(name, U(650), ry, 0.28, ink_soft, align="RIGHT", spacing=1.1)
    for (label, on), kx in zip(keys, (694, 766, 838)):
        key(U(kx), ry, label, on)

# WEAR
WX = U(934)
caption("WEAR", WX, 1.68)
knob(WX, -2.2, 0.38, -80, "HISS", "20%", False)
legend("HISS", "20%", WX, -2.86)
knob(WX, -3.95, 0.38, -100, "FLUTTER", "0.05%", False)
legend("FLUTTER", "0.05%", WX, -4.61)

# REPRO
OX = U(1063)
caption("REPRO", OX, 2.6)
knob(OX, -2.3, 0.56, -38, "OUTPUT", "-4.5 dB", True)
legend("OUTPUT", "âˆ’4.5 dB", OX, -3.3)
knob(U(1027), -4.4, 0.38, 135, "MIX", "100%", False)
legend("MIX", "100%", U(1027), -5.06)
key(U(1095), -4.4, "LINK", True)
text("OUT\nFOLLOWS IN", U(1095), -5.12, 0.2, ink_soft, spacing=1.05, line=0.9)

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

# ----- light: a key from the top of the unit, the room in reflections ---------------------------
sun_data = bpy.data.lights.new("Key", "SUN")
sun_data.energy = 2.6
sun_data.angle = math.radians(24)
sun = bpy.data.objects.new("Key", sun_data)
sun.rotation_mode = "QUATERNION"
sun.rotation_quaternion = (Vector((0, 0, 0.3)) - Vector((-1.2, 3.4, 4.0))).to_track_quat("-Z", "Y")
scene.collection.objects.link(sun)

image = bpy.data.images.load(ROOM)
world = bpy.data.worlds.new("World")
world.use_nodes = True
wn = world.node_tree
wc = wn.nodes.new("ShaderNodeTexCoord")
wm = wn.nodes.new("ShaderNodeMapping")
wm.inputs["Rotation"].default_value = (0.0, 0.0, math.radians(float(os.environ.get("ORBIT_ROOM_ROT", "200"))))
env = wn.nodes.new("ShaderNodeTexEnvironment")
env.image = image
wn.links.new(wc.outputs["Generated"], wm.inputs["Vector"])
wn.links.new(wm.outputs["Vector"], env.inputs["Vector"])
hue = wn.nodes.new("ShaderNodeHueSaturation")
hue.inputs["Saturation"].default_value = 0.5
wn.links.new(env.outputs["Color"], hue.inputs["Color"])
bg = wn.nodes["Background"]
bg.inputs["Strength"].default_value = 0.8
wn.links.new(hue.outputs["Color"], bg.inputs["Color"])
dim = wn.nodes.new("ShaderNodeBackground")
dim.inputs["Strength"].default_value = 0.18
wn.links.new(hue.outputs["Color"], dim.inputs["Color"])
lp = wn.nodes.new("ShaderNodeLightPath")
mixw = wn.nodes.new("ShaderNodeMixShader")
wn.links.new(lp.outputs["Is Glossy Ray"], mixw.inputs["Fac"])
wn.links.new(dim.outputs["Background"], mixw.inputs[1])
wn.links.new(bg.outputs["Background"], mixw.inputs[2])
wn.links.new(mixw.outputs["Shader"], wn.nodes["World Output"].inputs["Surface"])
scene.world = world

# ----- mood: a dim mastering room, a warm spot pooled on the deck, a cool rim from the side -------
if os.environ.get("ORBIT_MOOD"):
    sun_data.energy = 0.08
    bg.inputs["Strength"].default_value = 0.1
    dim.inputs["Strength"].default_value = 0.015

    def aim(ob, at):
        ob.rotation_mode = "QUATERNION"
        ob.rotation_quaternion = (Vector(at) - ob.location).to_track_quat("-Z", "Y")

    spot = bpy.data.lights.new("Spot", "SPOT")
    spot.energy = float(os.environ.get("ORBIT_SPOT", "14000"))
    spot.color = (1.0, 0.78, 0.55)
    spot.spot_size = math.radians(38)
    spot.spot_blend = 0.95
    spot.shadow_soft_size = 1.2
    so = bpy.data.objects.new("Spot", spot)
    so.location = (-7.0, 10.0, 15.0)
    aim(so, (-4.6, 2.9, 0.0))
    scene.collection.objects.link(so)

    rim = bpy.data.lights.new("Rim", "AREA")
    rim.energy = float(os.environ.get("ORBIT_RIM", "2800"))
    rim.color = (0.42, 0.58, 1.0)
    rim.size = 5.0
    ro = bpy.data.objects.new("Rim", rim)
    ro.location = (15.5, -4.5, 2.2)
    aim(ro, (7.5, -3.2, 0.0))
    scene.collection.objects.link(ro)

    fill = bpy.data.lights.new("Fill", "AREA")
    fill.energy = float(os.environ.get("ORBIT_FILL", "1400"))
    fill.color = (1.0, 0.7, 0.42)
    fill.size = 8.0
    fo = bpy.data.objects.new("Fill", fill)
    fo.location = (-6.0, -14.0, 5.0)
    aim(fo, (-2.0, -3.5, 0.0))
    scene.collection.objects.link(fo)
    ink_soft.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.55, 0.52, 0.45, 1)
    ink_cream.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.85, 0.8, 0.68, 1)

    def glow(mat, strength):
        mat.node_tree.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = strength

    kp = key_lit.node_tree.nodes["Principled BSDF"]
    kp.inputs["Base Color"].default_value = (0.5, 0.2, 0.03, 1)
    kp.inputs["Emission Color"].default_value = (1.0, 0.38, 0.05, 1)
    glow(key_lit, 1.6)
    glow(digit_mat, 2.6)
    glow(lamp_mat, 4.5)

# ----- render ------------------------------------------------------------------------------------
scene.render.engine = "CYCLES"
scene.cycles.samples = SAMPLES
scene.cycles.use_denoising = True
scene.cycles.transparent_max_bounces = 16
scene.cycles.device = "CPU"
if os.environ.get("ORBIT_GPU"):
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
    scene.render.threads = int(os.environ.get("ORBIT_THREADS", "8"))
scene.render.resolution_x = round(1160 * SCALE)
scene.render.resolution_y = round(600 * SCALE)
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGB"

# ----- the layers ----------------------------------------------------------------------------------
if not COMP and LAYER in ("deck", "lit"):
    for ob in POINTERS + GLASSES:
        ob.hide_render = True            # each its own layer
elif LAYER in ("pointers", "hood", "glass"):
    keep = {"pointers": POINTERS, "hood": SHROUDS, "glass": GLASSES}[LAYER]
    for ob in scene.objects:
        if ob.type in {"MESH", "CURVE", "FONT"} and ob not in keep:
            ob.visible_camera = False    # still there in the light, the shadows and the reflections
            if LAYER == "glass":
                ob.visible_transmission = False
    if LAYER == "hood":
        for ob in BEZELS:              # what the bezel covers stays covered
            ob.visible_camera = True
            ob.is_holdout = True
    scene.render.film_transparent = True
    scene.cycles.film_transparent_glass = LAYER == "glass"
    scene.render.image_settings.color_mode = "RGBA"
if os.environ.get("ORBIT_MOOD"):
    scene.view_settings.view_transform = "AgX"
    try:
        scene.view_settings.look = "AgX - Punchy"
    except TypeError:
        pass
else:
    scene.view_settings.view_transform = "Standard"
if os.environ.get("ORBIT_BORDER"):
    x0, y0, x1, y1 = (float(v) for v in os.environ["ORBIT_BORDER"].split(","))
    scene.render.use_border = True
    scene.render.use_crop_to_border = True
    scene.render.border_min_x, scene.render.border_min_y = x0, y0
    scene.render.border_max_x, scene.render.border_max_y = x1, y1
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("rendered", OUT)
