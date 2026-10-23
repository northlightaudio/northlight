"""Blender (4.2) script: the bezel round the tape window, the VU meter's bezel made
the window's size. Its geometry helpers, painted metal, edge wear and screws are
taken from vu_render.py itself, so the two bezels stay the same.

The window on the page is 642 x 212 px at 50 px per unit: 12.84 x 4.24 units. The
opening (inset FRAME) is left empty: the page draws the tape under it, and the
bezel's shadow falls onto it (a shadow-catching panel runs under the opening too).
Perspective down the bezel's axis from 20 units, so its inner walls show a little,
about as much as the meter's; lit by the knobs' key as a distant sun (the bezel is
too long for one lamp to light evenly) and the room in its reflections.

Image: 150 px per unit, margins 0.4 above and at the sides, 0.9 below for the
shadow: 2046 x 831 px; the bezel's centre lands at pixel (1023, 378).

Run: blender --background --factory-startup --python bezel_render.py -- <out.png>
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

OUT = sys.argv[sys.argv.index("--") + 1]
HERE = os.path.dirname(os.path.abspath(__file__))
ROOM = os.environ.get("NORTHLIGHT_HDRI", r"H:\Tools\hdri\lythwood_lounge_2k.hdr")  # Poly Haven, CC0
PX = 150
DIST = 20.0
MARGIN, MARGIN_BELOW = 0.4, 0.9

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# The meter's helpers and materials, run here.
src = open(os.path.join(HERE, "vu_render.py"), encoding="utf-8").read()
shared = src[src.index("# ----- geometry helpers"):src.index("liner_mat = plain(")]
screw_src = src[src.index("def screw("):src.index("parts = [bezel_ob")]
exec(shared + "\nnickel = plain('Nickel', (0.70, 0.68, 0.64), metallic=1.0, rough=0.38)\n"
     "slot_mat = plain('Slot', (0.02, 0.02, 0.02), rough=0.8)\n" + screw_src)

W, H, R = 642 / 50, 212 / 50, 0.11     # the window's bezel (rrect reads these)

# ----- the bezel: the meter's profile, down to the panel ----------------------------------------
profile = [(0.00, 0.00), (0.00, 0.16), (0.035, 0.21), (FRAME - 0.04, 0.21), (FRAME, 0.17), (FRAME, -0.03)]
bm = bmesh.new()
rings = [[bm.verts.new((x, y, z)) for x, y in rrect(d)] for d, z in profile]
bridge(bm, rings)
bezel_ob = link("Bezel", bm, bezel_mat)
bevel(bezel_ob, 0.012, 3)
smooth_by_angle(bezel_ob)
# (No corner screws: on a bezel this slim they made no sense.)

# The panel, and the tape window's face under the opening: catch the shadow, otherwise clear.
bpy.ops.mesh.primitive_plane_add(size=1, location=(0, 0, 0))
floor = bpy.context.active_object
floor.scale = (W + 6, H + 6, 1)
floor.is_shadow_catcher = True
floor.data.materials.append(plain("CreamEnamel", (0.877, 0.797, 0.576), rough=0.45))

# ----- camera ------------------------------------------------------------------------------------
frame_w = W + 2 * MARGIN
frame_h = H + MARGIN + MARGIN_BELOW
cam_data = bpy.data.cameras.new("Camera")
cam_data.type = "PERSP"
cam_data.sensor_fit = "HORIZONTAL"
cam_data.sensor_width = 36
cam_data.lens = DIST * 36 / frame_w
cam_data.clip_end = 200
cam_data.shift_y = ((MARGIN - MARGIN_BELOW) / 2) / frame_w
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
px = image.pixels[:]
best, best_u = -1.0, 0.5
for y in range(ih // 2, ih, 4):
    for x in range(0, iw, 4):
        i = (y * iw + x) * 4
        lum = 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2]
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
scene.cycles.samples = 768
scene.cycles.use_denoising = True
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
scene.render.film_transparent = True
scene.render.resolution_x = round(frame_w * PX)
scene.render.resolution_y = round(frame_h * PX)
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "Standard"
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("rendered bezel", OUT, scene.render.resolution_x, scene.render.resolution_y)
