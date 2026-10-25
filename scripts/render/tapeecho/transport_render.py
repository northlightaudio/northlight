"""Blender (4.2) script: the tape transport seen through the window, under a smoked
perspex lid. Seen from the display bezel's own camera (20 units above the window's
centre), so the two line up.

Left to right: a flanged guide, the erase head, the record head and four playback
heads on their brackets, the capstan and its rubber pinch roller, then the chamber
where the loop's slack lies folded; the tape comes back along the rear to a second
guide. The tape is a ribbon on edge, as it is: from above, mostly its edge shows.

  transport  everything under the lid, the lid's smoke tinting the light in and out.
  perspex    the lid's reflections and fine scratches alone, against black (turned
             into colour + alpha afterwards, as the meter's glass is).

The window's opening on the page is 624 x 194 px at 50 px per unit: 12.48 x 3.88
units. The image is that opening at the panel's plane, 150 px per unit: 1872 x 582.

Run: blender --background --factory-startup --python transport_render.py -- <transport|perspex> <out.png>
"""
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
LAYER, OUT = argv[0], argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from render_config import hdri_path  # noqa: E402  (scripts/render: NORTHLIGHT_HDRI or render.local.json)
ROOM = hdri_path()  # Poly Haven, CC0
PX, DIST = 150, 20.0
WIN_W, WIN_H = 624 / 50, 194 / 50
DECK_Z = -1.0                        # the deck plate, this far under the panel
SCALE = float(os.environ.get("TR_SCALE", "1.0"))
NEON_W = float(os.environ.get("TR_NEON", "2.5"))         # the glow it throws
NEON_GAS = float(os.environ.get("TR_GAS", "1.15"))       # the gas, seen directly

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
random.seed(7)

# The meter's helpers (node, plain, bevel, link, smooth_by_angle, aluminium...), run here.
os.environ["BEZEL_FINISH"] = "alu"
src = open(os.path.join(HERE, "vu_render.py"), encoding="utf-8").read()
exec(src[src.index("# ----- geometry helpers"):src.index("liner_mat = plain(")])
alu = bezel_mat


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


def cyl(name, x, y, r, z0, z1, mat, bev=0.0, verts=96):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=z1 - z0, location=(x, y, (z0 + z1) / 2))
    ob = bpy.context.active_object
    ob.name = name
    ob.data.materials.append(mat)
    if bev:
        bevel(ob, bev, 3)
    smooth_by_angle(ob)
    return ob


# ----- materials ---------------------------------------------------------------------------------
def painted(name, base, rough=0.5, grain=0.12):
    m = plain(name, base, rough=rough)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    n = node(nt, "ShaderNodeTexNoise", Scale=60.0, Detail=10.0, Roughness=0.7)
    b = node(nt, "ShaderNodeBump", Strength=grain, Distance=0.004)
    nt.links.new(n.outputs["Fac"], b.inputs["Height"])
    nt.links.new(b.outputs["Normal"], p.inputs["Normal"])
    return m


deck = painted("Deck", (0.07, 0.07, 0.075), rough=0.5, grain=0.25)     # black crinkle-ish paint
cavity = plain("Cavity", (0.02, 0.02, 0.02), rough=0.7)
chrome = plain("Chrome", (0.80, 0.79, 0.76), metallic=1.0, rough=0.18)
steel = plain("Steel", (0.62, 0.61, 0.59), metallic=1.0, rough=0.28)
headcan = plain("HeadCan", (0.70, 0.69, 0.66), metallic=1.0, rough=0.32)
blackplastic = plain("BlackPlastic", (0.015, 0.015, 0.015), rough=0.35)
rubber = plain("Rubber", (0.012, 0.012, 0.012), rough=0.75)
felt = painted("Felt", (0.05, 0.05, 0.05), rough=0.95, grain=0.5)
bracket = plain("Bracket", (0.55, 0.53, 0.5), metallic=1.0, rough=0.45)
tape_mat = plain("Tape", (0.11, 0.055, 0.025), rough=0.45)               # brown oxide
nickel = plain("Nickel", (0.70, 0.68, 0.64), metallic=1.0, rough=0.38)
slot_mat = plain("Slot", (0.02, 0.02, 0.02), rough=0.8)


