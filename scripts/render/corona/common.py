"""Shared set-up for Corona's Blender scripts: the one lighting every part gets (a key
from the top of the unit, the lounge in the reflections only and desaturated),
shadow-catcher floors, material helpers and the GPU render. Imported by each script
(they put this folder on sys.path), so the parts cannot drift apart.

Units are the tape echo's: 50 page px per unit (about 20 mm), the top of the unit is
+Y, the camera looks down -Z.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from render_config import hdri_path  # noqa: E402  (scripts/render: NORTHLIGHT_HDRI or render.local.json)
ROOM = hdri_path()  # Poly Haven, CC0

# The face's colours (sRGB), the floors shadows are caught on.
TOLEX = "#2B1714"
CHAMPAGNE = "#C8B48C"
GRAPHITE = "#1F201E"
CLOTH = "#7A5A3E"
BAFFLE = "#141312"


def lin(hex_or_rgb):
    """sRGB (hex or 0..1 tuple) to linear RGB."""
    if isinstance(hex_or_rgb, str):
        h = hex_or_rgb.lstrip("#")
        c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    else:
        c = list(hex_or_rgb)
    return tuple(v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in c)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    return bpy.context.scene


def node(nt, kind, **inputs):
    n = nt.nodes.new(kind)
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


def math_node(nt, op, a, b=None, c=None, clamp=False):
    n = nt.nodes.new("ShaderNodeMath")
    n.operation = op
    n.use_clamp = clamp
    for i, v in enumerate((a, b, c)):
        if v is None:
            continue
        if isinstance(v, (int, float)):
            n.inputs[i].default_value = v
        else:
            nt.links.new(v, n.inputs[i])
    return n.outputs["Value"]


def ramp(nt, src, lo, hi, a=0.0, b=1.0):
    r = node(nt, "ShaderNodeMapRange", **{"From Min": lo, "From Max": hi, "To Min": a, "To Max": b})
    nt.links.new(src, r.inputs["Value"])
    return r.outputs["Result"]


def noise(nt, vec, scale, sx=1.0, sy=1.0, sz=1.0, detail=6.0, rough=0.55):
    """Noise over `vec` (a socket), stretched: sx, sy, sz multiply the coordinates."""
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (sx, sy, sz)
    nt.links.new(vec, mp.inputs["Vector"])
    t = node(nt, "ShaderNodeTexNoise", Scale=scale, Detail=detail, Roughness=rough)
    nt.links.new(mp.outputs["Vector"], t.inputs["Vector"])
    return t.outputs["Fac"]


def material(name, base, metallic=0.0, rough=0.5, coat=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = (*base, 1)
    p.inputs["Metallic"].default_value = metallic
    p.inputs["Roughness"].default_value = rough
    p.inputs["Coat Weight"].default_value = coat
    return m


def link(scene, name, bm, mat):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    scene.collection.objects.link(ob)
    if mat is not None:
        ob.data.materials.append(mat)
    return ob


def revolve(scene, name, profile, mat, segments=192, smooth=True):
    """A solid of revolution about Z from (r, z) points, in order along the surface."""
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
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    ob = link(scene, name, bm, mat)
    for p in ob.data.polygons:
        p.use_smooth = smooth
    return ob


def rrect(w, h, r, inset=0.0, segs=12, min_r=0.004):
    """A rounded rectangle of half-size (w, h) and corner radius r, inset by `inset`,
    as points; every inset has the same count, so rings bridge."""
    w, h = w - inset, h - inset
    r = max(r - inset, min_r)
    pts = []
    for cx, cy, a0 in ((w - r, h - r, 0), (-(w - r), h - r, 90), (-(w - r), -(h - r), 180), (w - r, -(h - r), 270)):
        for i in range(segs + 1):
            a = math.radians(a0 + 90 * i / segs)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def loft(scene, name, rings, mat, cap_top=True, cap_bottom=True):
    """Bridges rings of (x, y, z) points (all the same count) bottom to top."""
    bm = bmesh.new()
    vrings = [[bm.verts.new(p) for p in ring] for ring in rings]
    n = len(rings[0])
    for lower, upper in zip(vrings, vrings[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((lower[i], lower[j], upper[j], upper[i]))
    if cap_bottom:
        bm.faces.new(list(reversed(vrings[0])))
    if cap_top:
        bm.faces.new(vrings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return link(scene, name, bm, mat)


def bevel(ob, width, segments=3):
    b = ob.modifiers.new("bevel", "BEVEL")
    b.width = width
    b.segments = segments
    b.limit_method = "ANGLE"
    return b


def smooth(ob, on=True):
    for p in ob.data.polygons:
        p.use_smooth = on


def smooth_by_angle(ob, degrees=35):
    """Smooth faces, keeping edges sharper than `degrees` crisp (4.2: per-edge sharp flags)."""
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    lim = math.radians(degrees)
    for e in bm.edges:
        if len(e.link_faces) == 2 and e.calc_face_angle(0.0) > lim:
            e.smooth = False
    for f in bm.faces:
        f.smooth = True
    bm.to_mesh(me)
    bm.free()


# ----- light ------------------------------------------------------------------------------------
def aim(ob, target):
    ob.rotation_mode = "QUATERNION"
    ob.rotation_quaternion = (Vector(target) - ob.location).to_track_quat("-Z", "Y")


def area(scene, name, size, energy, location, target=(0, 0, 0), shape="DISK", size_y=None, shadow=True,
         camera=False, glossy=True, diffuse=True):
    d = bpy.data.lights.new(name, "AREA")
    d.shape = shape
    d.size = size
    if size_y is not None:
        d.size_y = size_y
    d.energy = energy
    d.use_shadow = shadow
    o = bpy.data.objects.new(name, d)
    o.location = location
    aim(o, target)
    o.visible_camera = camera
    o.visible_glossy = glossy
    o.visible_diffuse = diffuse
    scene.collection.objects.link(o)
    return o


def key(scene, s=1.0, energy=200.0, size=2.5, target_z=0.3):
    """The tape echo's key: a disc from the top of the unit, raised about 50 degrees.
    `s` scales its geometry (and its power by s squared) for a part modelled at
    another scale than the page's units, so it lights the same."""
    return area(scene, "Key", size * s, energy * s * s, (0, 3.4 * s, 4.0 * s), (0, 0, target_z * s))


