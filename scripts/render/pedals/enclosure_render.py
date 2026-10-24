"""Blender (4.2) script: a guitar pedal's die-cast enclosure on a pedalboard, the whole
face of one of the guitar pedals (design-system.md "Rendered parts"; Corona's lighting
from scripts/render/corona/common.py: a key from the top of the unit, the room in the
reflections only).

The enclosure is a die-cast aluminium box with a draft on its sides, a generous
rounded edge round its top and rounded corners, painted in the pedal's finish and worn
the way a pedal is: the paint chipped back to bare aluminium along the rounded edge,
the top scuffed by boots round the footswitch. It stands on a pedalboard's dark loop
carpet, which catches its shadow. Nothing is printed on it: every legend is drawn by
the app.

Seen head-on from far above (orthographic), as the plates are. 1 unit is 50 face px:
the face is 360 x 600 px (7.2 x 12 units), the enclosure 312 x 560 px, centred.

  finish   gravity   cobalt metal-flake under a clear coat
           flare     signal orange in a fine sand-textured powder coat
           comet     deep green wrinkle finish
           tide      pale sea-glass gloss enamel
           polaris   graphite hammertone

Env: PEDAL_WEAR scales the wear (1.0), PEDAL_SEED moves it.

Run: blender --background --factory-startup --python enclosure_render.py -- <finish> <out.png> [px_per_unit]
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "corona"))
import common as C  # noqa: E402
import bpy  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
FINISH, OUT = argv[0], argv[1]
PX = int(argv[2]) if len(argv) > 2 else 100           # 2x the face's 50 px per unit
WEAR = float(os.environ.get("PEDAL_WEAR", "1.0"))
SEED = float(os.environ.get("PEDAL_SEED", "0"))

FACE_W, FACE_H = 7.2, 12.0
W, H = 3.12, 5.6            # the enclosure's half-size at its top (312 x 560 px)
CORNER = 0.42               # the corners' radius in plan
HEIGHT = 1.7                # its height (unseen head-on; it sets the shadow)
EDGE = 0.34                 # the rounded edge round the top
DRAFT = 0.07                # the sides flare this much toward the foot

FINISHES = {
    #          colour      kind
    "gravity": ("#1D3A86", "flake"),
    "flare":   ("#D8521C", "sand"),
    "comet":   ("#1F4436", "wrinkle"),
    "tide":    ("#9CCFC3", "gloss"),
    "polaris": ("#2A2C2E", "hammer"),
}
COLOUR, KIND = FINISHES[FINISH]

scene = C.reset()


# ----- the enclosure ---------------------------------------------------------------------------
def enclosure(mat):
    rings = []
    segs = 16
    # Up the drafted side, then over the rounded edge onto the flat top.
    for k in range(7):
        t = k / 6
        z = (HEIGHT - EDGE) * t
        grow = DRAFT * (1 - t)
        rings.append([(x, y, z) for x, y in C.rrect(W + grow, H + grow, CORNER + grow, 0.0, segs)])
    for k in range(1, 13):
        a = math.radians(90 * k / 12)
        inset = EDGE * (1 - math.cos(a))
        z = HEIGHT - EDGE + EDGE * math.sin(a)
        rings.append([(x, y, z) for x, y in C.rrect(W, H, CORNER, inset, segs)])
    ob = C.loft(scene, "Enclosure", rings, mat, cap_bottom=True, cap_top=True)
    C.smooth_by_angle(ob, 40)
    return ob


def finish_material():
    m = C.material("Paint", C.lin(COLOUR), rough=0.35, coat=0.0)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    base = C.lin(COLOUR)

    seeded = nt.nodes.new("ShaderNodeVectorMath")
    seeded.operation = "ADD"
    seeded.inputs[1].default_value = (SEED * 7.3, SEED * 3.1, 0.0)
    nt.links.new(co, seeded.inputs[0])
    sco = seeded.outputs["Vector"]

    bump_height = None
    rough = None
    metal = None
    colour = None
    coat = 0.0
    if KIND == "flake":
        vor = C.node(nt, "ShaderNodeTexVoronoi", Scale=55.0)
        nt.links.new(sco, vor.inputs["Vector"])
        cell = nt.nodes.new("ShaderNodeSeparateColor")
        nt.links.new(vor.outputs["Color"], cell.inputs["Color"])
        # Each flake its own tilt: brightness and a little bump from its random value.
        flake = C.ramp(nt, cell.outputs["Red"], 0.0, 1.0, 0.0, 1.0)
        mixc = nt.nodes.new("ShaderNodeMix")
        mixc.data_type = "RGBA"
        nt.links.new(flake, mixc.inputs["Factor"])
        mixc.inputs["A"].default_value = (*[v * 0.55 for v in base], 1)
        mixc.inputs["B"].default_value = (*[min(1.0, v * 1.9 + 0.04) for v in base], 1)
        colour = mixc.outputs["Result"]
        metal = 0.75
        rough = 0.32
        bump_height = C.math_node(nt, "MULTIPLY", cell.outputs["Green"], 0.25)
        coat = 1.0
    elif KIND == "sand":
        grain = C.noise(nt, sco, 260.0, detail=2.0, rough=0.6)
        bump_height = C.ramp(nt, grain, 0.35, 0.75, 0.0, 1.0)
        rough = 0.5
        metal = 0.0
        coat = 0.15
    elif KIND == "wrinkle":
        w1 = C.noise(nt, sco, 38.0, detail=8.0, rough=0.7)
        ridge = C.math_node(nt, "ABSOLUTE", C.math_node(nt, "SUBTRACT", w1, 0.5))
        bump_height = C.ramp(nt, ridge, 0.0, 0.18, 1.0, 0.0)
        rough = 0.5
        metal = 0.05
        coat = 0.25
    elif KIND == "gloss":
        peel = C.noise(nt, sco, 18.0, detail=3.0, rough=0.5)
        bump_height = C.math_node(nt, "MULTIPLY", peel, 0.15)
        rough = 0.22
        metal = 0.0
        coat = 0.9
    else:  # hammer
        vor = C.node(nt, "ShaderNodeTexVoronoi", Scale=11.0)
        vor.feature = "SMOOTH_F1"
        vor.inputs["Smoothness"].default_value = 0.6
        nt.links.new(sco, vor.inputs["Vector"])
        bump_height = C.ramp(nt, vor.outputs["Distance"], 0.0, 0.5, 0.0, 1.0)
        rough = 0.3
        metal = 0.55
        coat = 0.3

    # ----- wear: the paint chipped back to aluminium along the rounded edge, and scuffs.
    normal = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(geo.outputs["Normal"], normal.inputs["Vector"])
    slope = C.math_node(nt, "SUBTRACT", 1.0, normal.outputs["Z"])
    # On the rounded edge (the normal tilted 15-75 degrees), where noise says so.
    edge = C.ramp(nt, slope, 0.04, 0.4, 0.0, 1.0)
    chips = C.noise(nt, sco, 7.0, detail=10.0, rough=0.72)
    chipped = C.math_node(nt, "MULTIPLY", C.ramp(nt, chips, 0.66 - 0.05 * WEAR, 0.69 - 0.05 * WEAR, 0.0, 1.0), edge)
    # Boot scuffs on the top round the footswitch: streaks across, faint.
    streak = C.noise(nt, sco, 2.2, sx=0.5, sy=7.0, sz=1.0, detail=6.0, rough=0.6)
    position = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(co, position.inputs["Vector"])
    lower = C.ramp(nt, position.outputs["Y"], -5.0, -0.5, 1.0, 0.0)
    patch = C.ramp(nt, C.noise(nt, sco, 0.9, detail=3.0), 0.42, 0.62, 0.0, 1.0)
    scuff = C.math_node(nt, "MULTIPLY", C.ramp(nt, streak, 0.56, 0.7, 0.0, 0.5 * WEAR),
                        C.math_node(nt, "MULTIPLY", lower, patch))

    alu = (0.62, 0.62, 0.6)
    if colour is None:
        mixb = nt.nodes.new("ShaderNodeMix")
        mixb.data_type = "RGBA"
        nt.links.new(C.noise(nt, sco, 3.0, detail=4.0), mixb.inputs["Factor"])
        mixb.inputs["A"].default_value = (*[v * 0.94 for v in base], 1)
        mixb.inputs["B"].default_value = (*[min(1.0, v * 1.06) for v in base], 1)
        colour = mixb.outputs["Result"]
    # Scuffs lighten the paint toward a dusty grey.
    scuffed = nt.nodes.new("ShaderNodeMix")
    scuffed.data_type = "RGBA"
    nt.links.new(scuff, scuffed.inputs["Factor"])
    nt.links.new(colour, scuffed.inputs["A"])
    scuffed.inputs["B"].default_value = (*[min(1.0, v * 0.6 + 0.2) for v in base], 1)
    worn = nt.nodes.new("ShaderNodeMix")
    worn.data_type = "RGBA"
    nt.links.new(chipped, worn.inputs["Factor"])
    nt.links.new(scuffed.outputs["Result"], worn.inputs["A"])
    worn.inputs["B"].default_value = (*alu, 1)
    nt.links.new(worn.outputs["Result"], p.inputs["Base Color"])

    mm = C.math_node(nt, "ADD", C.math_node(nt, "MULTIPLY", chipped, 1.0 - metal), metal)
    nt.links.new(mm, p.inputs["Metallic"])
    rr = C.math_node(nt, "ADD", C.math_node(nt, "MULTIPLY", scuff, 0.25), rough)
    rr = C.math_node(nt, "ADD", C.math_node(nt, "MULTIPLY", chipped, 0.1), rr)
    nt.links.new(rr, p.inputs["Roughness"])
    p.inputs["Coat Weight"].default_value = coat
    p.inputs["Coat Roughness"].default_value = 0.06
    cw = C.math_node(nt, "MULTIPLY", C.math_node(nt, "SUBTRACT", 1.0, C.math_node(nt, "MAXIMUM", chipped, scuff)),
                     coat)
    nt.links.new(cw, p.inputs["Coat Weight"])

    if bump_height is not None:
        bump = C.node(nt, "ShaderNodeBump", Strength=0.35, Distance=0.02)
        nt.links.new(C.math_node(nt, "MULTIPLY", bump_height,
                                 C.math_node(nt, "SUBTRACT", 1.0, chipped)), bump.inputs["Height"])
        nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    return m


def carpet_material():
    """A pedalboard's loop carpet: near-black fibres, matte, a fine woven bump."""
    m = C.material("Carpet", C.lin("#1B1B1D"), rough=0.92)
    nt = m.node_tree
    p = nt.nodes["Principled BSDF"]
    co = nt.nodes.new("ShaderNodeTexCoord").outputs["Object"]
    loops = C.noise(nt, co, 140.0, detail=3.0, rough=0.7)
    fuzz = C.noise(nt, co, 22.0, detail=6.0, rough=0.6)
    tone = nt.nodes.new("ShaderNodeMix")
    tone.data_type = "RGBA"
    nt.links.new(C.ramp(nt, C.math_node(nt, "MULTIPLY", loops, fuzz), 0.1, 0.45, 0.0, 1.0), tone.inputs["Factor"])
    tone.inputs["A"].default_value = (*C.lin("#0C0C0E"), 1)
    tone.inputs["B"].default_value = (*C.lin("#232326"), 1)
    nt.links.new(tone.outputs["Result"], p.inputs["Base Color"])
    bump = C.node(nt, "ShaderNodeBump", Strength=0.6, Distance=0.05)
    nt.links.new(loops, bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], p.inputs["Normal"])
    p.inputs["Sheen Weight"].default_value = 0.3
    return m


enclosure(finish_material())
bpy.ops.mesh.primitive_plane_add(size=60, location=(0, 0, 0))
bpy.context.active_object.data.materials.append(carpet_material())

C.camera_ortho(scene, FACE_W, z=30.0)
scene.camera.data.sensor_fit = "HORIZONTAL"
C.key_sun(scene, energy=2.6, angle=24.0)
C.area(scene, "Fill", 16.0, 450, (0, -2.0, 18.0), (0, 0, 0), glossy=False)
C.area(scene, "Sheen", 10.0, 1600, (0, 9.0, 9.0), (0, 1.0, 0), shadow=False, diffuse=False)
C.room(scene, strength=0.55)
C.render(scene, OUT, round(FACE_W * PX), round(FACE_H * PX), samples=256, transparent=False, denoise=True)