def screw(x, y, z, r=0.05, turn=None):
    turn = random.uniform(0, 180) if turn is None else turn
    head = cyl("Screw", x, y, r, z, z + 0.03, nickel, bev=r * 0.4, verts=48)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(x, y, z + 0.032))
    s = bpy.context.active_object
    s.scale = (r * 1.8, r * 0.28, 0.02)
    s.rotation_euler = (0, 0, math.radians(turn))
    s.data.materials.append(slot_mat)
    return [head, s]


# ----- the panel and the well under the window ---------------------------------------------------
def rect(cx, cy, w, h, z):
    return [(cx - w / 2, cy - h / 2, z), (cx + w / 2, cy - h / 2, z), (cx + w / 2, cy + h / 2, z), (cx - w / 2, cy + h / 2, z)]


bm = bmesh.new()
outer = [bm.verts.new(p) for p in rect(0, 0, WIN_W + 6, WIN_H + 6, 0)]
hole = [bm.verts.new(p) for p in rect(0, 0, WIN_W, WIN_H, 0)]
floor_ring = [bm.verts.new(p) for p in rect(0, 0, WIN_W, WIN_H, DECK_Z)]
for a, b in ((outer, hole), (hole, floor_ring)):
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new((a[i], a[j], b[j], b[i]))
bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
panel = link("Panel", bm, cavity)

box("Deck", -WIN_W / 2 - 1, WIN_W / 2 + 1, -WIN_H / 2 - 1, WIN_H / 2 + 1, DECK_Z - 0.06, DECK_Z, deck)
for sx, sy in ((-5.0, 1.55), (1.9, 1.55), (-2.9, -1.55), (2.3, -1.55)):
    screw(sx, sy, DECK_Z, 0.06)

# ----- the tape path -----------------------------------------------------------------------------
FRONT_Y, BACK_Y = -0.35, 1.1
TAPE_Z0, TAPE_H = -0.86, 0.32
POST_R = 0.14
LX = -5.95                                   # the tape's left turn
CAP_X = 2.65
CH_X0, CH_X1, CH_Y0, CH_Y1 = 3.45, 6.05, -1.62, 1.62


def arc(cx, cy, r, a0, a1, n=24):
    return [(cx + r * math.cos(math.radians(a0 + (a1 - a0) * i / n)), cy + r * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


path = []
# Out of the chamber at its rear, left along the back, round the upper guide, down, round the lower guide.
path += [(CH_X0 + 0.25, BACK_Y), (CH_X0 - 0.1, BACK_Y)]
path += [(x, BACK_Y) for x in (2.0, 0.0, -2.0, -4.0, LX + POST_R)]
path += arc(LX + POST_R, BACK_Y - POST_R, POST_R, 90, 180)
path += arc(LX + POST_R, FRONT_Y + POST_R, POST_R, 180, 270)
# Along the front, past the heads, through the capstan, into the chamber.
path += [(x, FRONT_Y) for x in (-4.0, -2.0, 0.0, CAP_X, CH_X0 - 0.1, CH_X0 + 0.25)]
# The slack: loose folds, as tape piles up in a chamber, rightwards through the
# lower half and back through the upper.
def catmull(ctrl, per=14):
    pts = []
    c = [ctrl[0]] + ctrl + [ctrl[-1]]
    for i in range(1, len(c) - 2):
        p0, p1, p2, p3 = c[i - 1], c[i], c[i + 1], c[i + 2]
        for k in range(per):
            t = k / per
            t2, t3 = t * t, t * t * t
            pts.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3) for j in (0, 1)))
    pts.append(ctrl[-1])
    return pts


def meander(x_from, x_to, y_lo, y_hi, folds_n):
    ctrl = []
    up = True
    for k in range(folds_n):
        f = k / (folds_n - 1)
        x = x_from + (x_to - x_from) * f + random.uniform(-0.12, 0.12)
        span = random.uniform(0.55, 1.0)
        mid = (y_lo + y_hi) / 2 + random.uniform(-0.15, 0.15)
        half = (y_hi - y_lo) / 2 * span
        y = mid + half if up else mid - half
        ctrl.append((x + random.uniform(-0.05, 0.05), y))
        ctrl.append((x + random.uniform(0.02, 0.14) * (1 if x_to > x_from else -1), y + random.uniform(-0.08, 0.08)))
        up = not up
    return ctrl


