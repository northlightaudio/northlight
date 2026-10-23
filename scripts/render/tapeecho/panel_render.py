"""Blender (4.2) script: the front panel, one enamelled steel plate the size of the board
(1160 x 600 page px, 23.2 x 12 units at 50 px per unit), cream above the trim and sage
below, in the same satin enamel. Lit as every part is (the top key as a distant sun, a
soft fall-off from the top of the unit, the room in its sheen); worn where the wear map
says (panel_wear.py): rubbed and scratched round the controls, grimy at the edges and the
seam, chipped to primer with a little rust at the corners and edges.

The camera looks straight at the plate from 30 units, so the room's sheen drifts across
it as it would; a flat plate square to the camera keeps its shape exactly.

Run: blender --background --factory-startup --python panel_render.py -- <wear.png> <out.png> [px_per_unit]
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
WEAR, OUT = argv[0], argv[1]
PX = float(argv[2]) if len(argv) > 2 else 100.0
ROOM = os.environ.get("NORTHLIGHT_HDRI", r"H:\Tools\hdri\lythwood_lounge_2k.hdr")  # Poly Haven, CC0
BW, BH = 1160 / 50, 600 / 50
SEAM_Y = BH / 2 - 264 / 50             # the trim's centre line
DIST = 30.0

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene


def node(nt, kind, **inputs):
    n = nt.nodes.new(kind)
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


wear_img = bpy.data.images.load(WEAR)
wear_img.colorspace_settings.name = "Non-Color"


def enamel(name, colour):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    L = nt.links.new
    p = nt.nodes["Principled BSDF"]
    uv = nt.nodes.new("ShaderNodeTexCoord")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = wear_img
    tex.interpolation = "Cubic"
    L(uv.outputs["UV"], tex.inputs["Vector"])
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    L(tex.outputs["Color"], sep.inputs["Color"])
    rub, grime, chip, rust = sep.outputs["Red"], sep.outputs["Green"], sep.outputs["Blue"], tex.outputs["Alpha"]

    # The paint: its colour a little uneven, as sprayed enamel is, and faded in patches.
    mott = node(nt, "ShaderNodeTexNoise", Scale=0.35, Detail=4.0, Roughness=0.55)
    L(uv.outputs["Object"], mott.inputs["Vector"])
    mr = node(nt, "ShaderNodeMapRange", **{"From Min": 0.3, "From Max": 0.7, "To Min": 0.95, "To Max": 1.04})
    L(mott.outputs["Fac"], mr.inputs["Value"])
    paint = nt.nodes.new("ShaderNodeMix"); paint.data_type = "RGBA"; paint.blend_type = "MULTIPLY"
    paint.inputs["Factor"].default_value = 1.0
    paint.inputs["A"].default_value = (*colour, 1)
    cm = nt.nodes.new("ShaderNodeCombineColor")
    for ch in ("Red", "Green", "Blue"):
        L(mr.outputs["Result"], cm.inputs[ch])
    L(cm.outputs["Color"], paint.inputs["B"])
    # Rubbed: a touch lighter where fingers have polished it.
    rubbed = nt.nodes.new("ShaderNodeMix"); rubbed.data_type = "RGBA"
    rubbed.inputs["B"].default_value = (*(min(1.0, c * 1.04 + 0.01) for c in colour), 1)
    L(paint.outputs["Result"], rubbed.inputs["A"])
    rf = node(nt, "ShaderNodeMath"); rf.operation = "MULTIPLY"; rf.inputs[1].default_value = 0.6
    L(rub, rf.inputs[0])
    L(rf.outputs["Value"], rubbed.inputs["Factor"])
    # Grime, then chips to primer, then rust round them.
    dirty = nt.nodes.new("ShaderNodeMix"); dirty.data_type = "RGBA"
    dirty.inputs["B"].default_value = (0.16, 0.125, 0.085, 1)
    L(rubbed.outputs["Result"], dirty.inputs["A"])
    gf = node(nt, "ShaderNodeMath"); gf.operation = "MULTIPLY"; gf.inputs[1].default_value = 0.55
    L(grime, gf.inputs[0])
    L(gf.outputs["Value"], dirty.inputs["Factor"])
    rusty = nt.nodes.new("ShaderNodeMix"); rusty.data_type = "RGBA"
    rusty.inputs["B"].default_value = (0.32, 0.12, 0.045, 1)
    L(dirty.outputs["Result"], rusty.inputs["A"])
    rsf = node(nt, "ShaderNodeMath"); rsf.operation = "MULTIPLY"; rsf.inputs[1].default_value = 0.45
    L(rust, rsf.inputs[0])
    L(rsf.outputs["Value"], rusty.inputs["Factor"])
    primer = nt.nodes.new("ShaderNodeMix"); primer.data_type = "RGBA"
    primer.inputs["B"].default_value = (0.36, 0.35, 0.33, 1)         # grey primer, steel showing through
    L(rusty.outputs["Result"], primer.inputs["A"])
    L(chip, primer.inputs["Factor"])
    L(primer.outputs["Result"], p.inputs["Base Color"])
    # Satin: a little gloss, more where rubbed, none in the grime or the chips.
    r0 = node(nt, "ShaderNodeMath"); r0.operation = "MULTIPLY_ADD"; r0.use_clamp = True
    L(rub, r0.inputs[0]); r0.inputs[1].default_value = -0.14; r0.inputs[2].default_value = 0.34
    r1 = node(nt, "ShaderNodeMath"); r1.operation = "MULTIPLY_ADD"; r1.use_clamp = True
    L(grime, r1.inputs[0]); r1.inputs[1].default_value = 0.3; L(r0.outputs["Value"], r1.inputs[2])
    r2 = node(nt, "ShaderNodeMath"); r2.operation = "MAXIMUM"
    L(r1.outputs["Value"], r2.inputs[0])
    cr = node(nt, "ShaderNodeMath"); cr.operation = "MULTIPLY"; cr.inputs[1].default_value = 0.65
    L(chip, cr.inputs[0]); L(cr.outputs["Value"], r2.inputs[1])
    L(r2.outputs["Value"], p.inputs["Roughness"])
    # The surface: fine orange peel, and the chips sunk below the paint.
    peel = node(nt, "ShaderNodeTexNoise", Scale=26.0, Detail=3.0, Roughness=0.5)
    L(uv.outputs["Object"], peel.inputs["Vector"])
    h = node(nt, "ShaderNodeMath"); h.operation = "MULTIPLY_ADD"
    L(chip, h.inputs[0]); h.inputs[1].default_value = -1.2; L(peel.outputs["Fac"], h.inputs[2])
    bump = node(nt, "ShaderNodeBump", Strength=0.06, Distance=0.004)
    L(h.outputs["Value"], bump.inputs["Height"])
    L(bump.outputs["Normal"], p.inputs["Normal"])
    return m


cream = enamel("CreamEnamel", (0.877, 0.797, 0.576))    # the colours the parts' shadows were caught on
sage = enamel("SageEnamel", (0.41, 0.57, 0.425))

# ----- the plate: two faces split at the seam, UVs over the whole board --------------------------
bm = bmesh.new()
uvl = bm.loops.layers.uv.new("UVMap")
xs = (-BW / 2, BW / 2)
ys = (-BH / 2, SEAM_Y, BH / 2)
for (y0, y1), mi in (((ys[0], ys[1]), 1), ((ys[1], ys[2]), 0)):
    vs = [bm.verts.new((x, y, 0)) for x, y in ((xs[0], y0), (xs[1], y0), (xs[1], y1), (xs[0], y1))]
    f = bm.faces.new(vs)
    f.material_index = mi
    for loop in f.loops:
        x, y, _ = loop.vert.co
        loop[uvl].uv = ((x + BW / 2) / BW, (y + BH / 2) / BH)
me = bpy.data.meshes.new("Panel")
bm.to_mesh(me)
bm.free()
plate = bpy.data.objects.new("Panel", me)
scene.collection.objects.link(plate)
plate.data.materials.append(cream)
plate.data.materials.append(sage)

# ----- camera ------------------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("Camera")
cam_data.type = "PERSP"
cam_data.sensor_fit = "HORIZONTAL"
cam_data.sensor_width = 36
cam_data.lens = DIST * 36 / BW
cam_data.clip_end = 200
cam = bpy.data.objects.new("Camera", cam_data)
cam.location = (0, 0, DIST)
scene.collection.objects.link(cam)
scene.camera = cam

# ----- light: the top key as a sun, a soft fall-off from the top, the room in the sheen ---------
sun_data = bpy.data.lights.new("Key", "SUN")
sun_data.energy = 2.3
sun_data.angle = math.radians(30)
sun = bpy.data.objects.new("Key", sun_data)
sun.rotation_mode = "QUATERNION"
sun.rotation_quaternion = (Vector((0, 0, 0.3)) - Vector((0, 3.4, 4.0))).to_track_quat("-Z", "Y")
scene.collection.objects.link(sun)
fall = bpy.data.lights.new("TopFall", "AREA")
fall.shape = "RECTANGLE"
fall.size, fall.size_y = 30.0, 3.0
fall.energy = 900
fo = bpy.data.objects.new("TopFall", fall)
fo.location = (0, BH / 2 + 4, 7)
fo.rotation_mode = "QUATERNION"
fo.rotation_quaternion = (Vector((0, 0, 0)) - fo.location).to_track_quat("-Z", "Y")
scene.collection.objects.link(fo)
fo.visible_glossy = False

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
bg.inputs["Strength"].default_value = 0.8
wn.links.new(hue.outputs["Color"], bg.inputs["Color"])
dim = wn.nodes.new("ShaderNodeBackground")
dim.inputs["Strength"].default_value = 0.16
wn.links.new(hue.outputs["Color"], dim.inputs["Color"])
lp = wn.nodes.new("ShaderNodeLightPath")
mix = wn.nodes.new("ShaderNodeMixShader")
wn.links.new(lp.outputs["Is Glossy Ray"], mix.inputs["Fac"])
wn.links.new(dim.outputs["Background"], mix.inputs[1])
wn.links.new(bg.outputs["Background"], mix.inputs[2])
wn.links.new(mix.outputs["Shader"], wn.nodes["World Output"].inputs["Surface"])
scene.world = world

# ----- render ------------------------------------------------------------------------------------
scene.render.engine = "CYCLES"
scene.cycles.samples = 256
scene.cycles.use_denoising = False     # the denoiser would smooth the orange peel away
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
scene.render.resolution_x = round(BW * PX)
scene.render.resolution_y = round(BH * PX)
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGB"
scene.view_settings.view_transform = "Standard"
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("rendered panel", OUT, scene.render.resolution_x, scene.render.resolution_y)
