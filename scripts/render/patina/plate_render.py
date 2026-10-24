"""Blender (4.2) script: Patina's flat parts, each seen head-on and lit as Corona's are
(common.py: the key as a distant sun from the top of the unit, a soft fall-off from
the top, a broad soft source high above for the sheen, the room in the reflections),
its shadow caught on a floor of what it sits on and otherwise transparent.

  panel      the whole face, 1160 x 680 page px: slate-green hammertone enamel on
             steel, the hammered dimples catching the light, a rounded edge.
  module     a stage's plate, 142 x 568: grey-green satin enamel, a fine orange
             peel, worn a little at its edges, four slotted screws 9 px in.
  master     the master plate, 216 x 568, the same enamel and screws.
  nameplate  the nameplate, 300 x 56: a flat brushed stainless plate (square
             corners, a hairline edge) after decades on a rack (grime in its edges and round its screws, smudges, fine
             scratches, faint tea-staining), a slotted screw in each corner.
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
STAINLESS = "#C3C6C7"
BLACK = "#141615"

SIZES = {                        # page px
    "panel": (1160, 680), "module": (142, 568), "master": (216, 568),
    "nameplate": (300, 56), "window": (126, 96), "display": (300, 44),
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


def aged_stainless(w2, h2, screws_at):
    """Polished, brushed stainless steel after decades on a rack: the grain along
    X, smooth enough to mirror the room, a little duller in smudged patches; grime worked into its edges and round its screws,
    broken up so it reads as dirt and not a vignette; fine scratches across the
    grain; faint tea-staining (the warm rust film steel takes in damp air) and a
    few pits."""
    m = C.material("Stainless", C.lin(STAINLESS), metallic=1.0, rough=0.3)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    xyz = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(co, xyz.inputs["Vector"])
    x, y = xyz.outputs["X"], xyz.outputs["Y"]

    # The brushing, and scratches at a few angles across it.
    streak = C.noise(nt, co, 1.0, sx=0.5, sy=600.0, detail=10.0, rough=0.62)
    streak2 = C.noise(nt, co, 1.0, sx=2.5, sy=1600.0, detail=4.0, rough=0.5)
    height = C.math_node(nt, "MULTIPLY_ADD", streak2, 0.5, streak)
    scratches = None
    for angle in (0.35, -0.6, 1.2):
        rot = nt.nodes.new("ShaderNodeMapping")
        rot.inputs["Rotation"].default_value = (0.0, 0.0, angle)
        rot.inputs["Scale"].default_value = (0.25, 70.0, 1.0)
        nt.links.new(co, rot.inputs["Vector"])
        t = C.node(nt, "ShaderNodeTexNoise", Scale=3.0, Detail=2.0)
        nt.links.new(rot.outputs["Vector"], t.inputs["Vector"])
        line = C.ramp(nt, t.outputs["Fac"], 0.66, 0.68)
        # Scratches come and go along their length.
        gate = C.ramp(nt, C.noise(nt, co, 2.2, detail=2.0), 0.55, 0.65)
        line = C.math_node(nt, "MULTIPLY", line, gate)
        scratches = line if scratches is None else C.math_node(nt, "MAXIMUM", scratches, line)
    height = C.math_node(nt, "MULTIPLY_ADD", scratches, -0.8, height)
    bump = C.node(nt, "ShaderNodeBump", Strength=0.55, Distance=0.003)
    nt.links.new(height, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    p.inputs["Anisotropic"].default_value = 0.7
    tan = C.node(nt, "ShaderNodeCombineXYZ", X=1.0, Y=0.0, Z=0.0)
    nt.links.new(tan.outputs["Vector"], p.inputs["Tangent"])

    # Grime: near the edges (the distance to the nearest edge, broken up by
    # noise) and in a ring round each screw.
    ax = C.math_node(nt, "ABSOLUTE", x)
    ay = C.math_node(nt, "ABSOLUTE", y)
    dx = C.math_node(nt, "SUBTRACT", w2, ax)
    dy = C.math_node(nt, "SUBTRACT", h2, ay)
    edge = C.math_node(nt, "MINIMUM", dx, dy)
    breakup = C.noise(nt, co, 5.0, detail=8.0, rough=0.65)
    edge = C.math_node(nt, "MULTIPLY_ADD", breakup, 0.12, edge)
    grime = C.ramp(nt, edge, 0.03, 0.4, 1.0, 0.0)
    for sx, sy in screws_at:
        ddx = C.math_node(nt, "SUBTRACT", x, sx)
        ddy = C.math_node(nt, "SUBTRACT", y, sy)
        r = C.math_node(nt, "SQRT", C.math_node(nt, "ADD", C.math_node(nt, "MULTIPLY", ddx, ddx),
                                                C.math_node(nt, "MULTIPLY", ddy, ddy)))
        ring = C.ramp(nt, C.math_node(nt, "MULTIPLY_ADD", breakup, 0.05, r), 0.08, 0.26, 1.0, 0.0)
        grime = C.math_node(nt, "MAXIMUM", grime, ring)
    # Patches of grime reaching in from the edges, and broad smudges where hands have been.
    # Handled steel stays bright in its middle: the dirt gathers toward the edges.
    toward_edge = C.ramp(nt, edge, 0.0, 0.7, 1.0, 0.15)
    blotch = C.ramp(nt, C.noise(nt, co, 3.2, detail=8.0, rough=0.7), 0.5, 0.78, 0.0, 0.8)
    grime = C.math_node(nt, "MAXIMUM", grime, C.math_node(nt, "MULTIPLY", blotch, toward_edge))
    smudge = C.ramp(nt, C.noise(nt, co, 1.6, detail=3.0), 0.5, 0.75)
    # Tea staining and pits.
    stain = C.ramp(nt, C.noise(nt, co, 2.4, detail=7.0, rough=0.7), 0.48, 0.8, 0.0, 0.7)
    stain = C.math_node(nt, "MULTIPLY", stain, C.ramp(nt, edge, 0.0, 0.7, 1.0, 0.3))
    vor = nt.nodes.new("ShaderNodeTexVoronoi")
    vor.inputs["Scale"].default_value = 60.0
    nt.links.new(co, vor.inputs["Vector"])
    pits = C.ramp(nt, vor.outputs["Distance"], 0.0, 0.16, 1.0, 0.0)
    pits = C.math_node(nt, "MULTIPLY", pits, C.ramp(nt, C.noise(nt, co, 3.0), 0.5, 0.62))

    # Colour: the steel, toward the stain's warm brown, darkened by grime and pits,
    # the brushing faintly lighter and darker along the grain.
    col = nt.nodes.new("ShaderNodeMix")
    col.data_type = "RGBA"
    col.inputs["A"].default_value = (*C.lin(STAINLESS), 1)
    col.inputs["B"].default_value = (*C.lin("#5E4128"), 1)
    nt.links.new(stain, col.inputs["Factor"])
    dark = C.math_node(nt, "MULTIPLY_ADD", grime, -0.72, 1.0)
    dark = C.math_node(nt, "MULTIPLY_ADD", pits, -0.7, dark)
    dark = C.math_node(nt, "MULTIPLY", dark, C.ramp(nt, streak, 0.2, 0.8, 0.86, 1.1))
    shade = nt.nodes.new("ShaderNodeCombineColor")
    for ch in ("Red", "Green", "Blue"):
        nt.links.new(dark, shade.inputs[ch])
    tint = nt.nodes.new("ShaderNodeMix")
    tint.data_type = "RGBA"
    tint.blend_type = "MULTIPLY"
    tint.inputs["Factor"].default_value = 1.0
    nt.links.new(col.outputs["Result"], tint.inputs["A"])
    nt.links.new(shade.outputs["Color"], tint.inputs["B"])
    nt.links.new(tint.outputs["Result"], p.inputs["Base Color"])
    # Grime and stain dull it; smudges more so; scratches catch a little brighter.
    rough = C.ramp(nt, streak, 0.3, 0.7, 0.1, 0.19)
    rough = C.math_node(nt, "MULTIPLY_ADD", grime, 0.35, rough)
    rough = C.math_node(nt, "MULTIPLY_ADD", smudge, 0.08, rough)
    rough = C.math_node(nt, "MULTIPLY_ADD", stain, 0.4, rough)
    rough = C.math_node(nt, "MULTIPLY_ADD", scratches, -0.1, rough, clamp=True)
    nt.links.new(rough, p.inputs["Roughness"])
    # Grime and the stain are not metal: a film over it.
    metal = C.math_node(nt, "MULTIPLY_ADD", grime, -0.5, 1.0)
    metal = C.math_node(nt, "MULTIPLY_ADD", stain, -0.6, metal, clamp=True)
    nt.links.new(metal, p.inputs["Metallic"])
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
    """An old screw: zinc gone dull and dark, rust in its slot and round its rim."""
    m = C.material("Screw", C.lin("#7C7972"), metallic=1.0, rough=0.38)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    rust = C.ramp(nt, C.noise(nt, co, 30.0, detail=8.0, rough=0.7), 0.5, 0.72)
    col = nt.nodes.new("ShaderNodeMix")
    col.data_type = "RGBA"
    col.inputs["A"].default_value = (*C.lin("#7C7972"), 1)
    col.inputs["B"].default_value = (*C.lin("#5E3A20"), 1)
    nt.links.new(rust, col.inputs["Factor"])
    nt.links.new(col.outputs["Result"], p.inputs["Base Color"])
    nt.links.new(C.math_node(nt, "MULTIPLY_ADD", rust, 0.4, 0.34), p.inputs["Roughness"])
    nt.links.new(C.math_node(nt, "MULTIPLY_ADD", rust, -0.8, 1.0), p.inputs["Metallic"])
    return m


# ----- wear ---------------------------------------------------------------------------------------
STEEL = "#64625C"   # bare steel where the paint has gone
RUST = "#4A3122"
YELLOWED = "#A39D7E"
DUST = "#7D7A6C"


def _linked(nt, socket):
    """The output linked into `socket`, or None."""
    for l in nt.links:
        if l.to_socket == socket:
            return l.from_socket
    return None


def apply_wear(m, w2, h2, screws_at=(), amount=1.0, inner=None, yellow=0.0, seed=0.0, dust=0.0):
    """Decades of use over a painted part (the references: rack gear whose paint has
    worn through at every edge): the paint chipped back to bare steel along its
    edges and corners (and round an opening, `inner` its half size) and in a few
    spots across it, part of that steel rusted; worn rings round its screws; grime
    in its edges and settling toward its foot, streaks running down (on dark paint,
    `dust`: a pale film rather than darkening); scuffs and rubbed patches; light
    enamel yellowed by `yellow`. Takes over the material's
    colour, roughness and bump, keeping what it had as the paint."""
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co0 = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    # A seed offsets the noise, so two parts of one kind do not wear alike.
    off = nt.nodes.new("ShaderNodeVectorMath")
    off.operation = "ADD"
    off.inputs[1].default_value = (seed * 13.1, seed * 7.7, seed * 3.3)
    nt.links.new(co0, off.inputs[0])
    co = off.outputs["Vector"]
    xyz = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(co0, xyz.inputs["Vector"])
    x, y = xyz.outputs["X"], xyz.outputs["Y"]

    # The paint as it was.
    paint_colour = _linked(nt, p.inputs["Base Color"])
    if paint_colour is None:
        rgb = nt.nodes.new("ShaderNodeRGB")
        rgb.outputs[0].default_value = p.inputs["Base Color"].default_value
        paint_colour = rgb.outputs[0]
    paint_rough = _linked(nt, p.inputs["Roughness"])
    if paint_rough is None:
        v = nt.nodes.new("ShaderNodeValue")
        v.outputs[0].default_value = p.inputs["Roughness"].default_value
        paint_rough = v.outputs[0]
    paint_metal = p.inputs["Metallic"].default_value
    bump = None
    for n in nt.nodes:
        if n.type == "BUMP":
            bump = n
    paint_height = _linked(nt, bump.inputs["Height"]) if bump is not None else None

    # How far from an edge (the outer one, or an opening's).
    ax = C.math_node(nt, "ABSOLUTE", x)
    ay = C.math_node(nt, "ABSOLUTE", y)
    edge = C.math_node(nt, "MINIMUM", C.math_node(nt, "SUBTRACT", w2, ax), C.math_node(nt, "SUBTRACT", h2, ay))
    if inner is not None:
        to_opening = C.math_node(nt, "MAXIMUM", C.math_node(nt, "SUBTRACT", ax, inner[0]),
                                 C.math_node(nt, "SUBTRACT", ay, inner[1]))
        edge = C.math_node(nt, "MINIMUM", edge, to_opening)
    # Corners take the most knocks.
    corner = C.math_node(nt, "MULTIPLY", C.ramp(nt, C.math_node(nt, "SUBTRACT", w2, ax), 0.0, 0.9, 1.0, 0.0),
                         C.ramp(nt, C.math_node(nt, "SUBTRACT", h2, ay), 0.0, 0.9, 1.0, 0.0))

    # Chips: the edge's distance pushed about by noise at two scales, so the worn
    # band is ragged, with bites taken out of it, deepest at the corners.
    n1 = C.noise(nt, co, 4.5, detail=10.0, rough=0.72)
    n2 = C.noise(nt, co, 18.0, detail=6.0, rough=0.6)
    # How far in the paint has gone here (units: 50 px each): up to a third of a
    # unit along an edge, more at a corner.
    reach = C.math_node(nt, "MULTIPLY_ADD", n2, 0.07, C.ramp(nt, n1, 0.36, 0.8, 0.0, 0.6))
    reach = C.math_node(nt, "MULTIPLY", C.math_node(nt, "MULTIPLY_ADD", corner, 1.1, 0.5), reach)
    reach = C.math_node(nt, "MULTIPLY", reach, amount)
    chip = C.ramp(nt, C.math_node(nt, "SUBTRACT", edge, reach), -0.008, 0.008, 1.0, 0.0)
    # Flakes out in the face: irregular patches where the paint has lifted, their
    # edges ragged, more of them toward the edges than the middle.
    flakes = C.noise(nt, co, 1.8, detail=12.0, rough=0.72)
    flakes = C.math_node(nt, "MULTIPLY_ADD", n2, 0.08, flakes)
    near = C.ramp(nt, edge, 0.0, 2.5, 0.0, 0.05)
    spots = C.ramp(nt, C.math_node(nt, "ADD", flakes, near), 0.735 - 0.025 * amount, 0.745 - 0.025 * amount, 1.0, 0.0)
    spots = C.math_node(nt, "SUBTRACT", 1.0, spots)
    chip = C.math_node(nt, "MAXIMUM", chip, spots)
    # Round each screw, worn by the screwdriver and by hands.
    for sx, sy in screws_at:
        ddx = C.math_node(nt, "SUBTRACT", x, sx)
        ddy = C.math_node(nt, "SUBTRACT", y, sy)
        r = C.math_node(nt, "SQRT", C.math_node(nt, "ADD", C.math_node(nt, "MULTIPLY", ddx, ddx),
                                                C.math_node(nt, "MULTIPLY", ddy, ddy)))
        ring = C.ramp(nt, C.math_node(nt, "MULTIPLY_ADD", n1, 0.14, r), 0.12, 0.2 + 0.1 * amount, 1.0, 0.0)
        chip = C.math_node(nt, "MAXIMUM", chip, ring)
    rust = C.math_node(nt, "MULTIPLY", chip, C.ramp(nt, C.noise(nt, co, 7.0, detail=6.0), 0.48, 0.7))

    # Grime: in the edges, settling toward the foot, streaks running down.
    grime = C.ramp(nt, C.math_node(nt, "MULTIPLY_ADD", n1, 0.15, edge), 0.0, 0.55, 0.8, 0.0)
    low = C.ramp(nt, y, -h2, h2, 0.7, 0.0)
    low = C.math_node(nt, "MULTIPLY", low, C.ramp(nt, C.noise(nt, co, 2.0, detail=6.0), 0.3, 0.75, 0.4, 1.0))
    grime = C.math_node(nt, "MAXIMUM", grime, low)
    streak = C.ramp(nt, C.noise(nt, co, 1.0, sx=14.0, sy=0.7, detail=5.0), 0.55, 0.78, 0.0, 0.5)
    grime = C.math_node(nt, "MAXIMUM", grime, streak)
    blotch = C.ramp(nt, C.noise(nt, co, 2.6, detail=8.0, rough=0.7), 0.5, 0.8, 0.0, 0.8)
    grime = C.math_node(nt, "MAXIMUM", grime, blotch)
    # Scuffs: fine scratches through the paint's sheen, mostly across.
    scuff = C.ramp(nt, C.noise(nt, co, 1.0, sx=0.4, sy=90.0, detail=3.0), 0.68, 0.72)
    scuff = C.math_node(nt, "MULTIPLY", scuff, C.ramp(nt, C.noise(nt, co, 1.8), 0.5, 0.65))
    rubbed = C.ramp(nt, C.noise(nt, co, 1.2, detail=3.0), 0.55, 0.8)

    # Colour: the paint (yellowed, dirtied), then bare steel and rust where it chipped.
    painted = paint_colour
    if yellow > 0.0:
        ym = nt.nodes.new("ShaderNodeMix")
        ym.data_type = "RGBA"
        nt.links.new(C.ramp(nt, C.noise(nt, co, 1.1, detail=5.0), 0.3, 0.8, 0.2 * yellow, yellow), ym.inputs["Factor"])
        nt.links.new(painted, ym.inputs["A"])
        ym.inputs["B"].default_value = (*C.lin(YELLOWED), 1)
        painted = ym.outputs["Result"]
    dirt = C.math_node(nt, "MULTIPLY_ADD", grime, -0.6 * (1.0 - dust), 1.0)
    dirt = C.math_node(nt, "MULTIPLY_ADD", scuff, 0.25, dirt)
    shade = nt.nodes.new("ShaderNodeCombineColor")
    for ch in ("Red", "Green", "Blue"):
        nt.links.new(dirt, shade.inputs[ch])
    dirty = nt.nodes.new("ShaderNodeMix")
    dirty.data_type = "RGBA"
    dirty.blend_type = "MULTIPLY"
    dirty.inputs["Factor"].default_value = 1.0
    if dust > 0.0:
        # On dark paint the grime is dust: a pale, dead film.
        dm = nt.nodes.new("ShaderNodeMix")
        dm.data_type = "RGBA"
        nt.links.new(C.math_node(nt, "MULTIPLY", grime, dust), dm.inputs["Factor"])
        nt.links.new(painted, dm.inputs["A"])
        dm.inputs["B"].default_value = (*C.lin(DUST), 1)
        painted = dm.outputs["Result"]
    nt.links.new(painted, dirty.inputs["A"])
    nt.links.new(shade.outputs["Color"], dirty.inputs["B"])
    bare = nt.nodes.new("ShaderNodeMix")
    bare.data_type = "RGBA"
    bare.inputs["A"].default_value = (*C.lin(STEEL), 1)
    bare.inputs["B"].default_value = (*C.lin(RUST), 1)
    nt.links.new(C.ramp(nt, C.noise(nt, co, 7.0, detail=6.0), 0.5, 0.75, 0.0, 0.8), bare.inputs["Factor"])
    out = nt.nodes.new("ShaderNodeMix")
    out.data_type = "RGBA"
    nt.links.new(chip, out.inputs["Factor"])
    nt.links.new(dirty.outputs["Result"], out.inputs["A"])
    nt.links.new(bare.outputs["Result"], out.inputs["B"])
    nt.links.new(out.outputs["Result"], p.inputs["Base Color"])

    # Roughness: grime and rubbing dull the paint; steel is satin, rust dead.
    rough = C.math_node(nt, "MULTIPLY_ADD", grime, 0.3, paint_rough)
    rough = C.math_node(nt, "MULTIPLY_ADD", rubbed, 0.1, rough)
    rough = C.math_node(nt, "MULTIPLY_ADD", scuff, 0.15, rough)
    steel_rough = C.math_node(nt, "MULTIPLY_ADD", rust, 0.45, 0.4)
    rmix = nt.nodes.new("ShaderNodeMix")
    rmix.data_type = "FLOAT"
    nt.links.new(chip, rmix.inputs["Factor"])
    nt.links.new(rough, rmix.inputs[2])
    nt.links.new(steel_rough, rmix.inputs[3])
    nt.links.new(rmix.outputs[0], p.inputs["Roughness"])
    # Metallic: steel is, rust and paint (mostly) are not.
    metal = C.math_node(nt, "MULTIPLY_ADD", chip, 1.0 - paint_metal, paint_metal)
    metal = C.math_node(nt, "MULTIPLY_ADD", rust, -1.0, metal, clamp=True)
    nt.links.new(metal, p.inputs["Metallic"])

    # The chips sit lower than the paint: a step at their edge.
    if bump is None:
        bump = C.node(nt, "ShaderNodeBump", Strength=0.3, Distance=0.004)
        nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    step = C.math_node(nt, "MULTIPLY", chip, -0.5)
    step = C.math_node(nt, "MULTIPLY_ADD", C.math_node(nt, "MULTIPLY", rust, n2), 0.15, step)
    height = step if paint_height is None else C.math_node(nt, "ADD", C.math_node(nt, "MULTIPLY", paint_height,
                                                                              C.math_node(nt, "SUBTRACT", 1.0, chip)), step)
    nt.links.new(height, bump.inputs["Height"])
    bump.inputs["Strength"].default_value = max(bump.inputs["Strength"].default_value, 0.3)
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
    plate("Panel", W2, H2, 8 / 50, 0.0, top, 0.06, apply_wear(hammertone(), W2, H2, amount=1.3 * float(os.environ.get("PATINA_WEAR", "1.0")), dust=0.55))
elif KIND in ("module", "master"):
    z0, top = 0.06, 0.14                       # on stand-offs, so it casts a soft shadow
    steel = screw_steel()
    inset = 9 / 50
    at = [(sx * (W2 - inset), sy * (H2 - inset)) for sx, sy in ((-1, 1), (1, 1), (1, -1), (-1, -1))]
    seed = float(os.environ.get("PATINA_SEED", "0"))
    plate("Plate", W2, H2, 5 / 50, z0, top, 0.03,
          apply_wear(enamel(), W2, H2, at, amount=float(os.environ.get("PATINA_WEAR", "1.0")), yellow=0.6, seed=seed))
    for (sx, sy), a in zip(at, (0.5, 2.1, 2.8, 1.2)):
        screw(sx, sy, top, steel, a)
elif KIND == "nameplate":
    # A flat stainless plate cut from sheet: square corners (barely broken), a
    # hairline edge, a screw in each corner.
    z0, top = 0.02, 0.045
    inset = 10 / 50
    at = [(sx * (W2 - inset), sy * (H2 - inset)) for sx, sy in ((-1, 1), (1, 1), (1, -1), (-1, -1))]
    plate("Plate", W2, H2, 0.8 / 50, z0, top, 0.004, aged_stainless(W2, H2, at))
    steel = screw_steel()
    for (sx, sy), a in zip(at, (0.6, 2.2, 1.4, 2.9)):
        screw(sx, sy, top, steel, a, R=0.075)
elif KIND in ("window", "display"):
    frame = (7 if KIND == "window" else 6) / 50
    z0, top = 0.0, 0.12
    # The opening's corners keep a 3 px radius, following the outer corner's curve
    # (the card clips the glass to the same).
    ring_frame("Bezel", W2, H2, frame + OPENING_RADIUS, frame, z0, top, 0.035,
               apply_wear(crinkle(), W2, H2, amount=0.1 * float(os.environ.get("PATINA_WEAR", "1.0")), inner=(W2 - frame, H2 - frame)))
    # The bed under the glass (the card draws the lit glass over it): the
    # opening's shape, a little larger so it tucks under the frame, never past
    # the frame's rounded outer corners.
    bm = bmesh.new()
    for x, y in C.rrect(W2, H2, frame + OPENING_RADIUS, frame - 0.03):
        bm.verts.new((x, y, 0.01))
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
if KIND == "nameplate":
    # Polished steel mirrors what is over it: a broad soft panel of light above,
    # off to the left and seen only in reflections, lays a bright sheen across the
    # plate's left that fades into the darker room toward its right.
    C.area(scene, "Reflection", 9.0, float(os.environ.get("NAMEPLATE_SHEEN", "150")), (-4.5, 1.0, 7.0), (-4.5, 1.0, 0.0),
           shape="RECTANGLE", size_y=7.0, diffuse=False, shadow=False)
    C.room(scene, strength=0.55)
else:
    C.room(scene)
C.render(scene, OUT, round(fw * PX), round(fh * PX), samples=1024, transparent=KIND != "panel",
         denoise=os.environ.get("PLATE_DENOISE", "0") == "1")