folds = catmull([(CH_X0 + 0.3, FRONT_Y)] + meander(CH_X0 + 0.4, CH_X1 - 0.25, CH_Y0 + 0.15, 0.15, 22)
                + meander(CH_X1 - 0.25, CH_X0 + 0.4, -0.2, CH_Y1 - 0.15, 20) + [(CH_X0 + 0.3, BACK_Y)])
path += folds
path += [(CH_X0 + 0.25, BACK_Y)]

curve = bpy.data.curves.new("TapePath", "CURVE")
curve.dimensions = "2D"
curve.extrude = TAPE_H / 2
curve.fill_mode = "BOTH"
spline = curve.splines.new("POLY")
spline.points.add(len(path) - 1)
for p, (x, y) in zip(spline.points, path):
    p.co = (x, y, 0, 1)
tape = bpy.data.objects.new("Tape", curve)
tape.location = (0, 0, TAPE_Z0 + TAPE_H / 2)
scene.collection.objects.link(tape)
tape.data.materials.append(tape_mat)
sol = tape.modifiers.new("thick", "SOLIDIFY")
sol.thickness = 0.03                          # drawn a little thick, so it reads at this size
sol.offset = 0

# ----- guides, heads, capstan, pinch roller ------------------------------------------------------
for gx, gy in ((LX + POST_R, BACK_Y - POST_R), (LX + POST_R, FRONT_Y + POST_R)):
    cyl("Post", gx, gy, 0.09, DECK_Z, TAPE_Z0 + TAPE_H + 0.03, chrome)
    cyl("Flange", gx, gy, 0.2, TAPE_Z0 + TAPE_H + 0.03, TAPE_Z0 + TAPE_H + 0.06, chrome, bev=0.01)
    cyl("FlangeLow", gx, gy, 0.2, TAPE_Z0 - 0.04, TAPE_Z0 - 0.01, chrome, bev=0.01)


def head(x, w=0.44, cap=None):
    """A tape head on its bracket: the can's rounded face touching the tape from the front."""
    y_face = FRONT_Y - 0.03
    box("Bracket", x - 0.36, x + 0.36, y_face - 0.72, y_face - 0.12, DECK_Z, DECK_Z + 0.06, bracket, bev=0.01)
    screw(x - 0.26, y_face - 0.6, DECK_Z + 0.06, 0.045)
    screw(x + 0.26, y_face - 0.6, DECK_Z + 0.06, 0.045)
    z0, z1 = DECK_Z + 0.06, TAPE_Z0 + TAPE_H + 0.05
    box("Can", x - w / 2, x + w / 2, y_face - 0.5, y_face - w / 2, z0, z1, headcan, bev=0.03)
    cyl("Face", x, y_face - w / 2, w / 2, z0, z1, headcan, bev=0.02)
    if cap:
        box("Cap", x - w / 2 + 0.04, x + w / 2 - 0.04, y_face - 0.46, y_face - 0.12, z1, z1 + 0.04, cap, bev=0.015)


HEAD_X = {"E": -5.0, "R": -3.85, "1": -2.4, "2": -1.2, "3": 0.0, "4": 1.2}

# Neon indicators (NE-2 style) on the record and playback heads' brackets: a clear glass
# capsule lying along the bracket, two electrodes in it, the gas glowing round them when lit.
neon_glass = bpy.data.materials.new("NeonGlass")
neon_glass.use_nodes = True
ngp = neon_glass.node_tree.nodes["Principled BSDF"]
ngp.inputs["Transmission Weight"].default_value = 1.0
ngp.inputs["Roughness"].default_value = 0.05
ngp.inputs["Base Color"].default_value = (0.95, 0.9, 0.85, 1)
electrode = plain("Electrode", (0.5, 0.48, 0.45), metallic=1.0, rough=0.4)
neon_gas = {}
NEON_Y = FRONT_Y - 0.03 - 0.66
NEON_Z = DECK_Z + 0.06 + 0.075


