"""Blender (4.2) script: a guitar or bass speaker seen straight on from the front
(orthographic), for the cabinet page's baffle. Generic, ours: a stamped steel basket
in dark grey hammertone with four mounting bolts and a gasket, a cloth surround,
a paper cone with faint concentric ribs, and a dust cap.

  12  felt dust cap, the usual depth of cone
  10  paper dust cap, a shallower cone
  15  a slightly domed aluminium dust cap, a deeper cone

The basket's outer rim is the image's width (radius 1 unit); outside it the image
is transparent. Lit as every part is: the key from the top of the unit, scaled to
the speaker's size on the page (300 px across for a 12", so 1 unit here is 3 page
units), the room in the reflections. The rim and surround shade the recessed cone
toward the top.

Run: blender --background --factory-startup --python speaker_render.py -- <12|10|15> <out.png> [size_px]
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common as C  # noqa: E402
import bpy  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
SIZE_IN, OUT = argv[0], argv[1]
PXS = int(argv[2]) if len(argv) > 2 else 1600
DEPTH = {"12": 0.36, "10": 0.25, "15": 0.5}[SIZE_IN]
CAP_R = {"12": 0.3, "10": 0.29, "15": 0.33}[SIZE_IN]
CAP_H = {"12": 0.085, "10": 0.06, "15": 0.12}[SIZE_IN]
RIBS = {"12": 7, "10": 5, "15": 9}[SIZE_IN]

scene = C.reset()


# ----- materials --------------------------------------------------------------------------------
def hammertone():
    m = C.material("Hammertone", C.lin("#3A3C3D"), metallic=0.65, rough=0.38)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    v = nt.nodes.new("ShaderNodeTexVoronoi")
    v.feature = "SMOOTH_F1"
    v.inputs["Scale"].default_value = 70.0
    v.inputs["Smoothness"].default_value = 0.6
    nt.links.new(co, v.inputs["Vector"])
    bump = C.node(nt, "ShaderNodeBump", Strength=0.45, Distance=0.004)
    nt.links.new(v.outputs["Distance"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    cell = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(v.outputs["Color"], cell.inputs["Color"])
    t = C.ramp(nt, cell.outputs["Red"], 0.0, 1.0, 0.82, 1.12)
    cm = nt.nodes.new("ShaderNodeCombineColor")
    for ch in ("Red", "Green", "Blue"):
        nt.links.new(t, cm.inputs[ch])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.blend_type = "MULTIPLY"
    mix.inputs["Factor"].default_value = 1.0
    mix.inputs["A"].default_value = (*C.lin("#3A3C3D"), 1)
    nt.links.new(cm.outputs["Color"], mix.inputs["B"])
    nt.links.new(mix.outputs["Result"], p.inputs["Base Color"])
    return m


def radial_coords(nt):
    """(r, theta) of the object's XY as sockets."""
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(co, sep.inputs["Vector"])
    xy = C.node(nt, "ShaderNodeCombineXYZ")
    nt.links.new(sep.outputs["X"], xy.inputs["X"])
    nt.links.new(sep.outputs["Y"], xy.inputs["Y"])
    ln = nt.nodes.new("ShaderNodeVectorMath")
    ln.operation = "LENGTH"
    nt.links.new(xy.outputs["Vector"], ln.inputs[0])
    th = C.math_node(nt, "ARCTAN2", sep.outputs["Y"], sep.outputs["X"])
    return co, ln.outputs["Value"], th