def key_sun(scene, energy=2.3, angle=30.0):
    """The key as a distant sun, for long parts lit alike along their length."""
    d = bpy.data.lights.new("Key", "SUN")
    d.energy = energy
    d.angle = math.radians(angle)
    o = bpy.data.objects.new("Key", d)
    o.rotation_mode = "QUATERNION"
    o.rotation_quaternion = (Vector((0, 0, 0.3)) - Vector((0, 3.4, 4.0))).to_track_quat("-Z", "Y")
    scene.collection.objects.link(o)
    return o


def room(scene, strength=0.8, saturation=0.5, rest=0.2, turn=0.0):
    """The lounge in the reflections only (diffuse sees it at `rest` of its strength),
    desaturated so its lamps do not tint the metal, turned so its brightest light
    sits at the top of the unit (+Y), then turned by `turn` radians about Z."""
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
    wm.inputs["Rotation"].default_value = (0.0, 0.0, bright_az - math.pi / 2 + turn)
    env = wn.nodes.new("ShaderNodeTexEnvironment")
    env.image = image
    wn.links.new(wc.outputs["Generated"], wm.inputs["Vector"])
    wn.links.new(wm.outputs["Vector"], env.inputs["Vector"])
    hue = wn.nodes.new("ShaderNodeHueSaturation")
    hue.inputs["Saturation"].default_value = saturation
    wn.links.new(env.outputs["Color"], hue.inputs["Color"])
    bg = wn.nodes["Background"]
    bg.inputs["Strength"].default_value = strength
    wn.links.new(hue.outputs["Color"], bg.inputs["Color"])
    dim = wn.nodes.new("ShaderNodeBackground")
    dim.inputs["Strength"].default_value = strength * rest
    wn.links.new(hue.outputs["Color"], dim.inputs["Color"])
    lp = wn.nodes.new("ShaderNodeLightPath")
    mix = wn.nodes.new("ShaderNodeMixShader")
    wn.links.new(lp.outputs["Is Glossy Ray"], mix.inputs["Fac"])
    wn.links.new(dim.outputs["Background"], mix.inputs[1])
    wn.links.new(bg.outputs["Background"], mix.inputs[2])
    wn.links.new(mix.outputs["Shader"], wn.nodes["World Output"].inputs["Surface"])
    scene.world = world
    return world


