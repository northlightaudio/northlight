"""Blender (4.2) script: a jewelled pilot lamp on the cream panel. A rose-cut amber
glass lens in a thin ring of the bezels' satin aluminium (taken from vu_render.py,
so it matches), a small bulb in a reflector cup behind it. Seen as the knobs are:
perspective straight down its axis, the same key from the top of the unit, the
same room in its reflections.

  off  the bulb dark: the facets catch the room as dark amber glints.
  lit  the bulb at incandescent warmth, its light coming through the glass.

The ring is 0.76 units across, 38 px on the page at 50 px per unit. The image is
1.3 units wide, 0.65 above the lamp's centre and 0.85 below (room for its shadow),
at 300 px per unit: 390 x 450, the centre at pixel (195, 195).

Run: blender --background --factory-startup --python lamp_render.py -- <off|lit> <out.png>
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
STATE, OUT = argv[0], argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from render_config import hdri_path  # noqa: E402  (scripts/render: NORTHLIGHT_HDRI or render.local.json)
ROOM = hdri_path()  # Poly Haven, CC0
BULB = float(os.environ.get("LAMP_BULB", "1.2"))
PX, DIST = 300, 8.0
FRAME_W, ABOVE, BELOW = 1.3, 0.65, 0.85

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# The bezels' helpers and their aluminium, run here.
os.environ["BEZEL_FINISH"] = "alu"
src = open(os.path.join(HERE, "vu_render.py"), encoding="utf-8").read()
exec(src[src.index("# ----- geometry helpers"):src.index("liner_mat = plain(")])
ring_mat = bezel_mat


def revolve(name, profile, mat, segments=160, smooth=True):
    """A solid of revolution about Z from (r, z) points."""
    bm = bmesh.new()
    rings = []
    for k in range(segments):
        a = 2 * math.pi * k / segments
        rings.append([bm.verts.new((r * math.cos(a), r * math.sin(a), z)) for r, z in profile])
    n = len(profile)
    for k in range(segments):
        nxt = (k + 1) % segments
        for i in range(n - 1):
            bm.faces.new((rings[k][i], rings[nxt][i], rings[nxt][i + 1], rings[k][i + 1]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    ob = link(name, bm, mat)
    for p in ob.data.polygons:
        p.use_smooth = smooth
    return ob


# ----- the ring: thin, a rounded lip, its throat running down into the panel ---------------------
ring = revolve("Ring", [(0.255, -0.12), (0.255, 0.07), (0.265, 0.095), (0.29, 0.105), (0.33, 0.1),
                        (0.35, 0.08), (0.355, 0.045), (0.355, 0.0), (0.255, -0.12)], ring_mat)
bevel(ring, 0.006, 2)

# ----- the reflector cup and the bulb -------------------------------------------------------------
cup_mat = plain("Reflector", (0.32, 0.30, 0.27), metallic=1.0, rough=0.6)   # dull, tarnished
revolve("Cup", [(0.0, -0.2), (0.12, -0.19), (0.2, -0.14), (0.25, -0.06), (0.255, 0.0)], cup_mat)

bulb_mat = bpy.data.materials.new("Bulb")
bulb_mat.use_nodes = True
bnt = bulb_mat.node_tree
bp = bnt.nodes["Principled BSDF"]
bp.inputs["Base Color"].default_value = (0.9, 0.85, 0.75, 1)
bp.inputs["Transmission Weight"].default_value = 0.6
bp.inputs["Roughness"].default_value = 0.7        # a frosted envelope
if STATE == "lit":
    bb = bnt.nodes.new("ShaderNodeBlackbody")
    bb.inputs["Temperature"].default_value = 2300
    bnt.links.new(bb.outputs["Color"], bp.inputs["Emission Color"])
    bp.inputs["Emission Strength"].default_value = BULB
bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24, radius=0.13, location=(0, 0.03, -0.09))   # a touch off centre, as bulbs sit
bulb = bpy.context.active_object
bulb.data.materials.append(bulb_mat)
bpy.ops.object.shade_smooth()

# ----- the lens: rose-cut amber glass ------------------------------------------------------------
glass = bpy.data.materials.new("Amber")
glass.use_nodes = True
gp = glass.node_tree.nodes["Principled BSDF"]
gp.inputs["Base Color"].default_value = (1.0, 0.32, 0.035, 1) if os.environ.get("LAMP_GLASS", "amber") == "amber" else (1.0, 0.035, 0.02, 1)
gp.inputs["Transmission Weight"].default_value = 1.0
gp.inputs["IOR"].default_value = 1.52
gp.inputs["Roughness"].default_value = 0.03
# Fifty years of fingers: faintly hazed in patches.
haze = node(glass.node_tree, "ShaderNodeTexNoise", Scale=9.0, Detail=6.0)
hr = node(glass.node_tree, "ShaderNodeMapRange", **{"From Min": 0.4, "From Max": 0.7, "To Min": 0.06, "To Max": 0.16})
glass.node_tree.links.new(haze.outputs["Fac"], hr.inputs["Value"])
glass.node_tree.links.new(hr.outputs["Result"], gp.inputs["Roughness"])

N = 12
bm = bmesh.new()
girdle = [bm.verts.new((0.262 * math.cos(2 * math.pi * k / N), 0.262 * math.sin(2 * math.pi * k / N), 0.04)) for k in range(N)]
crown = [bm.verts.new((0.205 * math.cos(2 * math.pi * (k + 0.5) / N), 0.205 * math.sin(2 * math.pi * (k + 0.5) / N), 0.15)) for k in range(N)]
table = [bm.verts.new((0.11 * math.cos(2 * math.pi * k / N), 0.11 * math.sin(2 * math.pi * k / N), 0.205)) for k in range(N)]
base = [bm.verts.new((0.262 * math.cos(2 * math.pi * k / N), 0.262 * math.sin(2 * math.pi * k / N), -0.04)) for k in range(N)]
for k in range(N):
    j = (k + 1) % N
    bm.faces.new((girdle[k], girdle[j], crown[k]))          # the lower facets
    bm.faces.new((crown[k], girdle[j], crown[j]))
    bm.faces.new((crown[k], crown[j], table[j]))             # the upper facets
    bm.faces.new((crown[k], table[j], table[k]))
    bm.faces.new((base[k], base[j], girdle[j], girdle[k]))   # the girdle, down into the ring
bm.faces.new(table)
bm.faces.new(list(reversed(base)))
bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
lens = link("Lens", bm, glass)
for p in lens.data.polygons:
    p.use_smooth = False

# ----- the panel ---------------------------------------------------------------------------------
bm = bmesh.new()
hole = [bm.verts.new((0.3 * math.cos(2 * math.pi * k / 64), 0.3 * math.sin(2 * math.pi * k / 64), 0)) for k in range(64)]
edge = [bm.verts.new((8 * math.cos(2 * math.pi * k / 64), 8 * math.sin(2 * math.pi * k / 64), 0)) for k in range(64)]
for k in range(64):
    j = (k + 1) % 64
    bm.faces.new((hole[k], hole[j], edge[j], edge[k]))
bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
floor = link("Panel", bm, plain("CreamEnamel", (0.877, 0.797, 0.576), rough=0.45))
floor.is_shadow_catcher = True

# ----- camera ------------------------------------------------------------------------------------
frame_h = ABOVE + BELOW
cam_data = bpy.data.cameras.new("Camera")
cam_data.type = "PERSP"
cam_data.sensor_fit = "HORIZONTAL"
cam_data.sensor_width = 36
cam_data.lens = DIST * 36 / FRAME_W
cam_data.shift_y = ((ABOVE - BELOW) / 2) / FRAME_W
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
key.rotation_quaternion = (Vector((0, 0, 0.1)) - key.location).to_track_quat("-Z", "Y")
scene.collection.objects.link(key)

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
scene.cycles.samples = 1024
scene.cycles.use_denoising = True
scene.cycles.max_bounces = 24
scene.cycles.transmission_bounces = 24
scene.cycles.glossy_bounces = 12
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
scene.render.resolution_x = round(FRAME_W * PX)
scene.render.resolution_y = round(frame_h * PX)
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "Standard"
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("rendered lamp", STATE, OUT)