def neon(key, x):
    bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=0.07, depth=0.24, location=(x, NEON_Y, NEON_Z))
    cap = bpy.context.active_object
    cap.rotation_euler = (0, math.radians(90), 0)
    cap.data.materials.append(neon_glass)
    cap.visible_shadow = False           # its glass lets the glow out
    bpy.ops.object.shade_smooth()
    for ex in (-0.12, 0.12):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=0.07, location=(x + ex, NEON_Y, NEON_Z))
        e = bpy.context.active_object
        e.data.materials.append(neon_glass)
        e.visible_shadow = False
        bpy.ops.object.shade_smooth()
    for ey in (-0.022, 0.022):
        bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.009, depth=0.17, location=(x, NEON_Y + ey, NEON_Z))
        w = bpy.context.active_object
        w.rotation_euler = (0, math.radians(90), 0)
        w.data.materials.append(electrode)
    # the glow: a soft sheath of lit gas round the electrodes
    gm = bpy.data.materials.new("Neon" + key)
    gm.use_nodes = True
    gnt = gm.node_tree
    gnt.nodes.remove(gnt.nodes["Principled BSDF"])
    em = gnt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (1.0, 0.3, 0.05, 1)      # neon's orange
    em.inputs["Strength"].default_value = NEON_GAS     # seen directly: kept just under clipping
    gnt.links.new(em.outputs["Emission"], gnt.nodes["Material Output"].inputs["Surface"])
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=0.042, depth=0.19, location=(x, NEON_Y, NEON_Z))
    g = bpy.context.active_object
    g.rotation_euler = (0, math.radians(90), 0)
    g.data.materials.append(gm)
    g.visible_shadow = False
    bpy.ops.object.shade_smooth()
    # the light it throws on its bracket, head and the deck
    ld = bpy.data.lights.new("NeonLight" + key, "POINT")
    ld.energy = NEON_W
    ld.shadow_soft_size = 0.06
    ld.color = (1.0, 0.3, 0.05)
    lo = bpy.data.objects.new("NeonLight" + key, ld)
    lo.location = (x, NEON_Y, NEON_Z)
    lo.visible_camera = False            # only its light shows, not the light itself
    lo.visible_glossy = False
    lo.visible_transmission = False     # nor through the glass
    scene.collection.objects.link(lo)
    neon_gas[key] = (g, lo)

head(HEAD_X["E"], w=0.5)
head(HEAD_X["R"])
for k in "1234":
    head(HEAD_X[k])
for k in "R1234":
    neon(k, HEAD_X[k])

# The capstan inside the loop, the pinch roller outside it.
cyl("CapstanBoss", CAP_X, FRONT_Y + 0.08, 0.16, DECK_Z, DECK_Z + 0.08, steel, bev=0.01)
cyl("Capstan", CAP_X, FRONT_Y + 0.08, 0.065, DECK_Z, TAPE_Z0 + TAPE_H + 0.12, steel)
py = FRONT_Y - 0.3
box("PinchArm", CAP_X - 0.1, CAP_X + 0.95, py - 0.12, py + 0.12, DECK_Z + 0.02, DECK_Z + 0.08, bracket, bev=0.02)
cyl("PinchRoller", CAP_X, py, 0.28, TAPE_Z0 - 0.04, TAPE_Z0 + TAPE_H + 0.04, rubber, bev=0.02)
HUB_TOP = TAPE_Z0 + TAPE_H + 0.08
# The round parts stay in the transport (their highlights must not turn); only the
# screw's slot and the drillings turn, in the hubs layer.
cyl("PinchHub", CAP_X, py, 0.1, TAPE_Z0 + TAPE_H + 0.04, HUB_TOP, chrome, bev=0.01)
turning = [screw(CAP_X, py, HUB_TOP, 0.035, turn=20)[1]]
for k in range(3):                       # three drillings, so the turning shows
    a = math.radians(90 + 120 * k)
    turning.append(cyl("HubHole", CAP_X + 0.065 * math.cos(a), py + 0.065 * math.sin(a), 0.014, HUB_TOP - 0.005, HUB_TOP + 0.002, slot_mat, verts=24))
cyl("ArmPivot", CAP_X + 0.85, py, 0.08, DECK_Z, DECK_Z + 0.14, steel, bev=0.01)

