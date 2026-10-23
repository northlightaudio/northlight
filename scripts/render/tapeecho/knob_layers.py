"""Blender (4.2) script: a vintage skirted control knob, rendered straight down
(orthographic) in two layers that the card stacks:

  body     the knob without its pointer, lit from the top of the unit, its
           shadow caught on a transparent floor. Round, so it never rotates:
           the light and shadow always fall from the top.
  pointer  the cream inlay alone, lit evenly, on transparency: the only layer
           that turns with the value.

Run: blender --background --factory-startup --python knob_layers.py -- <body|pointer> <out.png> [size] [key] [fill] [room]
"""
import math
import os
import sys

import bpy
import bmesh

argv = sys.argv[sys.argv.index("--") + 1:]
LAYER, OUT = argv[0], argv[1]
SIZE = int(argv[2]) if len(argv) > 2 else 512
KEY = float(argv[3]) if len(argv) > 3 else 260.0
FILL = float(argv[4]) if len(argv) > 4 else 40.0
ROOM_STRENGTH = float(argv[5]) if len(argv) > 5 else 0.8
ROOM = os.environ.get("NORTHLIGHT_HDRI", r"H:\Tools\hdri\lythwood_lounge_2k.hdr")  # Poly Haven, CC0

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene


def material(name, base, metallic=0.0, roughness=0.4, coat=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*base, 1.0)
    p.inputs["Metallic"].default_value = metallic
    p.inputs["Roughness"].default_value = roughness
    p.inputs["Coat Weight"].default_value = coat
    return m


