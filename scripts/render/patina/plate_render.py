"""Blender (4.2) script: Patina's flat parts, each seen head-on and lit as Corona's are
(common.py: the key as a distant sun from the top of the unit, a soft fall-off from
the top, a broad soft source high above for the sheen, the room in the reflections),
its shadow caught on a floor of what it sits on and otherwise transparent.

  panel      the whole face, 1160 x 660 page px: slate-green hammertone enamel on
             steel, the hammered dimples catching the light, a rounded edge.
  module     a stage's plate, 142 x 548: grey-green satin enamel, a fine orange
             peel, worn a little at its edges, four slotted screws 9 px in.
  master     the master plate, 216 x 548, the same enamel and screws.
  nameplate  the nameplate, 300 x 48: brushed clear-anodised aluminium, a slotted
             screw at each end.
  window     a stage window's bezel, 126 x 96: satin black crinkle paint, its
             opening (112 x 82, corners 3 px, which the card fills with the lit
             glass) following its outer corners' curve.
  display    the preset display's bezel, 300 x 44: the same, its opening 288 x 32.

Every part but the panel has a 12 px margin on every side for its shadow. Images
are at twice the page size (100 px per unit). The camera is 80 units away, so a
part keeps its outline to a pixel while the sheen still drifts across it.

Run: blender --background --factory-startup --python plate_render.py -- <kind> <out.png>
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "corona"))
import common as C  # noqa: E402
import bmesh  # noqa: E402
import bpy  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
KIND, OUT = argv[0], argv[1]
PX = 100.0                       # image px per unit (twice the page's 50)
MARGIN = 12 / 50                 # 12 page px

HAMMER = "#38433E"               # the face's hammertone
ENAMEL = "#A3ABA3"               # the plates' grey-green enamel
ALUMINIUM = "#C8CBCA"
BLACK = "#141615"

SIZES = {                        # page px
    "panel": (1160, 660), "module": (142, 548), "master": (216, 548),
    "nameplate": (300, 48), "window": (126, 96), "display": (300, 44),
}
pw, ph = SIZES[KIND]
W2, H2 = pw / 50 / 2, ph / 50 / 2          # half size in units
scene = C.reset()


# ----- materials --------------------------------------------------------------------------------
def tint_by(nt, p, colour, factor):
    """Base colour = `colour` times a grey factor (a socket)."""
    cm = nt.nodes.new("ShaderNodeCombineColor")
    for ch in ("Red", "Green", "Blue"):
        nt.links.new(factor, cm.inputs[ch])
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.blend_type = "MULTIPLY"
    mix.inputs["Factor"].default_value = 1.0
    mix.inputs["A"].default_value = (*C.lin(colour), 1)
    nt.links.new(cm.outputs["Color"], mix.inputs["B"])
    nt.links.new(mix.outputs["Result"], p.inputs["Base Color"])


def hammertone():
    """Hammertone enamel: a metallic-flecked paint that dried into shallow round
    dimples, each a soft cell (Voronoi) catching the key on its upper rim; the
    tone darker in the dimples' floors, a broad mottle across the sheet."""
    m = C.material("Hammertone", C.lin(HAMMER), metallic=0.6, rough=0.34)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Coat Weight"].default_value = 0.25
    p.inputs["Coat Roughness"].default_value = 0.3
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    vor = nt.nodes.new("ShaderNodeTexVoronoi")
    vor.feature = "SMOOTH_F1"
    vor.inputs["Scale"].default_value = float(os.environ.get("HAMMER_SCALE", "3.4"))
    vor.inputs["Smoothness"].default_value = 1.0
    vor.inputs["Randomness"].default_value = 0.9
    warp = nt.nodes.new("ShaderNodeVectorMath")
    warp.operation = "ADD"
    nt.links.new(co, warp.inputs[0])
    wn = nt.nodes.new("ShaderNodeTexNoise")
    wn.inputs["Scale"].default_value = 3.0
    wn.inputs["Detail"].default_value = 3.0
    nt.links.new(co, wn.inputs["Vector"])
    ws = nt.nodes.new("ShaderNodeVectorMath")
    ws.operation = "SCALE"
    ws.inputs["Scale"].default_value = 0.12
    nt.links.new(wn.outputs["Color"], ws.inputs[0])
    nt.links.new(ws.outputs["Vector"], warp.inputs[1])
    nt.links.new(warp.outputs["Vector"], vor.inputs["Vector"])
    cell = vor.outputs["Distance"]
    fine = C.noise(nt, co, 90.0, detail=4.0)
    h = C.math_node(nt, "MULTIPLY_ADD", fine, 0.05, C.math_node(nt, "POWER", cell, 0.7))
    bump = C.node(nt, "ShaderNodeBump", Strength=0.45, Distance=0.05)
    nt.links.new(h, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    floor = C.ramp(nt, cell, 0.0, 0.5, 0.92, 1.05)
    mott = C.ramp(nt, C.noise(nt, co, 0.6, detail=4.0), 0.3, 0.7, 0.9, 1.06)
    tint_by(nt, p, HAMMER, C.math_node(nt, "MULTIPLY", floor, mott))
    sparkle = C.ramp(nt, C.noise(nt, co, 400.0, detail=2.0), 0.4, 0.6, 0.0, 0.1)
    rr = C.math_node(nt, "ADD", C.ramp(nt, cell, 0.0, 0.5, 0.40, 0.26), sparkle)
    nt.links.new(rr, p.inputs["Roughness"])
    return m


def enamel(colour=ENAMEL):
    """Satin enamel: a fine orange peel, the tone faintly uneven, handled to a
    slightly higher gloss in patches."""
    m = C.material("Enamel", C.lin(colour), rough=0.42)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Specular IOR Level"].default_value = 0.45
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    peel = C.noise(nt, co, 70.0, detail=3.0)
    bump = C.node(nt, "ShaderNodeBump", Strength=0.07, Distance=0.004)
    nt.links.new(peel, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    mott = C.ramp(nt, C.noise(nt, co, 0.9, detail=5.0), 0.3, 0.7, 0.94, 1.04)
    grime = C.ramp(nt, C.noise(nt, co, 4.0, detail=6.0), 0.55, 0.75, 1.0, 0.9)
    tint_by(nt, p, colour, C.math_node(nt, "MULTIPLY", mott, grime))
    rr = C.ramp(nt, C.noise(nt, co, 2.0, detail=4.0), 0.3, 0.7, 0.34, 0.5)
    nt.links.new(rr, p.inputs["Roughness"])
    return m


def brushed(colour=ALUMINIUM):
    """Brushed clear-anodised aluminium, the grain along X (Corona's anodised())."""
    m = C.material("Aluminium", C.lin(colour), metallic=1.0)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    streak = C.noise(nt, co, 1.0, sx=0.6, sy=520.0, detail=10.0, rough=0.62)
    streak2 = C.noise(nt, co, 1.0, sx=3.0, sy=1500.0, detail=4.0, rough=0.5)
    h = C.math_node(nt, "MULTIPLY_ADD", streak2, 0.5, streak)
    bump = C.node(nt, "ShaderNodeBump", Strength=0.35, Distance=0.002)
    nt.links.new(h, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    p.inputs["Anisotropic"].default_value = 0.75
    tan = C.node(nt, "ShaderNodeCombineXYZ", X=1.0, Y=0.0, Z=0.0)
    nt.links.new(tan.outputs["Vector"], p.inputs["Tangent"])
    nt.links.new(C.ramp(nt, streak, 0.3, 0.7, 0.24, 0.34), p.inputs["Roughness"])
    tint_by(nt, p, colour, C.ramp(nt, C.noise(nt, co, 0.5, detail=4.0), 0.3, 0.7, 0.94, 1.03))
    return m


def crinkle():
    """Satin black crinkle paint: a fine wrinkled bump, a soft sheen."""
    m = C.material("Crinkle", C.lin(BLACK), rough=0.55)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    p.inputs["Specular IOR Level"].default_value = 0.4
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    wr = C.noise(nt, co, 40.0, detail=8.0, rough=0.7)
    bump = C.node(nt, "ShaderNodeBump", Strength=0.35, Distance=0.004)
    nt.links.new(wr, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    return m


def screw_steel():
    m = C.material("Screw", C.lin("#8E8B84"), metallic=1.0, rough=0.3)
    nt = m.node_tree
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    nt.links.new(C.ramp(nt, C.noise(nt, co, 40.0, detail=6.0), 0.3, 0.7, 0.24, 0.42),
                 nt.nodes["Principled BSDF"].inputs["Roughness"])
    return m


# ----- geometry (Corona's plate_render.py) ---------------------------------------------------------
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


def screw(x, y, z, mat, angle, R=0.09):
    """A slotted pan-head screw on the part at (x, y, z)."""
    prof = [(0.0, z + 0.045), (R * 0.45, z + 0.043), (R * 0.75, z + 0.036), (R * 0.93, z + 0.024),
            (R, z + 0.011), (R, z), (0.0, z)]
    head = C.revolve(scene, "Screw", prof, mat, segments=64)
    head.location = (x, y, 0)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(x, y, z + 0.048))
    cut = bpy.context.active_object
    cut.scale = (R * 2.4, 0.022, 0.06)
    cut.rotation_euler = (0, 0, angle)
    cut.hide_render = True
    cut.display_type = "WIRE"
    b = head.modifiers.new("cut", "BOOLEAN")
    b.operation = "DIFFERENCE"
    b.solver = "EXACT"
    b.object = cut
    return head


# ----- the part ---------------------------------------------------------------------------------
R6 = 6 / 50
OPENING_RADIUS = 3 / 50
floor_colour = HAMMER
margin = MARGIN
if KIND == "panel":
    margin = 0.0
    top = 0.2
    plate("Panel", W2, H2, 8 / 50, 0.0, top, 0.06, hammertone())
elif KIND in ("module", "master"):
    z0, top = 0.06, 0.14                       # on stand-offs, so it casts a soft shadow
    plate("Plate", W2, H2, 5 / 50, z0, top, 0.03, enamel())
    steel = screw_steel()
    inset = 9 / 50
    for (sx, sy), a in zip(((-1, 1), (1, 1), (1, -1), (-1, -1)), (0.5, 2.1, 2.8, 1.2)):
        screw(sx * (W2 - inset), sy * (H2 - inset), top, steel, a)
elif KIND == "nameplate":
    z0, top = 0.04, 0.1
    plate("Plate", W2, H2, 4 / 50, z0, top, 0.02, brushed())
    steel = screw_steel()
    for sx, a in ((-1, 0.7), (1, 2.3)):
        screw(sx * (W2 - 14 / 50), 0.0, top, steel, a, R=0.1)
elif KIND in ("window", "display"):
    frame = (7 if KIND == "window" else 6) / 50
    z0, top = 0.0, 0.12
    # The opening's corners keep a 3 px radius, following the outer corner's curve
    # (the card clips the glass to the same).
    ring_frame("Bezel", W2, H2, frame + OPENING_RADIUS, frame, z0, top, 0.035, crinkle())
    # The bed under the glass (the card draws the lit glass over it).
    bm = bmesh.new()
    for x, y in ((-W2, -H2), (W2, -H2), (W2, H2), (-W2, H2)):
        bm.verts.new((x * 0.99, y * 0.99, 0.01))
    bm.faces.new(bm.verts[:])
    C.link(scene, "Bed", bm, C.material("Bed", (0.01, 0.01, 0.01), rough=0.7))
    floor_colour = ENAMEL

C.shadow_floor(scene, floor_colour, size=80)

# ----- camera and light -------------------------------------------------------------------------
fw, fh = 2 * (W2 + margin), 2 * (H2 + margin)
C.camera_down(scene, fw, fh, plane_z=top, dist=80.0)
C.key_sun(scene, energy=2.3, angle=float(os.environ.get("PLATE_SUN_ANGLE", "12")))
C.area(scene, "TopFall", 30.0, 900, (0, 6 + 4, 7), (0, 0, 0), shape="RECTANGLE", size_y=3.0, glossy=False)
C.area(scene, "Sheen", 34.0, 1800 * float(os.environ.get("PLATE_SHEEN", "1.0")), (0, 9, 30), (0, 0, 0),
       shape="RECTANGLE", size_y=14.0, diffuse=False, shadow=False)
C.room(scene)
C.render(scene, OUT, round(fw * PX), round(fh * PX), samples=1024, transparent=KIND != "panel",
         denoise=os.environ.get("PLATE_DENOISE", "0") == "1")