# A tension arm on the rear run: a felt-faced roller pressing the tape out, its coil spring.
ARM_PIV = (-1.6, 0.25)
ROLL = (-0.7, BACK_Y - 0.17)
cyl("ArmPivot2", ARM_PIV[0], ARM_PIV[1], 0.09, DECK_Z, DECK_Z + 0.16, steel, bev=0.01)
ang = math.atan2(ROLL[1] - ARM_PIV[1], ROLL[0] - ARM_PIV[0])
length = math.hypot(ROLL[0] - ARM_PIV[0], ROLL[1] - ARM_PIV[1])
arm = box("TensionArm", -0.05, length + 0.05, -0.07, 0.07, DECK_Z + 0.08, DECK_Z + 0.13, bracket, bev=0.02)
arm.location = (ARM_PIV[0], ARM_PIV[1], 0)
arm.rotation_euler = (0, 0, ang)
cyl("TensionRoller", ROLL[0], ROLL[1], 0.15, TAPE_Z0 - 0.02, TAPE_Z0 + TAPE_H + 0.02, felt)
CAP_TOP = TAPE_Z0 + TAPE_H + 0.05
cyl("TensionCap", ROLL[0], ROLL[1], 0.17, TAPE_Z0 + TAPE_H + 0.02, CAP_TOP, chrome, bev=0.01)
turning.append(screw(ROLL[0], ROLL[1], CAP_TOP, 0.035, turn=70)[1])
for k in range(2):
    a = math.radians(30 + 180 * k)
    turning.append(cyl("CapHole", ROLL[0] + 0.11 * math.cos(a), ROLL[1] + 0.11 * math.sin(a), 0.016, CAP_TOP - 0.005, CAP_TOP + 0.002, slot_mat, verts=24))
# Its spring, from the arm's middle to a post on the deck.
sp_a = (ARM_PIV[0] + math.cos(ang) * length * 0.45, ARM_PIV[1] + math.sin(ang) * length * 0.45)
sp_b = (-2.9, 0.15)
cyl("SpringPost", sp_b[0], sp_b[1], 0.05, DECK_Z, DECK_Z + 0.2, steel)
coil = []
turns, n_c = 18, 360
for i in range(n_c + 1):
    t = i / n_c
    cx = sp_a[0] + (sp_b[0] - sp_a[0]) * t
    cy = sp_a[1] + (sp_b[1] - sp_a[1]) * t
    th = 2 * math.pi * turns * t
    # the coil's axis runs along the spring; its loops stand across it
    nx, ny = -(sp_b[1] - sp_a[1]), (sp_b[0] - sp_a[0])
    nl = math.hypot(nx, ny)
    coil.append((cx + 0.05 * math.cos(th) * nx / nl, cy + 0.05 * math.cos(th) * ny / nl, DECK_Z + 0.14 + 0.05 * math.sin(th)))
sc_curve = bpy.data.curves.new("Spring", "CURVE")
sc_curve.dimensions = "3D"
sc_curve.bevel_depth = 0.008
spl = sc_curve.splines.new("POLY")
spl.points.add(len(coil) - 1)
for p, c in zip(spl.points, coil):
    p.co = (*c, 1)
spring = bpy.data.objects.new("Spring", sc_curve)
scene.collection.objects.link(spring)
spring.data.materials.append(steel)

# ----- the chamber: low black walls, felt floor, gaps where the tape goes in and out --------------
box("ChamberFloor", CH_X0, CH_X1, CH_Y0, CH_Y1, DECK_Z, DECK_Z + 0.02, felt)
WALL_T, WALL_Z1 = 0.06, TAPE_Z0 + TAPE_H + 0.08
box("WallR", CH_X1 - WALL_T, CH_X1, CH_Y0, CH_Y1, DECK_Z, WALL_Z1, blackplastic, bev=0.01)
box("WallT", CH_X0, CH_X1, CH_Y1 - WALL_T, CH_Y1, DECK_Z, WALL_Z1, blackplastic, bev=0.01)
box("WallB", CH_X0, CH_X1, CH_Y0, CH_Y0 + WALL_T, DECK_Z, WALL_Z1, blackplastic, bev=0.01)
box("WallL1", CH_X0, CH_X0 + WALL_T, CH_Y0, FRONT_Y - 0.12, DECK_Z, WALL_Z1, blackplastic, bev=0.01)
box("WallL2", CH_X0, CH_X0 + WALL_T, FRONT_Y + 0.12, BACK_Y - 0.12, DECK_Z, WALL_Z1, blackplastic, bev=0.01)
box("WallL3", CH_X0, CH_X0 + WALL_T, BACK_Y + 0.12, CH_Y1, DECK_Z, WALL_Z1, blackplastic, bev=0.01)