def knurled_cylinder(name, radius, depth, z, ridges, ridge_depth, mat, chamfer=0.0):
    """A cylinder knurled with sharp V-ridges, flat shaded. With `chamfer`, the
    ridges run on up through a bevel round the top edge (its top ring that much
    smaller), so they show from straight above as a ring of teeth catching the light."""
    bm = bmesh.new()
    n = ridges * 2
    heights = [z, z + depth - chamfer, z + depth] if chamfer > 0 else [z, z + depth]
    insets = [0.0, 0.0, chamfer] if chamfer > 0 else [0.0, 0.0]
    rings = []
    for h, inset in zip(heights, insets):
        ring = []
        for i in range(n):
            a = 2 * math.pi * i / n
            r = radius - inset - (ridge_depth if i % 2 else 0.0)
            ring.append(bm.verts.new((r * math.cos(a), r * math.sin(a), h)))
        rings.append(ring)
    for lower, upper in zip(rings, rings[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((lower[i], lower[j], upper[j], upper[i]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    scene.collection.objects.link(ob)
    ob.data.materials.append(mat)
    for poly in ob.data.polygons:
        poly.use_smooth = False
    return ob


def cylinder(name, radius, depth, z, mat, bevel=0.0):
    bpy.ops.mesh.primitive_cylinder_add(vertices=192, radius=radius, depth=depth, location=(0, 0, z + depth / 2))
    ob = bpy.context.active_object
    ob.name = name
    ob.data.materials.append(mat)
    if bevel > 0:
        b = ob.modifiers.new("bevel", "BEVEL")
        b.width = bevel
        b.segments = 6
        b.limit_method = "ANGLE"
    bpy.ops.object.shade_smooth()
    return ob


# ----- materials -----------------------------------------------------------------------------------
black = material("Bakelite", (0.012, 0.012, 0.011), roughness=0.3, coat=0.5)
cream = material("Inlay", (0.95, 0.92, 0.82), roughness=0.45)

# The cap: spun aluminium. Anisotropic with its grain running round the axis, and
# fine circular scratches that vary round the cap.
alu = material("Aluminium", (0.80, 0.81, 0.82), metallic=1.0, roughness=0.32)
nt = alu.node_tree
bsdf = nt.nodes["Principled BSDF"]
bsdf.inputs["Anisotropic"].default_value = 0.9
tangent = nt.nodes.new("ShaderNodeTangent")
tangent.direction_type = "RADIAL"
tangent.axis = "Z"
nt.links.new(tangent.outputs["Tangent"], bsdf.inputs["Tangent"])
coord = nt.nodes.new("ShaderNodeTexCoord")
sep = nt.nodes.new("ShaderNodeSeparateXYZ")
nt.links.new(coord.outputs["Object"], sep.inputs["Vector"])
# r = length(x, y); theta = atan2(y, x)
comb_xy = nt.nodes.new("ShaderNodeCombineXYZ")
nt.links.new(sep.outputs["X"], comb_xy.inputs["X"])
nt.links.new(sep.outputs["Y"], comb_xy.inputs["Y"])
length = nt.nodes.new("ShaderNodeVectorMath")
length.operation = "LENGTH"
nt.links.new(comb_xy.outputs["Vector"], length.inputs[0])
theta = nt.nodes.new("ShaderNodeMath")
theta.operation = "ARCTAN2"
nt.links.new(sep.outputs["Y"], theta.inputs[0])
nt.links.new(sep.outputs["X"], theta.inputs[1])
r_scaled = nt.nodes.new("ShaderNodeMath")
r_scaled.operation = "MULTIPLY"
r_scaled.inputs[1].default_value = 900.0
nt.links.new(length.outputs["Value"], r_scaled.inputs[0])
t_scaled = nt.nodes.new("ShaderNodeMath")
t_scaled.operation = "MULTIPLY"
t_scaled.inputs[1].default_value = 2.5
nt.links.new(theta.outputs["Value"], t_scaled.inputs[0])
polar = nt.nodes.new("ShaderNodeCombineXYZ")
nt.links.new(r_scaled.outputs["Value"], polar.inputs["X"])
nt.links.new(t_scaled.outputs["Value"], polar.inputs["Y"])
noise = nt.nodes.new("ShaderNodeTexNoise")
noise.inputs["Scale"].default_value = 1.0
noise.inputs["Detail"].default_value = 6.0
noise.inputs["Roughness"].default_value = 0.65
nt.links.new(polar.outputs["Vector"], noise.inputs["Vector"])
bump = nt.nodes.new("ShaderNodeBump")
bump.inputs["Strength"].default_value = 0.08
bump.inputs["Distance"].default_value = 0.002
nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
rough = nt.nodes.new("ShaderNodeMapRange")
rough.inputs["To Min"].default_value = 0.24
rough.inputs["To Max"].default_value = 0.4
nt.links.new(noise.outputs["Fac"], rough.inputs["Value"])
nt.links.new(rough.outputs["Result"], bsdf.inputs["Roughness"])

# ----- the knob ----------------------------------------------------------------------------------
parts = []
if LAYER == "body":
    parts.append(knurled_cylinder("Skirt", 1.0, 0.16, 0.0, 150, 0.018, black, chamfer=0.05))
    parts.append(knurled_cylinder("Body", 0.70, 0.62, 0.16, 90, 0.022, black, chamfer=0.045))
    parts.append(cylinder("Cap", 0.58, 0.06, 0.78, alu, bevel=0.014))
    # A floor that catches the shadow and is otherwise transparent.
    bpy.ops.mesh.primitive_plane_add(size=12, location=(0, 0, 0))
    floor = bpy.context.active_object
    floor.is_shadow_catcher = True
    _panel = bpy.data.materials.new("SageEnamel")
    _panel.use_nodes = True
    _pb = _panel.node_tree.nodes["Principled BSDF"]
    _pb.inputs["Base Color"].default_value = (0.38, 0.527, 0.393, 1.0)
    _pb.inputs["Roughness"].default_value = 0.45
    _pb.inputs["Coat Weight"].default_value = 0.3
    floor.data.materials.append(_panel)
else:
    # The inlay along the skirt's top toward the top of the image (+Y).
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0.855, 0.165))
    ptr = bpy.context.active_object
    ptr.scale = (0.045, 0.24, 0.012)
    ptr.data.materials.append(cream)
    b = ptr.modifiers.new("bevel", "BEVEL")
    b.width = 0.006
    b.segments = 3

# ----- camera ------------------------------------------------------------------------------------
cam_data = bpy.data.cameras.new("Camera")
cam_data.type = "ORTHO"
cam_data.ortho_scale = 3.4  # room round the knob for its shadow
cam = bpy.data.objects.new("Camera", cam_data)
TALL = 1.5 if LAYER == "body" else 1.0  # room below for the shadow
cam_data.sensor_fit = "HORIZONTAL"
cam.location = (0, 3.4 / 2 * (1 - TALL), 6)
import os
DIST = float(os.environ.get("CAM_DIST", "8"))
if DIST > 0:
    cam_data.type = "PERSP"
    cam_data.sensor_width = 36
    cam_data.lens = DIST * 36 / 3.4       # the panel plane spans 3.4 units across
    cam_data.clip_end = 100
    tilt = math.radians(float(os.environ.get("CAM_TILT", "0")))
    cam.location = (0, -DIST * math.sin(tilt), DIST * math.cos(tilt))
    cam.rotation_euler = (tilt, 0, 0)        # aimed at the part's centre
    cam_data.shift_y = (1 - TALL) / 2        # the extra height hangs below (shift is in widths)
scene.collection.objects.link(cam)
scene.camera = cam


def area(name, size, energy, location, target=(0, 0, 0.4)):
    d = bpy.data.lights.new(name, "AREA")
    d.shape = "DISK"
    d.size = size
    d.energy = energy
    o = bpy.data.objects.new(name, d)
    o.location = location
    scene.collection.objects.link(o)
    direction = [t - l for t, l in zip(target, location)]
    o.rotation_mode = "QUATERNION"
    from mathutils import Vector
    o.rotation_quaternion = Vector(direction).to_track_quat("-Z", "Y")
    return o


world = bpy.data.worlds.new("World")
world.use_nodes = True
bg = world.node_tree.nodes["Background"]
scene.world = world
if LAYER == "body":
    # The key from the top of the unit (+Y), raised 50 degrees; a soft fill from
    # above; a low rim from the top to edge the knurl.
    area("Key", 2.5, KEY, (0, 3.4, 4.0))
    area("Fill", 6.0, FILL, (0, 0, 7.0))
    rim = area("Rim", 1.5, KEY * 0.35, (0, 4.2, 1.2), target=(0, 0, 0.5))
    rim.data.use_shadow = False  # an edge light only: its long shadow would leave the frame
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
    wn = world.node_tree
    coord_w = wn.nodes.new("ShaderNodeTexCoord")
    mapping = wn.nodes.new("ShaderNodeMapping")
    mapping.inputs["Rotation"].default_value = (0.0, 0.0, bright_az - math.pi / 2)
    env = wn.nodes.new("ShaderNodeTexEnvironment")
    env.image = image
    wn.links.new(coord_w.outputs["Generated"], mapping.inputs["Vector"])
    wn.links.new(mapping.outputs["Vector"], env.inputs["Vector"])
    hue = wn.nodes.new("ShaderNodeHueSaturation")
    hue.inputs["Saturation"].default_value = 0.5
    wn.links.new(env.outputs["Color"], hue.inputs["Color"])
    bg.inputs["Strength"].default_value = ROOM_STRENGTH
    wn.links.new(hue.outputs["Color"], bg.inputs["Color"])
    dim = wn.nodes.new("ShaderNodeBackground")
    dim.inputs["Strength"].default_value = ROOM_STRENGTH * 0.2
    wn.links.new(hue.outputs["Color"], dim.inputs["Color"])
    path = wn.nodes.new("ShaderNodeLightPath")
    mix = wn.nodes.new("ShaderNodeMixShader")
    wn.links.new(path.outputs["Is Glossy Ray"], mix.inputs["Fac"])
    wn.links.new(dim.outputs["Background"], mix.inputs[1])
    wn.links.new(bg.outputs["Background"], mix.inputs[2])
    wn.links.new(mix.outputs["Shader"], wn.nodes["World Output"].inputs["Surface"])
else:
    # Even light: the pointer's own shading must not depend on its angle.
    area("Fill", 8.0, 160, (0, 0, 7.0))
    bg.inputs["Color"].default_value = (0.5, 0.5, 0.5, 1.0)
    bg.inputs["Strength"].default_value = 0.6

# ----- render ------------------------------------------------------------------------------------
scene.render.engine = "CYCLES"
scene.cycles.samples = 384
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
print("rendered", LAYER, OUT)


