"""Blender (4.2) script: a chrome bat-handle toggle switch, rendered straight down
(orthographic) and lit from the top of the unit as the knobs are, its shadow
caught on a transparent floor. One render per state, so each keeps the light
from the top (a mirrored image would light it from below).

The chrome reflects a photographed studio (a Poly Haven HDRI, turned so its
brightest light sits at the top of the unit), and its finish is worn: the polish
varies, with smudges and hairline scratches; the washer is spun like the knobs'
caps; the bushing is threaded.

Run: blender --background --factory-startup --python toggle_render.py -- <up|down> <out.png> [size] [hdri]
"""
import math
import os
import sys

import bpy
import bmesh
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
STATE, OUT = argv[0], argv[1]
SIZE = int(argv[2]) if len(argv) > 2 else 512
HDRI = argv[3] if len(argv) > 3 else os.environ.get("NORTHLIGHT_HDRI", r"H:\Tools\hdri\lythwood_lounge_2k.hdr")
TILT = {"up": 32.0, "centre": 0.0, "down": -32.0}[STATE]  # degrees from upright, toward the top of the unit (+Y) when up

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene


def node(nt, kind, **inputs):
    n = nt.nodes.new(kind)
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


def worn_metal(name, base, rough_min, rough_max, scratches=0.04, smudge_scale=3.0, spun=False):
    """A metal whose polish varies: broad smudges raise the roughness in places,
    fine hairline scratches break up the reflections. `spun`: grain running
    round the Z axis, as a spun or turned part has."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*base, 1.0)
    bsdf.inputs["Metallic"].default_value = 1.0
    coord = nt.nodes.new("ShaderNodeTexCoord")
    # Smudges: a soft, broad noise lifting the roughness.
    smudge = node(nt, "ShaderNodeTexNoise", Scale=smudge_scale, Detail=4.0, Roughness=0.55)
    nt.links.new(coord.outputs["Object"], smudge.inputs["Vector"])
    rough = node(nt, "ShaderNodeMapRange", **{"From Min": 0.35, "From Max": 0.75, "To Min": rough_min, "To Max": rough_max})
    nt.links.new(smudge.outputs["Fac"], rough.inputs["Value"])
    nt.links.new(rough.outputs["Result"], bsdf.inputs["Roughness"])
    # Hairline scratches: noise stretched hard along one axis, thresholded to thin lines.
    stretch = node(nt, "ShaderNodeMapping")
    stretch.inputs["Scale"].default_value = (1.0, 60.0, 1.0)
    stretch.inputs["Rotation"].default_value = (0.0, 0.0, 0.7)
    nt.links.new(coord.outputs["Object"], stretch.inputs["Vector"])
    lines = node(nt, "ShaderNodeTexNoise", Scale=9.0, Detail=2.0)
    nt.links.new(stretch.outputs["Vector"], lines.inputs["Vector"])
    thin = node(nt, "ShaderNodeMapRange", **{"From Min": 0.62, "From Max": 0.66, "To Min": 0.0, "To Max": 1.0})
    nt.links.new(lines.outputs["Fac"], thin.inputs["Value"])
    bump = node(nt, "ShaderNodeBump", Strength=scratches, Distance=0.002)
    nt.links.new(thin.outputs["Result"], bump.inputs["Height"])
    if spun:
        bsdf.inputs["Anisotropic"].default_value = 0.8
        tangent = nt.nodes.new("ShaderNodeTangent")
        tangent.direction_type = "RADIAL"
        tangent.axis = "Z"
        nt.links.new(tangent.outputs["Tangent"], bsdf.inputs["Tangent"])
        # Fine circular turning marks.
        sep = nt.nodes.new("ShaderNodeSeparateXYZ")
        nt.links.new(coord.outputs["Object"], sep.inputs["Vector"])
        xy = nt.nodes.new("ShaderNodeCombineXYZ")
        nt.links.new(sep.outputs["X"], xy.inputs["X"])
        nt.links.new(sep.outputs["Y"], xy.inputs["Y"])
        r = nt.nodes.new("ShaderNodeVectorMath")
        r.operation = "LENGTH"
        nt.links.new(xy.outputs["Vector"], r.inputs[0])
        rings = node(nt, "ShaderNodeTexNoise", Scale=1.0, Detail=5.0)
        rr = nt.nodes.new("ShaderNodeCombineXYZ")
        mult = nt.nodes.new("ShaderNodeMath")
        mult.operation = "MULTIPLY"
        mult.inputs[1].default_value = 700.0
        nt.links.new(r.outputs["Value"], mult.inputs[0])
        nt.links.new(mult.outputs["Value"], rr.inputs["X"])
        nt.links.new(rr.outputs["Vector"], rings.inputs["Vector"])
        turn = node(nt, "ShaderNodeBump", Strength=0.05, Distance=0.002)
        nt.links.new(rings.outputs["Fac"], turn.inputs["Height"])
        nt.links.new(turn.outputs["Normal"], bump.inputs["Normal"])
    nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
    age(nt, bsdf)
    return m


def age(nt, bsdf):
    out = nt.nodes["Material Output"]
    film = nt.nodes.new("ShaderNodeBsdfPrincipled")
    film.inputs["Base Color"].default_value = (0.11, 0.10, 0.085, 1)
    film.inputs["Roughness"].default_value = 0.85
    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.06
    ao.samples = 16
    crevice = nt.nodes.new("ShaderNodeMapRange")
    crevice.inputs["From Min"].default_value = 0.35
    crevice.inputs["From Max"].default_value = 0.95
    crevice.inputs["To Min"].default_value = 0.75
    crevice.inputs["To Max"].default_value = 0.0
    nt.links.new(ao.outputs["AO"], crevice.inputs["Value"])
    coord = nt.nodes.new("ShaderNodeTexCoord")
    patches = nt.nodes.new("ShaderNodeTexNoise")
    patches.inputs["Scale"].default_value = 7.0
    patches.inputs["Detail"].default_value = 6.0
    nt.links.new(coord.outputs["Object"], patches.inputs["Vector"])
    patch = nt.nodes.new("ShaderNodeMapRange")
    patch.inputs["From Min"].default_value = 0.5
    patch.inputs["From Max"].default_value = 0.75
    patch.inputs["To Min"].default_value = 0.0
    patch.inputs["To Max"].default_value = 0.3
    nt.links.new(patches.outputs["Fac"], patch.inputs["Value"])
    amount = nt.nodes.new("ShaderNodeMath")
    amount.operation = "ADD"
    amount.use_clamp = True
    nt.links.new(crevice.outputs["Result"], amount.inputs[0])
    nt.links.new(patch.outputs["Result"], amount.inputs[1])
    mix = nt.nodes.new("ShaderNodeMixShader")
    nt.links.new(amount.outputs["Value"], mix.inputs["Fac"])
    nt.links.new(bsdf.outputs["BSDF"], mix.inputs[1])
    nt.links.new(film.outputs["BSDF"], mix.inputs[2])
    nt.links.new(mix.outputs["Shader"], out.inputs["Surface"])


def threaded(name, base, pitch=0.022):
    """Chrome with screw threads: rings up the Z axis as a bump."""
    m = worn_metal(name, base, 0.28, 0.42, scratches=0.01)
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    coord = nt.nodes.new("ShaderNodeTexCoord")
    wave = nt.nodes.new("ShaderNodeTexWave")
    wave.wave_type = "BANDS"
    wave.bands_direction = "Z"
    wave.wave_profile = "SAW"
    wave.inputs["Scale"].default_value = 1.0 / pitch / 6.0
    nt.links.new(coord.outputs["Object"], wave.inputs["Vector"])
    thread = node(nt, "ShaderNodeBump", Strength=0.35, Distance=0.004)
    nt.links.new(wave.outputs["Fac"], thread.inputs["Height"])
    nt.links.new(thread.outputs["Normal"], bsdf.inputs["Normal"])
    return m


chrome = worn_metal("Chrome", (0.78, 0.76, 0.72), 0.22, 0.38, scratches=0.012)
nutmetal = worn_metal("Nut", (0.74, 0.72, 0.69), 0.3, 0.45, scratches=0.015, smudge_scale=5.0)
washer_metal = worn_metal("Washer", (0.66, 0.66, 0.64), 0.42, 0.58, scratches=0.01, spun=True)
bush_metal = threaded("Bushing", (0.74, 0.72, 0.69))
dark = bpy.data.materials.new("Throat")
dark.use_nodes = True
dark.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.02, 0.02, 0.02, 1)
dark.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.6


def bevel(ob, width, segments=4):
    b = ob.modifiers.new("bevel", "BEVEL")
    b.width = width
    b.segments = segments
    b.limit_method = "ANGLE"


def smooth(ob):
    for poly in ob.data.polygons:
        poly.use_smooth = True


# The washer, flat on the panel.
bpy.ops.mesh.primitive_cylinder_add(vertices=160, radius=0.40, depth=0.025, location=(0, 0, 0.0125))
washer = bpy.context.active_object
washer.data.materials.append(washer_metal)
bevel(washer, 0.008)
smooth(washer)

# The hex nut: 0.5 across the flats, chamfered top and bottom.
bpy.ops.mesh.primitive_cylinder_add(vertices=6, radius=0.5 / math.sqrt(3), depth=0.12, location=(0, 0, 0.085))
nut = bpy.context.active_object
nut.rotation_euler = (0, 0, math.radians(30))
nut.data.materials.append(nutmetal)
bevel(nut, 0.022, 4)

# The threaded bushing standing through the nut, its mouth dark.
bpy.ops.mesh.primitive_cylinder_add(vertices=128, radius=0.155, depth=0.19, location=(0, 0, 0.24))
bushing = bpy.context.active_object
bushing.data.materials.append(bush_metal)
bevel(bushing, 0.012)
smooth(bushing)
bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=0.105, depth=0.02, location=(0, 0, 0.33))
throat = bpy.context.active_object
throat.data.materials.append(dark)

# The lever: a slim nickel shaft from the bushing's mouth, and on it a black
# bakelite tip, eight-sided, flaring toward a chamfered faceted end.
pivot = Vector((0, 0, 0.31))
bpy.ops.mesh.primitive_cylinder_add(vertices=64, radius=0.036, depth=0.26, location=(0, 0, 0.44))
lever = bpy.context.active_object
lever.data.materials.append(chrome)
bevel(lever, 0.006)
smooth(lever)

bakelite = bpy.data.materials.new("Bakelite")
bakelite.use_nodes = True
bk = bakelite.node_tree
bkp = bk.nodes["Principled BSDF"]
tint = node(bk, "ShaderNodeTexNoise", Scale=9.0, Detail=3.0)
tint_ramp = bk.nodes.new("ShaderNodeValToRGB")
tint_ramp.color_ramp.elements[0].color = (0.66, 0.54, 0.34, 1)   # yellowed
tint_ramp.color_ramp.elements[1].color = (0.76, 0.66, 0.46, 1)   # cream
bk.links.new(tint.outputs["Fac"], tint_ramp.inputs["Fac"])
bk.links.new(tint_ramp.outputs["Color"], bkp.inputs["Base Color"])
bkp.inputs["Subsurface Weight"].default_value = 0.15
bkp.inputs["Subsurface Radius"].default_value = (0.02, 0.012, 0.006)
bkp.inputs["Coat Weight"].default_value = 0.0
bkp.inputs["Specular IOR Level"].default_value = 0.45
bkp.inputs["Coat Roughness"].default_value = 0.3
# Handled for fifty years: the gloss rubbed uneven.
wear = node(bk, "ShaderNodeTexNoise", Scale=6.0, Detail=2.0)
wear_r = node(bk, "ShaderNodeMapRange", **{"From Min": 0.35, "From Max": 0.7, "To Min": 0.24, "To Max": 0.3})
bk.links.new(wear.outputs["Fac"], wear_r.inputs["Value"])
bk.links.new(wear_r.outputs["Result"], bkp.inputs["Roughness"])

SIDES = 8
profile = [(0.52, 0.055), (0.60, 0.072), (0.84, 0.105), (0.92, 0.076)]  # (z, radius) up the tip
bm = bmesh.new()
rings = []
for z, r in profile:
    rings.append([bm.verts.new((r * math.cos(2 * math.pi * (i + 0.5) / SIDES),
                                r * math.sin(2 * math.pi * (i + 0.5) / SIDES), z)) for i in range(SIDES)])
for lower, upper in zip(rings, rings[1:]):
    for i in range(SIDES):
        j = (i + 1) % SIDES
        bm.faces.new((lower[i], lower[j], upper[j], upper[i]))
bm.faces.new(list(reversed(rings[0])))
bm.faces.new(rings[-1])
tip_mesh = bpy.data.meshes.new("Tip")
bm.to_mesh(tip_mesh)
bm.free()
tip = bpy.data.objects.new("Tip", tip_mesh)
bpy.context.scene.collection.objects.link(tip)
tip.data.materials.append(bakelite)
bevel(tip, 0.005, 2)   # crisp facets that still catch a highlight on their edges

for ob in (lever, tip):
    offset = ob.location - pivot
    angle = math.radians(-TILT)
    rot = Vector((offset.x,
                  offset.y * math.cos(angle) - offset.z * math.sin(angle),
                  offset.y * math.sin(angle) + offset.z * math.cos(angle)))
    ob.location = pivot + rot
    ob.rotation_euler = (angle, 0, 0)

# A floor that catches the shadow and is otherwise transparent.
bpy.ops.mesh.primitive_plane_add(size=8, location=(0, 0, 0))
bpy.context.active_object.is_shadow_catcher = True
_panel = bpy.data.materials.new("SageEnamel")
_panel.use_nodes = True
_pb = _panel.node_tree.nodes["Principled BSDF"]
_pb.inputs["Base Color"].default_value = (0.38, 0.527, 0.393, 1.0)
_pb.inputs["Roughness"].default_value = 0.45
_pb.inputs["Coat Weight"].default_value = 0.3
bpy.context.active_object.data.materials.append(_panel)

# ----- camera: straight down ---------------------------------------------------------------------
cam_data = bpy.data.cameras.new("Camera")
cam_data.type = "ORTHO"
cam_data.ortho_scale = 1.7
cam = bpy.data.objects.new("Camera", cam_data)
TALL = 1.5 if True else 1.0  # room below for the shadow
cam_data.sensor_fit = "HORIZONTAL"
cam.location = (0, 1.7 / 2 * (1 - TALL), 6)
import os
DIST = float(os.environ.get("CAM_DIST", "8"))
if DIST > 0:
    cam_data.type = "PERSP"
    cam_data.sensor_width = 36
    cam_data.lens = DIST * 36 / 1.7       # the panel plane spans 1.7 units across
    cam_data.clip_end = 100
    tilt = math.radians(float(os.environ.get("CAM_TILT", "0")))
    cam.location = (0, -DIST * math.sin(tilt), DIST * math.cos(tilt))
    cam.rotation_euler = (tilt, 0, 0)        # aimed at the part's centre
    cam_data.shift_y = (1 - TALL) / 2        # the extra height hangs below (shift is in widths)
scene.collection.objects.link(cam)
scene.camera = cam

# ----- light: the knobs' key from the top of the unit, and the studio HDRI ----------------------
key_data = bpy.data.lights.new("Key", "AREA")
key_data.shape = "DISK"
key_data.size = 5.0   # softer than the knobs' key: at this size a lever's hard shadow read as a smear
key_data.energy = 200
key = bpy.data.objects.new("Key", key_data)
key.location = (0, 3.4, 4.0)
key.rotation_mode = "QUATERNION"
key.rotation_quaternion = (Vector((0, 0, 0.3)) - key.location).to_track_quat("-Z", "Y")
scene.collection.objects.link(key)

image = bpy.data.images.load(HDRI)
# Turn the studio so its brightest light sits toward the top of the unit (+Y).
w, h = image.size
px = image.pixels[:]
best, best_u = -1.0, 0.5
for y in range(h // 2, h, 4):           # the upper hemisphere (rows count from the bottom)
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
bg = wn.nodes["Background"]
strength = float(argv[4]) if len(argv) > 4 else 1.0
hue = wn.nodes.new("ShaderNodeHueSaturation")
hue.inputs["Saturation"].default_value = float(argv[5]) if len(argv) > 5 else 0.5
wn.links.new(env.outputs["Color"], hue.inputs["Color"])
bg.inputs["Strength"].default_value = strength
wn.links.new(hue.outputs["Color"], bg.inputs["Color"])
dim = wn.nodes.new("ShaderNodeBackground")
dim.inputs["Strength"].default_value = strength * 0.2
wn.links.new(hue.outputs["Color"], dim.inputs["Color"])
path = wn.nodes.new("ShaderNodeLightPath")
mix = wn.nodes.new("ShaderNodeMixShader")
wn.links.new(path.outputs["Is Glossy Ray"], mix.inputs["Fac"])
wn.links.new(dim.outputs["Background"], mix.inputs[1])
wn.links.new(bg.outputs["Background"], mix.inputs[2])
wn.links.new(mix.outputs["Shader"], wn.nodes["World Output"].inputs["Surface"])
scene.world = world

# ----- render ------------------------------------------------------------------------------------
scene.render.engine = "CYCLES"
scene.cycles.samples = 512
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
scene.render.resolution_x = SIZE
scene.render.resolution_y = round(SIZE * TALL)
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "Standard"
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("rendered toggle", STATE, OUT, "studio turned", round(math.degrees(bright_az - math.pi / 2)), "degrees")