# The chamber's cover: a brushed aluminium plate on four screws, screen-printed like a
# service panel of the period (Barlow Condensed, the panel's own face).
FONTS = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "assets", "fonts")  # the repository's
font_bold = bpy.data.fonts.load(os.path.join(FONTS, "BarlowCondensed-Bold.ttf"))
font_semi = bpy.data.fonts.load(os.path.join(FONTS, "BarlowCondensed-SemiBold.ttf"))
ink = plain("Ink", (0.025, 0.024, 0.022), rough=0.6)
red_ink = plain("RedInk", (0.42, 0.045, 0.03), rough=0.6)
PL_X0, PL_X1, PL_Y0, PL_Y1 = CH_X0 - 0.03, CH_X1 + 0.03, CH_Y0 - 0.04, CH_Y1 + 0.04
PL_Z = WALL_Z1 + 0.035
box("Cover", PL_X0, PL_X1, PL_Y0, PL_Y1, WALL_Z1, PL_Z, alu, bev=0.02)
for sx in (PL_X0 + 0.17, PL_X1 - 0.17):
    for sy in (PL_Y0 + 0.17, PL_Y1 - 0.17):
        screw(sx, sy, PL_Z, 0.05)
PCX = (PL_X0 + PL_X1) / 2


def printed(text, y, size, font=None, mat=None, spacing=1.0):
    cu = bpy.data.curves.new("Print", "FONT")
    cu.body = text
    cu.font = font or font_semi
    cu.size = size
    cu.align_x = "CENTER"
    cu.align_y = "CENTER"
    cu.space_character = spacing
    cu.extrude = 0.0015
    ob = bpy.data.objects.new("Print", cu)
    ob.location = (PCX, y, PL_Z + 0.0015)
    scene.collection.objects.link(ob)
    ob.data.materials.append(mat or ink)
    return ob


def rule(y, w=1.9, t=0.014):
    box("Rule", PCX - w / 2, PCX + w / 2, y - t / 2, y + t / 2, PL_Z, PL_Z + 0.002, ink)


printed("TE-4", 0.86, 0.66, font_bold, spacing=1.04)
printed("TAPE  ECHO", 0.42, 0.19, spacing=1.35)
rule(0.24)
printed("TAPE LOOP CARTRIDGE", 0.03, 0.18, spacing=1.12)
printed("1/4 INCH  ·  ENDLESS LOOP", -0.22, 0.13, spacing=1.15)
rule(-0.42)
printed("REPLACE TAPE EVERY", -0.64, 0.13, spacing=1.15)
printed("500 HOURS OF USE", -0.83, 0.13, spacing=1.15)
printed("DO NOT OPEN WHILE RUNNING", -1.2, 0.12, mat=red_ink, spacing=1.12)

# Warm lamps along the front of the well, out of sight from above.
LAMP_W = float(os.environ.get("TR_LAMP", "90"))
for lx in (-5.4, -3.6, -1.8, 0.0, 1.8, 3.6, 5.4):   # a row of them, for an even wash
    ld = bpy.data.lights.new("Lamp", "SPOT")
    ld.spot_size = math.radians(150)
    ld.spot_blend = 1.0
    ld.energy = LAMP_W * (0.3 if lx > 3 else 1.0)   # the cover sits close to the last two
    ld.shadow_soft_size = 0.12
    ld.color = (1.0, 0.58, 0.24)
    lo = bpy.data.objects.new("Lamp", ld)
    lo.location = (lx, -WIN_H / 2 + 0.14, -0.17)
    lo.rotation_mode = "QUATERNION"
    lo.rotation_quaternion = (Vector((lx, 1.0, DECK_Z)) - lo.location).to_track_quat("-Z", "Y")   # down and back across the deck
    scene.collection.objects.link(lo)
# ...under a slim black valance along the window's front edge, which hides them.
box("Valance", -WIN_W / 2, WIN_W / 2, -WIN_H / 2, -WIN_H / 2 + 0.3, -0.09, -0.05, blackplastic, bev=0.01)
box("ValanceLip", -WIN_W / 2, WIN_W / 2, -WIN_H / 2 + 0.27, -WIN_H / 2 + 0.3, -0.24, -0.05, blackplastic, bev=0.008)