def flat_world(scene, grey=0.5, strength=0.6):
    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (grey, grey, grey, 1)
    bg.inputs["Strength"].default_value = strength
    scene.world = world


def shadow_floor(scene, colour, size=40.0, z=0.0):
    """A floor of the colour the part sits on that catches its shadow and is otherwise transparent."""
    bpy.ops.mesh.primitive_plane_add(size=size, location=(0, 0, z))
    floor = bpy.context.active_object
    floor.is_shadow_catcher = True
    floor.data.materials.append(material("Floor", lin(colour), rough=0.5))
    return floor


# ----- camera -----------------------------------------------------------------------------------
def camera_down(scene, frame_w, frame_h, centre=(0.0, 0.0), plane_z=0.0, dist=8.0):
    """Perspective straight down from `dist` above the plane z = plane_z, the plane
    spanning frame_w across; the frame's centre at `centre` on that plane while the
    camera stays over the origin (lens shift), as the tape echo's parts are seen."""
    cd = bpy.data.cameras.new("Camera")
    cd.type = "PERSP"
    cd.sensor_fit = "HORIZONTAL"
    cd.sensor_width = 36
    cd.lens = dist * 36 / frame_w
    cd.shift_x = centre[0] / frame_w
    cd.shift_y = centre[1] / frame_w
    cd.clip_start = 0.01
    cd.clip_end = dist * 4
    cam = bpy.data.objects.new("Camera", cd)
    cam.location = (0, 0, plane_z + dist)
    scene.collection.objects.link(cam)
    scene.camera = cam
    return cam


def camera_ortho(scene, frame_w, centre=(0.0, 0.0), z=20.0):
    cd = bpy.data.cameras.new("Camera")
    cd.type = "ORTHO"
    cd.sensor_fit = "HORIZONTAL"
    cd.ortho_scale = frame_w
    cd.clip_end = z * 3
    cam = bpy.data.objects.new("Camera", cd)
    cam.location = (centre[0], centre[1], z)
    scene.collection.objects.link(cam)
    scene.camera = cam
    return cam


# ----- render -----------------------------------------------------------------------------------
def render(scene, out, w, h, samples=512, transparent=True, denoise=True, bounces=None):
    scene.render.engine = "CYCLES"
    scene.cycles.samples = int(samples * float(os.environ.get("CORONA_SAMPLES", "1")))
    scene.cycles.use_denoising = denoise
    # A tight noise threshold: renders kept undenoised (their texture would be smoothed
    # away) must still be clean, or their PNGs swell with noise.
    scene.cycles.adaptive_threshold = float(os.environ.get("CORONA_NOISE", "0.003"))
    if bounces:
        scene.cycles.max_bounces = bounces
        scene.cycles.transmission_bounces = bounces
        scene.cycles.glossy_bounces = min(bounces, 12)
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
    scale = float(os.environ.get("CORONA_SCALE", "1"))   # quick tests: render smaller
    scene.render.film_transparent = transparent
    scene.render.resolution_x = round(w * scale)
    scene.render.resolution_y = round(h * scale)
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA" if transparent else "RGB"
    scene.render.image_settings.color_depth = "8"
    scene.view_settings.view_transform = "Standard"
    scene.render.filepath = out
    bpy.ops.render.render(write_still=True)
    print("rendered", out, scene.render.resolution_x, scene.render.resolution_y)
