# Tools/blender/preview_render.py
#
# Renders front / three-quarter / side previews of a generated body with a material that
# approximates the in-game look (vertex colours: R veins, G mottling, B creases, A glow).

import bpy, math, os
from mathutils import Vector


def _preview_material():
    mat = bpy.data.materials.new("ZombiePreview")
    mat.use_nodes = True
    nt = mat.node_tree
    nodes, links = nt.nodes, nt.links
    for n in list(nodes):
        nodes.remove(n)

    out = nodes.new("ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    attr = nodes.new("ShaderNodeVertexColor")
    attr.layer_name = "Col"
    sep = nodes.new("ShaderNodeSeparateColor")

    skin = nodes.new("ShaderNodeRGB");   skin.outputs[0].default_value = (0.17, 0.075, 0.23, 1)
    bruise = nodes.new("ShaderNodeRGB"); bruise.outputs[0].default_value = (0.045, 0.015, 0.07, 1)
    vein = nodes.new("ShaderNodeRGB");   vein.outputs[0].default_value = (0.04, 0.02, 0.10, 1)
    glowc = nodes.new("ShaderNodeRGB");  glowc.outputs[0].default_value = (0.85, 0.15, 0.75, 1)

    def mix(a, b, fac):
        m = nodes.new("ShaderNodeMix")
        m.data_type = 'RGBA'
        links.new(fac, m.inputs["Factor"])
        links.new(a, m.inputs["A"])
        links.new(b, m.inputs["B"])
        return m.outputs["Result"]

    links.new(attr.outputs["Color"], sep.inputs["Color"])
    mottled = mix(skin.outputs[0], bruise.outputs[0], sep.outputs["Green"])
    veined = mix(mottled, vein.outputs[0], sep.outputs["Red"])
    dark = nodes.new("ShaderNodeMapRange")
    dark.inputs["To Min"].default_value = 1.0
    dark.inputs["To Max"].default_value = 0.4
    links.new(sep.outputs["Blue"], dark.inputs["Value"])
    shaded = nodes.new("ShaderNodeMix"); shaded.data_type = 'RGBA'; shaded.blend_type = 'MULTIPLY'
    shaded.inputs["Factor"].default_value = 1.0
    links.new(veined, shaded.inputs["A"])
    links.new(dark.outputs["Result"], shaded.inputs["B"])
    glow_base = mix(shaded.outputs["Result"], glowc.outputs[0], attr.outputs["Alpha"])

    links.new(glow_base, bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.45
    bsdf.inputs["Subsurface Weight"].default_value = 0.25
    bsdf.inputs["Subsurface Radius"].default_value = (0.6, 0.15, 0.5)
    emit = nodes.new("ShaderNodeMath"); emit.operation = 'MULTIPLY'
    emit.inputs[1].default_value = 6.0
    links.new(attr.outputs["Alpha"], emit.inputs[0])
    links.new(glowc.outputs[0], bsdf.inputs["Emission Color"])
    links.new(emit.outputs[0], bsdf.inputs["Emission Strength"])
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return mat


def _metal_material():
    mat = bpy.data.materials.new("ZombiePreviewMetal")
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = nodes["Principled BSDF"]
    attr = nodes.new("ShaderNodeVertexColor"); attr.layer_name = "Col"
    sep = nodes.new("ShaderNodeSeparateColor")
    links.new(attr.outputs["Color"], sep.inputs["Color"])
    steel = (0.20, 0.21, 0.25, 1); rust = (0.32, 0.11, 0.05, 1)
    m = nodes.new("ShaderNodeMix"); m.data_type = 'RGBA'
    m.inputs["A"].default_value = steel; m.inputs["B"].default_value = rust
    links.new(sep.outputs["Green"], m.inputs["Factor"])
    links.new(m.outputs["Result"], bsdf.inputs["Base Color"])
    inv = nodes.new("ShaderNodeMath"); inv.operation = 'SUBTRACT'; inv.inputs[0].default_value = 1.0
    links.new(sep.outputs["Green"], inv.inputs[1])
    links.new(inv.outputs[0], bsdf.inputs["Metallic"])
    bsdf.inputs["Roughness"].default_value = 0.32
    bsdf.inputs["Emission Color"].default_value = (0.9, 0.2, 1.0, 1)
    em = nodes.new("ShaderNodeMath"); em.operation = 'MULTIPLY'; em.inputs[1].default_value = 12.0
    links.new(attr.outputs["Alpha"], em.inputs[0])
    links.new(em.outputs[0], bsdf.inputs["Emission Strength"])
    return mat


def _set_engine(scene):
    for eng in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE", "CYCLES"):
        try:
            scene.render.engine = eng
            break
        except TypeError:
            continue
    if scene.render.engine == "CYCLES":
        scene.cycles.samples = 24
        scene.cycles.device = 'CPU'
    return scene.render.engine


def render(body, arm, out_dir, name, archetype):
    os.makedirs(out_dir, exist_ok=True)
    scene = bpy.context.scene
    engine = _set_engine(scene)
    scene.render.resolution_x = 720
    scene.render.resolution_y = 960
    scene.render.film_transparent = False
    scene.view_settings.view_transform = 'AgX' if 'AgX' in [i.identifier for i in scene.view_settings.bl_rna.properties['view_transform'].enum_items] else 'Filmic'

    # Preview materials: flesh on every region slot, steel on the "Metal" slot.
    mat = _preview_material()
    metal = _metal_material()
    for i in range(len(body.data.materials)):
        orig = body.data.materials[i]
        body.data.materials[i] = metal if (orig and orig.name.startswith("Metal")) else mat

    # World: dark blue-grey, stylized.
    world = bpy.data.worlds.new("PreviewWorld")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.035, 0.04, 0.06, 1)
    world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
    scene.world = world

    def light(kind, loc, energy, color=(1, 1, 1), size=1.0):
        ld = bpy.data.lights.new("L", kind)
        ld.energy = energy
        ld.color = color
        if kind == 'AREA':
            ld.size = size
        lo = bpy.data.objects.new("L", ld)
        lo.location = loc
        scene.collection.objects.link(lo)
        direction = Vector((0, 0, 1.0)) - lo.location
        lo.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
        return lo

    light('AREA', (-1.6, -2.2, 2.6), 420, (1.0, 0.93, 0.88), 1.6)  # key
    light('AREA', (2.0, -1.0, 1.2), 90, (0.55, 0.65, 1.0), 2.0)     # cool fill
    light('AREA', (0.8, 2.4, 2.2), 1400, (0.85, 0.45, 1.0), 1.2)    # purple rim from behind

    # Floor for grounding shadows.
    bpy.ops.mesh.primitive_plane_add(size=6, location=(0, 0, 0))
    floor = bpy.context.active_object
    fm = bpy.data.materials.new("Floor"); fm.use_nodes = True
    fm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.05, 0.05, 0.07, 1)
    floor.data.materials.append(fm)

    cam_data = bpy.data.cameras.new("Cam")
    cam_data.type = 'ORTHO'
    cam_data.ortho_scale = 2.15
    cam = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam

    target = Vector((0, 0, 0.93))
    views = {"front": 0.0, "threequarter": 35.0, "side": 90.0}
    paths = []
    for view, deg in views.items():
        a = math.radians(deg)
        cam.location = target + Vector((math.sin(a) * 4.0, -math.cos(a) * 4.0, 0.25))
        cam.rotation_euler = (target - cam.location).to_track_quat('-Z', 'Y').to_euler()
        path = os.path.abspath(os.path.join(out_dir, "%s_%s.png" % (name, view)))
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        paths.append(path)
    print("[ZGEN] previews (%s): %s" % (engine, ", ".join(paths)))
    return paths