# ----- the lid: smoked perspex, just under the panel --------------------------------------------
lid = box("Lid", -WIN_W / 2, WIN_W / 2, -WIN_H / 2, WIN_H / 2, -0.04, -0.02, cavity)
lid.data.materials.clear()
lm = bpy.data.materials.new("Perspex")
lm.use_nodes = True
ln = lm.node_tree
ln.nodes.remove(ln.nodes["Principled BSDF"])
lout = ln.nodes["Material Output"]
NEON_LAYER = LAYER[5:] if LAYER.startswith("neon_") else None
if LAYER != "perspex":
    for k, parts in neon_gas.items():
        if k != NEON_LAYER:
            for p in parts:
                p.hide_render = True      # an unlit bulb: no glow, no light
if LAYER in ("transport", "hubs") or NEON_LAYER:
    # The smoke only: light through it is dimmed and browned, in and out.
    smoke = ln.nodes.new("ShaderNodeBsdfTransparent")
    smoke.inputs["Color"].default_value = (0.82, 0.76, 0.68, 1)
    ln.links.new(smoke.outputs["BSDF"], lout.inputs["Surface"])
else:
    # The surface only: a little of the room, through fine scratches and smudges.
    gl = ln.nodes.new("ShaderNodeBsdfGlossy")
    co = ln.nodes.new("ShaderNodeTexCoord")
    mp = ln.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1.0, 140.0, 1.0)
    mp.inputs["Rotation"].default_value = (0, 0, math.radians(8))
    ln.links.new(co.outputs["Object"], mp.inputs["Vector"])
    sc = node(ln, "ShaderNodeTexNoise", Scale=1.5, Detail=4.0, Roughness=0.5)
    ln.links.new(mp.outputs["Vector"], sc.inputs["Vector"])
    scr = node(ln, "ShaderNodeMapRange", **{"From Min": 0.68, "From Max": 0.74, "To Min": 0.0, "To Max": 1.0})
    ln.links.new(sc.outputs["Fac"], scr.inputs["Value"])
    sm = node(ln, "ShaderNodeTexNoise", Scale=1.2, Detail=8.0, Roughness=0.6)
    smr = node(ln, "ShaderNodeMapRange", **{"From Min": 0.4, "From Max": 0.75, "To Min": 0.03, "To Max": 0.2})
    ln.links.new(sm.outputs["Fac"], smr.inputs["Value"])
    ln.links.new(smr.outputs["Result"], gl.inputs["Roughness"])
    refl = ln.nodes.new("ShaderNodeMath")
    refl.operation = "MULTIPLY_ADD"
    refl.use_clamp = True
    ln.links.new(scr.outputs["Result"], refl.inputs[0])
    refl.inputs[1].default_value = 0.25          # scratches catch more light
    refl.inputs[2].default_value = 0.05          # acrylic's own reflectance
    clear = ln.nodes.new("ShaderNodeBsdfTransparent")
    mix = ln.nodes.new("ShaderNodeMixShader")
    ln.links.new(refl.outputs["Value"], mix.inputs["Fac"])
    ln.links.new(clear.outputs["BSDF"], mix.inputs[1])
    ln.links.new(gl.outputs["BSDF"], mix.inputs[2])
    ln.links.new(mix.outputs["Shader"], lout.inputs["Surface"])
lid.data.materials.append(lm)

if LAYER == "perspex":
    for ob in scene.objects:
        if ob.type in {"MESH", "CURVE"} and ob is not lid:
            ob.visible_camera = False
elif LAYER == "transport" or NEON_LAYER:
    for ob in turning:
        ob.hide_render = True            # drawn by the hubs layer, turning; gone here entirely,
                                         # or they would still shade the chrome under them
else:  # hubs
    for ob in scene.objects:
        if ob.type in {"MESH", "CURVE"} and ob not in turning and ob is not lid:
            ob.visible_camera = False
    lid.hide_render = True               # the page lays the lid over the hubs as well

# ----- camera: the bezel's, cropped to the opening -----------------------------------------------
cam_data = bpy.data.cameras.new("Camera")
cam_data.type = "PERSP"
cam_data.sensor_fit = "HORIZONTAL"
cam_data.sensor_width = 36
cam_data.lens = DIST * 36 / WIN_W
cam_data.clip_end = 200
cam = bpy.data.objects.new("Camera", cam_data)
cam.location = (0, 0, DIST)
scene.collection.objects.link(cam)
scene.camera = cam

