"""Blender (4.2) script: a 1970s VU meter on the cream panel, seen as the knobs and
toggles are (perspective, straight down its axis, the same key from the top of the
unit and the same room in its reflections). Rendered in layers the card stacks:

  body   bakelite bezel, blank cream dial, zero-adjust and corner screws, the
         shadows; the scale and needle are drawn on the dial by the page.
  hood   the needle's pivot cover alone, so the drawn needle passes under it.
  glass  the window's reflections alone: rendered against black, then turned
         into colour + alpha so it composites normally over the drawn dial.

Units: about 20 mm each. The image spans 5.6 units across the panel plane, at
150 px per unit; the meter's axis lands at pixel (420, 300) of 840 x 690.

Run: blender --background --factory-startup --python vu_render.py -- <body|hood|glass> <out.png>
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
LAYER, OUT = argv[0], argv[1]
SCALE = float(os.environ.get("VU_SCALE", "1.0"))   # resolution multiplier, for quick tests
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from render_config import hdri_path  # noqa: E402  (scripts/render: NORTHLIGHT_HDRI or render.local.json)
ROOM = hdri_path()  # Poly Haven, CC0
BULB_W = float(os.environ.get("VU_BULB", "45"))
DIAL_GLOW = float(os.environ.get("VU_GLOW", "0.3"))
DIST = 8.0
FRAME_W, FRAME_H, PX_PER_UNIT = 5.6, 4.6, 150
FRAME_CENTRE_Y = -0.3          # more room below, where the shadow falls

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene


# ----- geometry helpers --------------------------------------------------------------------------
FRAME = 0.18                   # the bezel's width round its window
W, H, R = 3.74 + 2 * FRAME, 2.34 + 2 * FRAME, 0.11   # its outline: the window plus the frame, hard corners
CORNER_SEGS = 12


def rrect(inset, min_r=0.03):
    """The bezel outline inset by `inset`, as points; every inset has the same count, so rings bridge."""
    w, h = W / 2 - inset, H / 2 - inset
    r = max(R - inset, min_r)
    pts = []
    for cx, cy, a0 in ((w - r, h - r, 0), (-(w - r), h - r, 90), (-(w - r), -(h - r), 180), (w - r, -(h - r), 270)):
        for i in range(CORNER_SEGS + 1):
            a = math.radians(a0 + 90 * i / CORNER_SEGS)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def link(name, bm, mat):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    scene.collection.objects.link(ob)
    ob.data.materials.append(mat)
    return ob


def bridge(bm, rings, close=True):
    n = len(rings[0])
    for lower, upper in zip(rings, rings[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((lower[i], lower[j], upper[j], upper[i]))
    if close:
        for i in range(n):  # the underside, from the last ring back to the first
            j = (i + 1) % n
            bm.faces.new((rings[-1][i], rings[-1][j], rings[0][j], rings[0][i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


def slab(name, inset, z0, z1, mat):
    bm = bmesh.new()
    lo = [bm.verts.new((x, y, z0)) for x, y in rrect(inset)]
    hi = [bm.verts.new((x, y, z1)) for x, y in rrect(inset)]
    bridge(bm, [lo, hi], close=False)
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return link(name, bm, mat)


def smooth_by_angle(ob, degrees=30):
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.shade_smooth_by_angle(angle=math.radians(degrees))
    ob.select_set(False)


def bevel(ob, width, segments=3):
    b = ob.modifiers.new("bevel", "BEVEL")
    b.width = width
    b.segments = segments
    b.limit_method = "ANGLE"


def node(nt, kind, **inputs):
    n = nt.nodes.new(kind)
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


# ----- materials ---------------------------------------------------------------------------------
def bakelite(name, rough=(0.26, 0.4)):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (0.012, 0.011, 0.010, 1)
    p.inputs["Coat Weight"].default_value = 0.25
    p.inputs["Coat Roughness"].default_value = 0.2
    # Handled for decades: gloss rubbed uneven, a few pale scuffs.
    wear = node(nt, "ShaderNodeTexNoise", Scale=5.0, Detail=6.0)
    rr = node(nt, "ShaderNodeMapRange", **{"From Min": 0.3, "From Max": 0.7, "To Min": rough[0], "To Max": rough[1]})
    nt.links.new(wear.outputs["Fac"], rr.inputs["Value"])
    nt.links.new(rr.outputs["Result"], p.inputs["Roughness"])
    scuff = node(nt, "ShaderNodeTexNoise", Scale=60.0, Detail=10.0, Roughness=0.7)
    sr = node(nt, "ShaderNodeMapRange", **{"From Min": 0.66, "From Max": 0.8, "To Min": 0.0, "To Max": 1.0})
    nt.links.new(scuff.outputs["Fac"], sr.inputs["Value"])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.inputs["A"].default_value = (0.012, 0.011, 0.010, 1)
    mix.inputs["B"].default_value = (0.06, 0.055, 0.05, 1)
    nt.links.new(sr.outputs["Result"], mix.inputs["Factor"])
    nt.links.new(mix.outputs["Result"], p.inputs["Base Color"])
    return m


def plain(name, base, metallic=0.0, rough=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*base, 1)
    p.inputs["Metallic"].default_value = metallic
    p.inputs["Roughness"].default_value = rough
    return m


bezel_mat = bakelite("Bezel")


def edge_wear(m, amount=1.0):
    """Paint worn through to dull steel on the convex edges, patchy, plus a few rubbed spots."""
    nt = m.node_tree
    out = nt.nodes["Material Output"]
    paint = nt.nodes["Principled BSDF"]
    steel = node(nt, "ShaderNodeBsdfPrincipled", Metallic=0.7, Roughness=0.55)
    steel.inputs["Base Color"].default_value = (0.56, 0.54, 0.50, 1)
    bev = node(nt, "ShaderNodeBevel", Radius=0.04)
    bev.samples = 16
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    dot = nt.nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    nt.links.new(bev.outputs["Normal"], dot.inputs[0])
    nt.links.new(geo.outputs["True Normal"], dot.inputs[1])
    edge = node(nt, "ShaderNodeMapRange", **{"From Min": 0.997, "From Max": 0.982, "To Min": 0.0, "To Max": 1.0})
    nt.links.new(dot.outputs["Value"], edge.inputs["Value"])
    patchy = node(nt, "ShaderNodeTexNoise", Scale=14.0, Detail=10.0, Roughness=0.65)
    patch_r = node(nt, "ShaderNodeMapRange", **{"From Min": 0.45, "From Max": 0.56, "To Min": 0.0, "To Max": 1.0})
    nt.links.new(patchy.outputs["Fac"], patch_r.inputs["Value"])
    m1 = node(nt, "ShaderNodeMath"); m1.operation = "MULTIPLY"; m1.use_clamp = True
    nt.links.new(edge.outputs["Result"], m1.inputs[0])
    nt.links.new(patch_r.outputs["Result"], m1.inputs[1])
    spots = node(nt, "ShaderNodeTexNoise", Scale=40.0, Detail=12.0, Roughness=0.75)
    spot_r = node(nt, "ShaderNodeMapRange", **{"From Min": 0.71, "From Max": 0.78, "To Min": 0.0, "To Max": 0.8})
    nt.links.new(spots.outputs["Fac"], spot_r.inputs["Value"])
    m2 = node(nt, "ShaderNodeMath"); m2.operation = "MAXIMUM"
    nt.links.new(m1.outputs["Value"], m2.inputs[0])
    nt.links.new(spot_r.outputs["Result"], m2.inputs[1])
    m3 = node(nt, "ShaderNodeMath"); m3.operation = "MULTIPLY"; m3.use_clamp = True
    m3.inputs[1].default_value = amount
    nt.links.new(m2.outputs["Value"], m3.inputs[0])
    mix = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(m3.outputs["Value"], mix.inputs["Fac"])
    nt.links.new(paint.outputs["BSDF"], mix.inputs[1])
    nt.links.new(steel.outputs["BSDF"], mix.inputs[2])
    nt.links.new(mix.outputs["Shader"], out.inputs["Surface"])
    if os.environ.get("VU_DEBUG_WEAR"):
        dbg = nt.nodes.new("ShaderNodeEmission")
        src = {"edge": edge.outputs["Result"], "dot": dot.outputs["Value"], "mask": m3.outputs["Value"]}[os.environ["VU_DEBUG_WEAR"]]
        nt.links.new(src, dbg.inputs["Strength"])
        nt.links.new(dbg.outputs["Emission"], out.inputs["Surface"])




def aluminium(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Metallic"].default_value = 1.0
    co = nt.nodes.new("ShaderNodeTexCoord")

    def tex(scale, sx=1.0, sy=1.0, detail=8.0, rough=0.6):
        mp = nt.nodes.new("ShaderNodeMapping")
        mp.inputs["Scale"].default_value = (sx, sy, 1.0)
        nt.links.new(co.outputs["Object"], mp.inputs["Vector"])
        t = node(nt, "ShaderNodeTexNoise", Scale=scale, Detail=detail, Roughness=rough)
        nt.links.new(mp.outputs["Vector"], t.inputs["Vector"])
        return t

    def ramp(src, lo, hi, a=0.0, b=1.0):
        r = node(nt, "ShaderNodeMapRange", **{"From Min": lo, "From Max": hi, "To Min": a, "To Max": b})
        nt.links.new(src, r.inputs["Value"])
        return r.outputs["Result"]

    def op(kind, a, b=None, clamp=True):
        n = nt.nodes.new("ShaderNodeMath")
        n.operation = kind
        n.use_clamp = clamp
        nt.links.new(a, n.inputs[0])
        if isinstance(b, float):
            n.inputs[1].default_value = b
        elif b is not None:
            nt.links.new(b, n.inputs[1])
        return n.outputs["Value"]

    # Satin grain, and scratches both ways (the frame's sides run both ways).
    grain = tex(1.0, 160.0, 160.0, 12.0, 0.7)
    sx = ramp(tex(1.0, 1.5, 260.0, 4.0, 0.5).outputs["Fac"], 0.67, 0.72)
    sy = ramp(tex(1.0, 260.0, 1.5, 4.0, 0.5).outputs["Fac"], 0.68, 0.73)
    scratches = op("MAXIMUM", sx, sy)
    hn = nt.nodes.new("ShaderNodeMath"); hn.operation = "MULTIPLY_ADD"
    nt.links.new(scratches, hn.inputs[0]); hn.inputs[1].default_value = -0.5
    nt.links.new(grain.outputs["Fac"], hn.inputs[2])
    bump = node(nt, "ShaderNodeBump", Strength=0.2, Distance=0.003)
    nt.links.new(hn.outputs["Value"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    # Oxidised patches: duller and a little darker.
    ox = ramp(tex(3.0, 1.0, 1.0, 8.0, 0.6).outputs["Fac"], 0.48, 0.66)
    # Edges rubbed bright by hands.
    bev = node(nt, "ShaderNodeBevel", Radius=0.04)
    bev.samples = 16
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    dot = nt.nodes.new("ShaderNodeVectorMath"); dot.operation = "DOT_PRODUCT"
    nt.links.new(bev.outputs["Normal"], dot.inputs[0])
    nt.links.new(geo.outputs["True Normal"], dot.inputs[1])
    rubbed = op("MULTIPLY", ramp(dot.outputs["Value"], 0.997, 0.982),
                ramp(tex(14.0).outputs["Fac"], 0.42, 0.56))
    r1 = ramp(ox, 0.0, 1.0, 0.3, 0.52)
    r2n = nt.nodes.new("ShaderNodeMath"); r2n.operation = "MULTIPLY_ADD"; r2n.use_clamp = True
    nt.links.new(scratches, r2n.inputs[0]); r2n.inputs[1].default_value = 0.12
    nt.links.new(r1, r2n.inputs[2])
    r3 = nt.nodes.new("ShaderNodeMath"); r3.operation = "MULTIPLY_ADD"; r3.use_clamp = True
    nt.links.new(rubbed, r3.inputs[0]); r3.inputs[1].default_value = -0.2
    nt.links.new(r2n.outputs["Value"], r3.inputs[2])
    nt.links.new(r3.outputs["Value"], p.inputs["Roughness"])
    col = nt.nodes.new("ShaderNodeMix"); col.data_type = "RGBA"
    col.inputs["A"].default_value = (0.84, 0.84, 0.83, 1)
    col.inputs["B"].default_value = (0.64, 0.62, 0.58, 1)
    nt.links.new(ox, col.inputs["Factor"])
    # Grime settled in the creases.
    ao = node(nt, "ShaderNodeAmbientOcclusion", Distance=0.06)
    ao.samples = 16
    grime = ramp(ao.outputs["AO"], 0.3, 0.9, 0.85, 0.0)
    dirty = nt.nodes.new("ShaderNodeMix"); dirty.data_type = "RGBA"
    dirty.inputs["B"].default_value = (0.10, 0.09, 0.07, 1)
    nt.links.new(col.outputs["Result"], dirty.inputs["A"])
    nt.links.new(grime, dirty.inputs["Factor"])
    nt.links.new(dirty.outputs["Result"], p.inputs["Base Color"])
    return m


if os.environ.get("BEZEL_FINISH", "paint") == "alu":
    bezel_mat = aluminium("BezelAlu")
else:
    edge_wear(bezel_mat)
liner_mat = plain("Liner", (0.62, 0.47, 0.27), rough=0.7)
hood_mat = bakelite("Hood", rough=(0.22, 0.34))
nickel = plain("Nickel", (0.70, 0.68, 0.64), metallic=1.0, rough=0.38)
slot_mat = plain("Slot", (0.02, 0.02, 0.02), rough=0.8)

# The dial: warm cream card, a little uneven with age.
dial = bpy.data.materials.new("Dial")
dial.use_nodes = True
dn = dial.node_tree
dp = dn.nodes["Principled BSDF"]
dp.inputs["Roughness"].default_value = 0.6
blot = node(dn, "ShaderNodeTexNoise", Scale=4.0, Detail=8.0, Roughness=0.6)
ramp = dn.nodes.new("ShaderNodeValToRGB")
ramp.color_ramp.elements[0].color = (0.76, 0.52, 0.23, 1)
ramp.color_ramp.elements[1].color = (0.95, 0.72, 0.38, 1)
dn.links.new(blot.outputs["Fac"], ramp.inputs["Fac"])
# Age spots: a few small brown flecks.
fox = node(dn, "ShaderNodeTexNoise", Scale=9.0, Detail=8.0, Roughness=0.7)
fox_r = node(dn, "ShaderNodeMapRange", **{"From Min": 0.6, "From Max": 0.68, "To Min": 0.0, "To Max": 0.6})
dn.links.new(fox.outputs["Fac"], fox_r.inputs["Value"])
aged = dn.nodes.new("ShaderNodeMix")
aged.data_type = "RGBA"
aged.inputs["B"].default_value = (0.50, 0.31, 0.13, 1)
dn.links.new(ramp.outputs["Color"], aged.inputs["A"])
dn.links.new(fox_r.outputs["Result"], aged.inputs["Factor"])
# Paper: fine fibres raised in its surface, and a faint grain in its colour.
fibre = node(dn, "ShaderNodeTexNoise", Scale=45.0, Detail=12.0, Roughness=0.75)
fibre_s = node(dn, "ShaderNodeMapping")
fibre_s.inputs["Scale"].default_value = (1.0, 3.0, 1.0)      # fibres run one way, as in card
dtc2 = dn.nodes.new("ShaderNodeTexCoord")
dn.links.new(dtc2.outputs["Object"], fibre_s.inputs["Vector"])
dn.links.new(fibre_s.outputs["Vector"], fibre.inputs["Vector"])
pbump = node(dn, "ShaderNodeBump", Strength=0.3, Distance=0.004)
dn.links.new(fibre.outputs["Fac"], pbump.inputs["Height"])
dn.links.new(pbump.outputs["Normal"], dp.inputs["Normal"])
grain = dn.nodes.new("ShaderNodeMix")
grain.data_type = "RGBA"
grain.blend_type = "MULTIPLY"
grain.inputs["Factor"].default_value = 0.32
dn.links.new(aged.outputs["Result"], grain.inputs["A"])
dn.links.new(fibre.outputs["Color"], grain.inputs["B"])
dn.links.new(grain.outputs["Result"], dp.inputs["Base Color"])
# Lit a little from behind, as the period's meters were: warm, strongest by the bulbs.
warm = dn.nodes.new("ShaderNodeMix")
warm.data_type = "RGBA"
warm.blend_type = "MULTIPLY"
warm.inputs["Factor"].default_value = 1.0
warm.inputs["B"].default_value = (1.0, 0.72, 0.42, 1)
dn.links.new(grain.outputs["Result"], warm.inputs["A"])
dn.links.new(warm.outputs["Result"], dp.inputs["Emission Color"])
dtc = dn.nodes.new("ShaderNodeTexCoord")
dsep = dn.nodes.new("ShaderNodeSeparateXYZ")
dn.links.new(dtc.outputs["Generated"], dsep.inputs["Vector"])
glow = node(dn, "ShaderNodeMapRange", **{"From Min": 0.0, "From Max": 1.0, "To Min": DIAL_GLOW * 1.6, "To Max": DIAL_GLOW * 0.55})
dn.links.new(dsep.outputs["Y"], glow.inputs["Value"])
dn.links.new(glow.outputs["Result"], dp.inputs["Emission Strength"])

# The glass: a little of the room reflected, a little smudged; the rest passes through.
glass_mat = bpy.data.materials.new("Glass")
glass_mat.use_nodes = True
gn = glass_mat.node_tree
gn.nodes.remove(gn.nodes["Principled BSDF"])
gout = gn.nodes["Material Output"]
gloss = gn.nodes.new("ShaderNodeBsdfGlossy")
smudge = node(gn, "ShaderNodeTexNoise", Scale=4.0, Detail=8.0, Roughness=0.6)
smr = node(gn, "ShaderNodeMapRange", **{"From Min": 0.4, "From Max": 0.75, "To Min": 0.02, "To Max": 0.16})
gn.links.new(smudge.outputs["Fac"], smr.inputs["Value"])
gn.links.new(smr.outputs["Result"], gloss.inputs["Roughness"])
refl = node(gn, "ShaderNodeMapRange", **{"From Min": 0.4, "From Max": 0.75, "To Min": 0.08, "To Max": 0.10})
gn.links.new(smudge.outputs["Fac"], refl.inputs["Value"])
clear = gn.nodes.new("ShaderNodeBsdfTransparent")
gmix = gn.nodes.new("ShaderNodeMixShader")
gn.links.new(refl.outputs["Result"], gmix.inputs["Fac"])
gn.links.new(clear.outputs["BSDF"], gmix.inputs[1])
gn.links.new(gloss.outputs["BSDF"], gmix.inputs[2])
gn.links.new(gmix.outputs["Shader"], gout.inputs["Surface"])

# ----- the meter ---------------------------------------------------------------------------------
# The bezel: a raised bakelite frame, chamfered into its window. (inset from the outline, height)
DIAL_Z = -0.35                               # the dial, sunk this far below the panel
profile = [(0.00, 0.00), (0.00, 0.16), (0.035, 0.21), (FRAME - 0.04, 0.21), (FRAME, 0.17), (FRAME, 0.10), (FRAME, DIAL_Z - 0.03)]
bm = bmesh.new()
rings = [[bm.verts.new((x, y, z)) for x, y in rrect(d)] for d, z in profile]
bridge(bm, rings)
bm.faces.ensure_lookup_table()
n_ring = len(rings[0])
for f in list(bm.faces)[5 * n_ring:6 * n_ring]:   # the well's walls, below the bezel's lip
    f.material_index = 1
bezel_ob = link("Bezel", bm, bezel_mat)
bezel_ob.data.materials.append(liner_mat)
bevel(bezel_ob, 0.012, 3)
smooth_by_angle(bezel_ob)

WIN_HALF_H = H / 2 - FRAME                    # the window's half height
dial_ob = slab("Dial", FRAME - 0.03, DIAL_Z - 0.06, DIAL_Z, dial)
glass_ob = slab("Glass", FRAME - 0.02, 0.16, 0.165, glass_mat)

# The needle's pivot hood: a trapezoid shelf standing off the dial, the bulbs under it.
bm = bmesh.new()
back_y, front_y = -WIN_HALF_H - 0.13, -0.70           # its back runs in under the bezel
z_lo = DIAL_Z + 0.13
corners = [(-0.85, back_y, z_lo), (0.85, back_y, z_lo), (0.55, front_y, z_lo), (-0.55, front_y, z_lo),
           (-0.85, back_y, DIAL_Z + 0.31), (0.85, back_y, DIAL_Z + 0.31), (0.55, front_y, DIAL_Z + 0.23), (-0.55, front_y, DIAL_Z + 0.23)]
v = [bm.verts.new(c) for c in corners]
for face in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
    bm.faces.new([v[i] for i in face])
bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
hood_ob = link("Hood", bm, hood_mat)
bevel(hood_ob, 0.025, 4)
smooth_by_angle(hood_ob)

# The bulbs: two small warm lamps under the hood.
for bx in (-0.6, 0.6):
    ld = bpy.data.lights.new("Bulb", "POINT")
    ld.energy = BULB_W
    ld.shadow_soft_size = 0.12
    ld.color = (1.0, 0.56, 0.22)
    lo = bpy.data.objects.new("Bulb", ld)
    lo.location = (bx, -1.02, DIAL_Z + 0.11)
    scene.collection.objects.link(lo)


def screw(x, y, z, r, turn, dome=0.03):
    bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=r, depth=0.035, location=(x, y, z + 0.0175))
    head = bpy.context.active_object
    head.data.materials.append(nickel)
    bevel(head, dome, 4)
    smooth_by_angle(head)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(x, y, z + 0.035))
    slot = bpy.context.active_object
    slot.scale = (r * 1.8, r * 0.26, 0.02)
    slot.rotation_euler = (0, 0, math.radians(turn))
    slot.data.materials.append(slot_mat)
    return [head, slot]


parts = [bezel_ob, dial_ob, hood_ob]
# Zero-adjust screws on the dial, and the four that hold the bezel.
parts += screw(-1.45, -0.86, DIAL_Z, 0.075, 20, dome=0.015)
parts += screw(1.45, -0.86, DIAL_Z, 0.075, -65, dome=0.015)
bezel_parts = [bezel_ob]
# (No corner screws: on a bezel this slim they made no sense.)
parts += bezel_parts[1:]

# The cream panel: catches the shadow, bounces its colour, otherwise transparent.
bm = bmesh.new()
hole = [bm.verts.new((x, y, 0)) for x, y in rrect(0.05)]
edge = [bm.verts.new((x * 6, y * 6, 0)) for x, y in rrect(0.05)]
bridge(bm, [hole, edge], close=False)
floor = link("Panel", bm, plain("CreamEnamel", (0.877, 0.797, 0.576), rough=0.45))
floor.is_shadow_catcher = True

# A window somewhere above the player, seen only in the glass: the sheen across its top left.
bpy.ops.mesh.primitive_plane_add(size=1, location=(-3.6, 3.1, 10.0))
strip = bpy.context.active_object
strip.scale = (40.0, 5.0, 1)
strip.rotation_euler = (0, 0, math.radians(-38))
sm = bpy.data.materials.new("Window")
sm.use_nodes = True
sn = sm.node_tree
sn.nodes.remove(sn.nodes["Principled BSDF"])
em = sn.nodes.new("ShaderNodeEmission")
tc = sn.nodes.new("ShaderNodeTexCoord")
sep = sn.nodes.new("ShaderNodeSeparateXYZ")
sn.links.new(tc.outputs["Generated"], sep.inputs["Vector"])
# Soft across its width: strength 3.0 * (1 - |2v - 1|)^3
m1 = node(sn, "ShaderNodeMath"); m1.operation = "MULTIPLY_ADD"; m1.inputs[1].default_value = 2.0; m1.inputs[2].default_value = -1.0
m2 = node(sn, "ShaderNodeMath"); m2.operation = "ABSOLUTE"
m3 = node(sn, "ShaderNodeMath"); m3.operation = "SUBTRACT"; m3.inputs[0].default_value = 1.0
m4 = node(sn, "ShaderNodeMath"); m4.operation = "POWER"; m4.inputs[1].default_value = 3.0
m5 = node(sn, "ShaderNodeMath"); m5.operation = "MULTIPLY"; m5.inputs[1].default_value = 2.2
sn.links.new(sep.outputs["Y"], m1.inputs[0])
sn.links.new(m1.outputs["Value"], m2.inputs[0])
sn.links.new(m2.outputs["Value"], m3.inputs[1])
sn.links.new(m3.outputs["Value"], m4.inputs[0])
sn.links.new(m4.outputs["Value"], m5.inputs[0])
sn.links.new(m5.outputs["Value"], em.inputs["Strength"])
em.inputs["Color"].default_value = (1.0, 0.97, 0.92, 1)
sn.links.new(em.outputs["Emission"], sn.nodes["Material Output"].inputs["Surface"])
strip.data.materials.append(sm)
for flag in ("visible_camera", "visible_diffuse", "visible_transmission", "visible_volume_scatter", "visible_shadow"):
    setattr(strip, flag, False)
strip.hide_render = True   # off: a window only the glass could see looked fake

# ----- which layer -------------------------------------------------------------------------------
if LAYER == "body":
    glass_ob.hide_render = True
    strip.hide_render = True
elif LAYER == "hood":
    glass_ob.hide_render = True
    strip.hide_render = True
    for ob in parts + [floor]:
        if ob is not hood_ob:
            ob.visible_camera = False
else:  # glass
    for ob in parts + [floor]:
        ob.visible_camera = False
if LAYER != "body":
    for ob in bezel_parts:      # what the bezel covers stays covered
        ob.visible_camera = True
        ob.is_holdout = True

# ----- camera: perspective, on the meter's axis --------------------------------------------------
cam_data = bpy.data.cameras.new("Camera")
cam_data.type = "PERSP"
cam_data.sensor_fit = "HORIZONTAL"
cam_data.sensor_width = 36
cam_data.lens = DIST * 36 / FRAME_W
cam_data.clip_end = 100
cam_data.shift_y = FRAME_CENTRE_Y / FRAME_W
cam = bpy.data.objects.new("Camera", cam_data)
cam.location = (0, 0, DIST)
scene.collection.objects.link(cam)
scene.camera = cam

# ----- light: the knobs' key, and the room in the reflections -----------------------------------
key_data = bpy.data.lights.new("Key", "AREA")
key_data.shape = "DISK"
key_data.size = 2.5
key_data.energy = 200
key = bpy.data.objects.new("Key", key_data)
key.location = (0, 3.4, 4.0)
key.rotation_mode = "QUATERNION"
key.rotation_quaternion = (Vector((0, 0, 0.3)) - key.location).to_track_quat("-Z", "Y")
scene.collection.objects.link(key)

image = bpy.data.images.load(ROOM)
w, h = image.size
px = image.pixels[:]
best, best_u = -1.0, 0.5
for y in range(h // 2, h, 4):
    for x in range(0, w, 4):
        i = (y * w + x) * 4
        lum = 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2]
        if lum > best:
            best, best_u = lum, x / w
phi = 2 * math.pi * (best_u - 0.5)
bright_az = math.atan2(math.sin(phi), -math.cos(phi))
world = bpy.data.worlds.new("World")
world.use_nodes = True
wn = world.node_tree
coord = wn.nodes.new("ShaderNodeTexCoord")
mapping = wn.nodes.new("ShaderNodeMapping")
mapping.inputs["Rotation"].default_value = (0.0, 0.0, bright_az - math.pi / 2)
env = wn.nodes.new("ShaderNodeTexEnvironment")
env.image = image
wn.links.new(coord.outputs["Generated"], mapping.inputs["Vector"])
wn.links.new(mapping.outputs["Vector"], env.inputs["Vector"])
hue = wn.nodes.new("ShaderNodeHueSaturation")
hue.inputs["Saturation"].default_value = 0.5
wn.links.new(env.outputs["Color"], hue.inputs["Color"])
bg = wn.nodes["Background"]
bg.inputs["Strength"].default_value = 0.8 if LAYER != "glass" else 0.2
wn.links.new(hue.outputs["Color"], bg.inputs["Color"])
dim = wn.nodes.new("ShaderNodeBackground")
dim.inputs["Strength"].default_value = 0.8 * 0.2
wn.links.new(hue.outputs["Color"], dim.inputs["Color"])
path = wn.nodes.new("ShaderNodeLightPath")
mix = wn.nodes.new("ShaderNodeMixShader")
wn.links.new(path.outputs["Is Glossy Ray"], mix.inputs["Fac"])
wn.links.new(dim.outputs["Background"], mix.inputs[1])
wn.links.new(bg.outputs["Background"], mix.inputs[2])
final = mix
if LAYER == "glass":
    # Against black: only what the glass reflects reaches the image.
    black = wn.nodes.new("ShaderNodeBackground")
    black.inputs["Color"].default_value = (0, 0, 0, 1)
    cam_mix = wn.nodes.new("ShaderNodeMixShader")
    wn.links.new(path.outputs["Is Camera Ray"], cam_mix.inputs["Fac"])
    wn.links.new(mix.outputs["Shader"], cam_mix.inputs[1])
    wn.links.new(black.outputs["Background"], cam_mix.inputs[2])
    final = cam_mix
wn.links.new(final.outputs["Shader"], wn.nodes["World Output"].inputs["Surface"])
scene.world = world

# ----- render ------------------------------------------------------------------------------------
scene.render.engine = "CYCLES"
scene.cycles.samples = int(os.environ.get("VU_SAMPLES", "1500")) if LAYER != "glass" else 256
scene.cycles.use_denoising = LAYER != "body"   # the denoiser would smooth the paper away
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
scene.render.film_transparent = LAYER != "glass"
scene.render.resolution_x = round(FRAME_W * PX_PER_UNIT * SCALE)
scene.render.resolution_y = round(FRAME_H * PX_PER_UNIT * SCALE)
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA" if LAYER != "glass" else "RGB"
scene.view_settings.view_transform = "Standard"
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("rendered vu", LAYER, OUT)