def paper(name, colour, ribs=0, rough=0.78, sheen=0.0):
    m = C.material(name, C.lin(colour), rough=rough)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co, r, th = radial_coords(nt)
    fibre = C.noise(nt, co, 160.0, detail=8.0, rough=0.7)
    blotch = C.noise(nt, co, 5.0, detail=5.0)
    h = C.math_node(nt, "MULTIPLY", fibre, 0.35)
    if ribs:
        # Concentric ribs pressed into the outer part of the cone.
        rib = C.math_node(nt, "SINE", C.math_node(nt, "MULTIPLY", r, 2 * math.pi * ribs / 0.42))
        fade = C.ramp(nt, r, CAP_R + 0.12, 0.72, 0.0, 1.0)
        h = C.math_node(nt, "MULTIPLY_ADD", C.math_node(nt, "MULTIPLY", rib, fade), 0.6, h)
    bump = C.node(nt, "ShaderNodeBump", Strength=0.5 if ribs else 0.25, Distance=0.006)
    nt.links.new(h, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    t = C.ramp(nt, blotch, 0.3, 0.7, 0.88, 1.08)
    t = C.math_node(nt, "MULTIPLY", t, C.ramp(nt, fibre, 0.2, 0.8, 0.93, 1.05))
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
    p.inputs["Sheen Weight"].default_value = sheen
    return m


def felt():
    m = C.material("Felt", C.lin("#2B2724"), rough=0.95)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    fuzz = C.noise(nt, co, 300.0, detail=10.0, rough=0.8)
    bump = C.node(nt, "ShaderNodeBump", Strength=0.9, Distance=0.004)
    nt.links.new(fuzz, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    p.inputs["Sheen Weight"].default_value = 1.0
    p.inputs["Sheen Roughness"].default_value = 0.5
    p.inputs["Specular IOR Level"].default_value = 0.05
    return m


def spun_alu():
    m = C.material("CapAlu", (0.42, 0.42, 0.41), metallic=1.0, rough=0.55)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co, r, th = radial_coords(nt)
    p.inputs["Anisotropic"].default_value = 0.4
    tan = nt.nodes.new("ShaderNodeTangent")
    tan.direction_type = "RADIAL"
    tan.axis = "Z"
    nt.links.new(tan.outputs["Tangent"], p.inputs["Tangent"])
    rings = C.node(nt, "ShaderNodeTexNoise", Scale=1.0, Detail=5.0)
    rv = C.node(nt, "ShaderNodeCombineXYZ")
    nt.links.new(C.math_node(nt, "MULTIPLY", r, 600.0), rv.inputs["X"])
    nt.links.new(rv.outputs["Vector"], rings.inputs["Vector"])
    bump = C.node(nt, "ShaderNodeBump", Strength=0.06, Distance=0.002)
    nt.links.new(rings.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    return m


def fabric():
    m = C.material("Surround", C.lin("#252220"), rough=0.88)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    weave = C.noise(nt, co, 400.0, detail=4.0)
    bump = C.node(nt, "ShaderNodeBump", Strength=0.3, Distance=0.003)
    nt.links.new(weave, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    p.inputs["Coat Weight"].default_value = 0.15      # the doping that seals it
    p.inputs["Coat Roughness"].default_value = 0.45
    return m


steel = hammertone()
gasket_mat = C.material("Gasket", C.lin("#1A1817"), rough=0.85)
nut_mat = C.material("Zinc", C.lin("#8F8C84"), metallic=1.0, rough=0.35)

# ----- the speaker ------------------------------------------------------------------------------
# The basket's front flange, its outer edge rolled under.
C.revolve(scene, "Flange", [(0.86, -0.03), (0.865, 0.0), (0.88, 0.016), (0.92, 0.02), (0.975, 0.02),
                            (0.993, 0.013), (1.0, 0.0), (1.0, -0.04)], steel)
C.revolve(scene, "Gasket", [(0.862, 0.02), (0.862, 0.034), (0.866, 0.038), (0.91, 0.038), (0.914, 0.034),
                            (0.914, 0.02)], gasket_mat)
# The surround: a half roll from the gasket to the cone.
surround = [(0.866 - 0.076 * t, -0.006 + 0.04 * math.sin(math.pi * t) - 0.03 * t) for t in [i / 16 for i in range(17)]]
C.revolve(scene, "Surround", surround, fabric())
# The cone, curved a little (curvilinear), down to the voice coil.
r0, z0 = surround[-1]
cone = []
for i in range(25):
    s = i / 24
    cone.append((r0 - (r0 - CAP_R) * s, z0 - (DEPTH + z0) * s ** 0.82))
C.revolve(scene, "Cone", cone, paper("Cone", "#463D35", ribs=RIBS))
# The dust cap.
cap_mat = {"12": felt(), "10": paper("CapPaper", "#2F2A26", rough=0.6), "15": spun_alu()}[SIZE_IN]
cap = [(CAP_R * math.cos(math.radians(a)), -DEPTH + CAP_H * math.sin(math.radians(a))) for a in range(0, 91, 5)]
cap[-1] = (0.0, cap[-1][1])
C.revolve(scene, "Cap", cap, cap_mat)
# A ring of glue round the cap's edge.
C.revolve(scene, "Glue", [(CAP_R + 0.02, -DEPTH + 0.004), (CAP_R + 0.005, -DEPTH + 0.012), (CAP_R - 0.01, -DEPTH + 0.01)],
          C.material("Glue", C.lin("#1E1A17"), rough=0.4))
# Four mounting bolts through the flange, between the gasket and the rim.
for k in range(4):
    a = math.radians(45 + 90 * k)
    x, y = 0.955 * math.cos(a), 0.955 * math.sin(a)
    w = C.revolve(scene, "Washer", [(0.0, 0.026), (0.036, 0.026), (0.038, 0.022), (0.038, 0.02), (0.0, 0.02)], nut_mat, 48)
    w.location = (x, y, 0)
    bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.029, depth=0.022, location=(x, y, 0.037))
    nut = bpy.context.active_object
    nut.rotation_euler = (0, 0, a + 0.3)
    nut.data.materials.append(nut_mat)
    C.bevel(nut, 0.004, 2)
    st = C.revolve(scene, "Stud", [(0.0, 0.058), (0.012, 0.056), (0.0135, 0.05), (0.0135, 0.045), (0.0, 0.045)], nut_mat, 32)
    st.location = (x, y, 0)

# ----- camera and light -------------------------------------------------------------------------
C.camera_ortho(scene, 2.0)
S = 1 / 3                                   # 1 unit here is 3 page units (300 px across)
C.key(scene, s=S, energy=200.0, target_z=0.0)
C.area(scene, "Fill", 6.0 * S, 40.0 * S * S, (0, 0, 7.0 * S), (0, 0, 0))
C.room(scene)
C.render(scene, OUT, PXS, PXS, samples=1024, transparent=True, denoise=False)
