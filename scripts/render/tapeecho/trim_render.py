"""Blender (4.2) script: the aluminium trim between the cream and sage panels, the
whole width of the unit in one image. It is long (23 units), so the camera is
orthographic: the same as looking straight down at every point along it, as
the knobs and toggles are each seen. The key is the knobs' key made a distant
sun, so the whole length is lit alike; the room is in its reflections only.

The bar: an extruded, crowned, round-edged aluminium strip, brushed along its
length; long cleaning scratches, a few across, small dents, dull oxidised
patches, grime where it meets the panels. It casts its shadow onto the sage.

At 150 px per unit: 3480 px across (1160 on the page) by 60 px (20 on the page);
the bar is 24 px (8 on the page) and starts 6 px from the top.

Run: blender --background --factory-startup --python trim_render.py -- <out.png>
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

OUT = sys.argv[sys.argv.index("--") + 1]
ROOM = os.environ.get("NORTHLIGHT_HDRI", r"H:\Tools\hdri\lythwood_lounge_2k.hdr")  # Poly Haven, CC0
PX = 150
LENGTH = 1160 / 50                 # the page's 50 px per unit
HALF_W, HEIGHT = 0.08, 0.055       # the bar: 0.16 across (8 page px), this tall
TOP_MARGIN, FRAME_H = 6 / PX, 60 / PX

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene


def node(nt, kind, **inputs):
    n = nt.nodes.new(kind)
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


def link(nt, a, b):
    nt.links.new(a, b)


# ----- the bar: a crowned profile with rounded edges, extruded along X -----------------------------
profile = []
for i in range(41):
    t = math.pi * i / 40
    c, s = math.cos(t), math.sin(t)
    profile.append((HALF_W * math.copysign(abs(c) ** 0.6, c), HEIGHT * abs(s) ** 0.6))   # rounded shoulders
bm = bmesh.new()
ends = []
for x in (-LENGTH / 2 - 0.5, LENGTH / 2 + 0.5):
    ends.append([bm.verts.new((x, y, z)) for y, z in profile])
for i in range(len(profile) - 1):
    bm.faces.new((ends[0][i], ends[0][i + 1], ends[1][i + 1], ends[1][i]))
bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
me = bpy.data.meshes.new("Trim")
bm.to_mesh(me)
bm.free()
bar = bpy.data.objects.new("Trim", me)
scene.collection.objects.link(bar)
for p in bar.data.polygons:
    p.use_smooth = True
# Enough loops along it for the dents to have somewhere to be.
sub = bar.modifiers.new("subdiv", "SUBSURF")
sub.subdivision_type = "SIMPLE"
sub.levels = sub.render_levels = 0

# ----- the aluminium ---------------------------------------------------------------------------
alu = bpy.data.materials.new("Aluminium")
alu.use_nodes = True
nt = alu.node_tree
out = nt.nodes["Material Output"]
p = nt.nodes["Principled BSDF"]
p.inputs["Metallic"].default_value = 1.0
p.inputs["Anisotropic"].default_value = 0.75
along = node(nt, "ShaderNodeCombineXYZ", X=1.0, Y=0.0, Z=0.0)   # the brushing runs along the bar
link(nt, along.outputs["Vector"], p.inputs["Tangent"])
coord = nt.nodes.new("ShaderNodeTexCoord")


def stretched(sx, sy, angle=0.0):
    m = nt.nodes.new("ShaderNodeMapping")
    m.inputs["Rotation"].default_value = (0, 0, math.radians(angle))
    m.inputs["Scale"].default_value = (sx, sy, 1.0)
    link(nt, coord.outputs["Object"], m.inputs["Vector"])
    return m


# Brushing: fine streaks along the length.
brush = node(nt, "ShaderNodeTexNoise", Scale=1.0, Detail=12.0, Roughness=0.7)
link(nt, stretched(4.0, 900.0).outputs["Vector"], brush.inputs["Vector"])
# Long cleaning scratches, and a few across.
long_s = node(nt, "ShaderNodeTexNoise", Scale=1.0, Detail=4.0, Roughness=0.5)
link(nt, stretched(1.2, 420.0, 0.4).outputs["Vector"], long_s.inputs["Vector"])
long_r = node(nt, "ShaderNodeMapRange", **{"From Min": 0.66, "From Max": 0.72, "To Min": 0.0, "To Max": 1.0})
link(nt, long_s.outputs["Fac"], long_r.inputs["Value"])
cross_s = node(nt, "ShaderNodeTexNoise", Scale=1.0, Detail=3.0, Roughness=0.5)
link(nt, stretched(260.0, 3.0, 25.0).outputs["Vector"], cross_s.inputs["Vector"])
cross_r = node(nt, "ShaderNodeMapRange", **{"From Min": 0.72, "From Max": 0.76, "To Min": 0.0, "To Max": 0.8})
link(nt, cross_s.outputs["Fac"], cross_r.inputs["Value"])
scratches = node(nt, "ShaderNodeMath")
scratches.operation = "MAXIMUM"
link(nt, long_r.outputs["Result"], scratches.inputs[0])
link(nt, cross_r.outputs["Result"], scratches.inputs[1])
# Dents: small round dings here and there.
dents = nt.nodes.new("ShaderNodeTexVoronoi")
dents.feature = "F1"
dents.inputs["Scale"].default_value = 6.0
link(nt, stretched(1.0, 1.0).outputs["Vector"], dents.inputs["Vector"])
dent_r = node(nt, "ShaderNodeMapRange", **{"From Min": 0.0, "From Max": 0.06, "To Min": -1.0, "To Max": 0.0})
link(nt, dents.outputs["Distance"], dent_r.inputs["Value"])
# The surface: brushing + scratches cut in + dents pressed in.
h1 = node(nt, "ShaderNodeMath"); h1.operation = "MULTIPLY_ADD"
link(nt, scratches.outputs["Value"], h1.inputs[0]); h1.inputs[1].default_value = -0.6
link(nt, brush.outputs["Fac"], h1.inputs[2])
h2 = node(nt, "ShaderNodeMath"); h2.operation = "MULTIPLY_ADD"
link(nt, dent_r.outputs["Result"], h2.inputs[0]); h2.inputs[1].default_value = 1.5
link(nt, h1.outputs["Value"], h2.inputs[2])
bump = node(nt, "ShaderNodeBump", Strength=0.25, Distance=0.003)
link(nt, h2.outputs["Value"], bump.inputs["Height"])
link(nt, bump.outputs["Normal"], p.inputs["Normal"])
# Oxidised patches: duller, a touch darker and warmer.
ox = node(nt, "ShaderNodeTexNoise", Scale=1.0, Detail=8.0, Roughness=0.6)
link(nt, stretched(0.8, 5.0).outputs["Vector"], ox.inputs["Vector"])
ox_r = node(nt, "ShaderNodeMapRange", **{"From Min": 0.48, "From Max": 0.66, "To Min": 0.0, "To Max": 1.0})
link(nt, ox.outputs["Fac"], ox_r.inputs["Value"])
rough = node(nt, "ShaderNodeMapRange", **{"From Min": 0.0, "From Max": 1.0, "To Min": 0.2, "To Max": 0.48})
link(nt, ox_r.outputs["Result"], rough.inputs["Value"])
rough2 = node(nt, "ShaderNodeMath"); rough2.operation = "MULTIPLY_ADD"; rough2.use_clamp = True
link(nt, scratches.outputs["Value"], rough2.inputs[0]); rough2.inputs[1].default_value = 0.15
link(nt, rough.outputs["Result"], rough2.inputs[2])
link(nt, rough2.outputs["Value"], p.inputs["Roughness"])
colour = nt.nodes.new("ShaderNodeMix")
colour.data_type = "RGBA"
colour.inputs["A"].default_value = (0.86, 0.86, 0.85, 1)
colour.inputs["B"].default_value = (0.66, 0.64, 0.60, 1)
link(nt, ox_r.outputs["Result"], colour.inputs["Factor"])
# Grime where the bar meets the panels (the shader's occlusion), and in patches.
ao = node(nt, "ShaderNodeAmbientOcclusion", Distance=0.04)
ao.samples = 16
grime = node(nt, "ShaderNodeMapRange", **{"From Min": 0.3, "From Max": 0.9, "To Min": 0.85, "To Max": 0.0})
link(nt, ao.outputs["AO"], grime.inputs["Value"])
dirty = nt.nodes.new("ShaderNodeMix")
dirty.data_type = "RGBA"
dirty.inputs["B"].default_value = (0.12, 0.10, 0.08, 1)
link(nt, colour.outputs["Result"], dirty.inputs["A"])
link(nt, grime.outputs["Result"], dirty.inputs["Factor"])
link(nt, dirty.outputs["Result"], p.inputs["Base Color"])
bar.data.materials.append(alu)

# ----- the panels: catch the shadow, bounce their colour, otherwise transparent ----------------------
def panel(name, y0, y1, colour):
    bpy.ops.mesh.primitive_plane_add(size=1, location=(0, (y0 + y1) / 2, 0))
    ob = bpy.context.active_object
    ob.scale = (LENGTH + 4, abs(y1 - y0), 1)
    ob.is_shadow_catcher = True
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*colour, 1)
    m.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.45
    ob.data.materials.append(m)


panel("Cream", 0.0, 6.0, (0.877, 0.797, 0.576))
panel("Sage", -6.0, 0.0, (0.38, 0.527, 0.393))

# ----- camera: orthographic, the whole width ------------------------------------------------------
cam_data = bpy.data.cameras.new("Camera")
cam_data.type = "ORTHO"
cam_data.sensor_fit = "HORIZONTAL"
cam_data.ortho_scale = LENGTH
cam = bpy.data.objects.new("Camera", cam_data)
top = HALF_W + TOP_MARGIN
cam.location = (0, top - FRAME_H / 2, 6)
scene.collection.objects.link(cam)
scene.camera = cam

# ----- light: the knobs' key as a sun from the top of the unit, the room in reflections -----------
sun_data = bpy.data.lights.new("Key", "SUN")
sun_data.energy = 2.3            # the irradiance the knobs' 200 W key gives at the knob
sun_data.angle = math.radians(30)  # as wide as the knobs' key looks from the knob
sun = bpy.data.objects.new("Key", sun_data)
sun.rotation_mode = "QUATERNION"
sun.rotation_quaternion = (Vector((0, 0, 0.3)) - Vector((0, 3.4, 4.0))).to_track_quat("-Z", "Y")
scene.collection.objects.link(sun)
sun.visible_glossy = False   # its reflection made a hard white line along the top edge

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
wc = wn.nodes.new("ShaderNodeTexCoord")
wm = wn.nodes.new("ShaderNodeMapping")
wm.inputs["Rotation"].default_value = (0.0, 0.0, bright_az - math.pi / 2)
env = wn.nodes.new("ShaderNodeTexEnvironment")
env.image = image
wn.links.new(wc.outputs["Generated"], wm.inputs["Vector"])
wn.links.new(wm.outputs["Vector"], env.inputs["Vector"])
hue = wn.nodes.new("ShaderNodeHueSaturation")
hue.inputs["Saturation"].default_value = 0.3
wn.links.new(env.outputs["Color"], hue.inputs["Color"])
bg = wn.nodes["Background"]
bg.inputs["Strength"].default_value = 1.1
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
scene.cycles.use_denoising = False      # the denoiser would smooth the brushing away
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
scene.render.resolution_x = round(LENGTH * PX)
scene.render.resolution_y = round(FRAME_H * PX)
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "Standard"
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("rendered trim", OUT)