# ----- light: the knobs' key as a sun from the top of the unit, the room in reflections -----------
sun_data = bpy.data.lights.new("Key", "SUN")
sun_data.energy = 2.3
sun_data.angle = math.radians(30)
sun = bpy.data.objects.new("Key", sun_data)
sun.rotation_mode = "QUATERNION"
sun.rotation_quaternion = (Vector((0, 0, 0.3)) - Vector((0, 3.4, 4.0))).to_track_quat("-Z", "Y")
scene.collection.objects.link(sun)

image = bpy.data.images.load(ROOM)
iw, ih = image.size
pxs = image.pixels[:]
best, best_u = -1.0, 0.5
for y in range(ih // 2, ih, 4):
    for x in range(0, iw, 4):
        i = (y * iw + x) * 4
        lum = 0.2126 * pxs[i] + 0.7152 * pxs[i + 1] + 0.0722 * pxs[i + 2]
        if lum > best:
            best, best_u = lum, x / iw
phi = 2 * math.pi * (best_u - 0.5)
bright_az = math.atan2(math.sin(phi), -math.cos(phi))
world = bpy.data.worlds.new("World")
world.use_nodes = True
wn = world.node_tree
wc = wn.nodes.new("ShaderNodeTexCoord")
wm = wn.nodes.new("ShaderNodeMapping")
wm.inputs["Rotation"].default_value = (0.0, 0.0, bright_az - math.pi / 2)
env = wn.nodes.new("ShaderNodeTexEnvironment")
env.image = image
wn.links.new(wc.outputs["Generated"], wm.inputs["Vector"])
wn.links.new(wm.outputs["Vector"], env.inputs["Vector"])
hue = wn.nodes.new("ShaderNodeHueSaturation")
hue.inputs["Saturation"].default_value = 0.5
wn.links.new(env.outputs["Color"], hue.inputs["Color"])
bg = wn.nodes["Background"]
bg.inputs["Strength"].default_value = 0.8 if LAYER == "transport" else 0.25
wn.links.new(hue.outputs["Color"], bg.inputs["Color"])
dim = wn.nodes.new("ShaderNodeBackground")
dim.inputs["Strength"].default_value = 0.16
wn.links.new(hue.outputs["Color"], dim.inputs["Color"])
lp = wn.nodes.new("ShaderNodeLightPath")
mixw = wn.nodes.new("ShaderNodeMixShader")
wn.links.new(lp.outputs["Is Glossy Ray"], mixw.inputs["Fac"])
wn.links.new(dim.outputs["Background"], mixw.inputs[1])
wn.links.new(bg.outputs["Background"], mixw.inputs[2])
final = mixw
if LAYER == "perspex":
    black = wn.nodes.new("ShaderNodeBackground")
    black.inputs["Color"].default_value = (0, 0, 0, 1)
    cm = wn.nodes.new("ShaderNodeMixShader")
    wn.links.new(lp.outputs["Is Camera Ray"], cm.inputs["Fac"])
    wn.links.new(mixw.outputs["Shader"], cm.inputs[1])
    wn.links.new(black.outputs["Background"], cm.inputs[2])
    final = cm
wn.links.new(final.outputs["Shader"], wn.nodes["World Output"].inputs["Surface"])
scene.world = world
if NEON_LAYER:
    for ob in scene.objects:
        if ob.type == "LIGHT" and not ob.name.startswith("NeonLight"):
            ob.hide_render = True
    bg.inputs["Strength"].default_value = 0.0
    dim.inputs["Strength"].default_value = 0.0

# ----- render ------------------------------------------------------------------------------------
scene.render.engine = "CYCLES"
scene.cycles.samples = 768 if LAYER != "perspex" else 256
scene.cycles.use_denoising = True
scene.cycles.transparent_max_bounces = 16
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
scene.render.film_transparent = LAYER == "hubs"
scene.render.resolution_x = round(WIN_W * PX * SCALE)
scene.render.resolution_y = round(WIN_H * PX * SCALE)
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA" if LAYER == "hubs" else "RGB"
scene.view_settings.view_transform = "Standard"
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
def project(x, y, z):
    k = PX * SCALE * DIST / (DIST - z)
    return (round(WIN_W * PX * SCALE / 2 + x * k, 1), round(WIN_H * PX * SCALE / 2 - y * k, 1))


print("rendered", LAYER, OUT)
print("HUB pinch", project(CAP_X, py, HUB_TOP), "tension", project(ROLL[0], ROLL[1], CAP_TOP))
print("NEON", {k: project(HEAD_X[k], NEON_Y, NEON_Z) for k in "R1234"})
