# Tools/create_zombie_materials.py
#
# Builds the three materials the procedural zombies use, under /Game/Zombie/Materials.
# All variation comes from the generator's vertex colours (painted in each part's
# own space, so patterns stick to the body instead of sliding as it moves):
#   R = veins, G = mottling / rust / pulse phase, B = crease darkening.
# Every material exposes a "Color" vector parameter, which the zombie code sets
# per spawn.
#
# Run with the editor closed:
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nosplash
# Re-running replaces the materials.

import unreal

MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
FOLDER = "/Game/Zombie/Materials"
LOG = []


def log(msg):
    LOG.append(msg)
    unreal.log_warning("[ZMAT] " + msg)


def new_material(name):
    path = FOLDER + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    return TOOLS.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def node(mat, cls, x, y, **props):
    e = MEL.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


def vparam(mat, name, rgb, x, y):
    return node(mat, unreal.MaterialExpressionVectorParameter, x, y,
                parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))


def sparam(mat, name, value, x, y):
    return node(mat, unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)


def link(a, a_out, b, b_in):
    if not MEL.connect_material_expressions(a, a_out, b, b_in):
        log("  !! failed to connect %s.%s -> %s.%s" % (a.get_name(), a_out, b.get_name(), b_in))


def to_prop(e, out, prop):
    if not MEL.connect_material_property(e, out, prop):
        log("  !! failed to connect %s.%s -> %s" % (e.get_name(), out, prop))


def finish(mat):
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    log("saved %s" % mat.get_path_name())


def add_glow(m, vc, x, y, strength=4.0, default=(0.85, 0.15, 0.75)):
    """Emissive from the vertex-colour glow mask (A): sacs, eyes, lesion cracks, lenses."""
    gcol = vparam(m, "GlowColor", default, x, y)
    gstr = sparam(m, "GlowStrength", strength, x, y + 150)
    t = node(m, unreal.MaterialExpressionTime, x, y + 300)
    speed = sparam(m, "PulseSpeed", 0.6, x, y + 400)
    tf = node(m, unreal.MaterialExpressionMultiply, x + 200, y + 300)
    link(t, "", tf, "A"); link(speed, "", tf, "B")
    ph = node(m, unreal.MaterialExpressionAdd, x + 350, y + 300)
    link(tf, "", ph, "A"); link(vc, "G", ph, "B")
    wave = node(m, unreal.MaterialExpressionSine, x + 500, y + 300)
    link(ph, "", wave, "")
    half = node(m, unreal.MaterialExpressionMultiply, x + 650, y + 300, const_b=0.5)
    link(wave, "", half, "A")
    p01 = node(m, unreal.MaterialExpressionAdd, x + 800, y + 300, const_b=0.5)
    link(half, "", p01, "A")
    pulse = node(m, unreal.MaterialExpressionLinearInterpolate, x + 950, y + 300, const_a=0.45, const_b=1.0)
    link(p01, "", pulse, "Alpha")
    k1 = node(m, unreal.MaterialExpressionMultiply, x + 950, y + 150)
    link(vc, "A", k1, "A"); link(gstr, "", k1, "B")
    k2 = node(m, unreal.MaterialExpressionMultiply, x + 1100, y + 200)
    link(k1, "", k2, "A"); link(pulse, "", k2, "B")
    em = node(m, unreal.MaterialExpressionMultiply, x + 1250, y + 100)
    link(gcol, "", em, "A"); link(k2, "", em, "B")
    to_prop(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return gcol


# ------------------------------------------------------------------ Flesh
def build_flesh():
    m = new_material("M_ZombieFlesh")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SUBSURFACE)

    vc = node(m, unreal.MaterialExpressionVertexColor, -1200, 0)
    skin = vparam(m, "Color", (0.30, 0.18, 0.40), -1200, -300)
    bruise = vparam(m, "BruiseColor", (0.12, 0.05, 0.16), -1200, -150)
    vein = vparam(m, "VeinColor", (0.05, 0.02, 0.12), -1200, 250)

    # Mottled bruising: lerp skin -> bruise by G (scaled).
    g_scaled = node(m, unreal.MaterialExpressionMultiply, -900, -100, const_b=0.8)
    link(vc, "G", g_scaled, "A")
    mottled = node(m, unreal.MaterialExpressionLinearInterpolate, -700, -200)
    link(skin, "", mottled, "A"); link(bruise, "", mottled, "B"); link(g_scaled, "", mottled, "Alpha")

    # Veins: lerp toward vein colour by R.
    veined = node(m, unreal.MaterialExpressionLinearInterpolate, -500, -100)
    link(mottled, "", veined, "A"); link(vein, "", veined, "B"); link(vc, "R", veined, "Alpha")

    # Creases: darken by B.
    dark = node(m, unreal.MaterialExpressionLinearInterpolate, -500, 150, const_a=1.0, const_b=0.4)
    link(vc, "B", dark, "Alpha")
    base = node(m, unreal.MaterialExpressionMultiply, -300, 0)
    link(veined, "", base, "A"); link(dark, "", base, "B")

    gcol = add_glow(m, vc, -1200, 700)
    glow_tint = node(m, unreal.MaterialExpressionLinearInterpolate, -150, 0)
    link(base, "", glow_tint, "A"); link(gcol, "", glow_tint, "B"); link(vc, "A", glow_tint, "Alpha")
    to_prop(glow_tint, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Dry, matte skin (was glossy enough to read as plastic); a little wetter in creases.
    rough = node(m, unreal.MaterialExpressionLinearInterpolate, -300, 250, const_a=0.82, const_b=0.58)
    link(vc, "B", rough, "Alpha")
    to_prop(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = sparam(m, "Specular", 0.3, -300, 330)
    to_prop(spec, "", unreal.MaterialProperty.MP_SPECULAR)

    sss = vparam(m, "SubsurfaceColor", (0.55, 0.08, 0.35), -300, 400)
    to_prop(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    amount = sparam(m, "SubsurfaceAmount", 0.6, -300, 550)
    to_prop(amount, "", unreal.MaterialProperty.MP_OPACITY)
    finish(m)


# ------------------------------------------------------------------ Growth (glowing, pulsing)
def build_growth():
    m = new_material("M_ZombieGrowth")
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SUBSURFACE)

    vc = node(m, unreal.MaterialExpressionVertexColor, -1300, 0)
    col = vparam(m, "Color", (0.75, 0.12, 0.65), -1300, -250)

    dark = node(m, unreal.MaterialExpressionLinearInterpolate, -900, 200, const_a=1.0, const_b=0.5)
    link(vc, "B", dark, "Alpha")
    base = node(m, unreal.MaterialExpressionMultiply, -600, -150)
    link(col, "", base, "A"); link(dark, "", base, "B")
    to_prop(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # Pulse: 0.5 + 0.5 * sin(Time * Speed + phase), phase from vertex G so sacs desync.
    t = node(m, unreal.MaterialExpressionTime, -1300, 450)
    speed = sparam(m, "PulseSpeed", 0.6, -1300, 550)
    t_fast = node(m, unreal.MaterialExpressionMultiply, -1100, 450)
    link(t, "", t_fast, "A"); link(speed, "", t_fast, "B")
    phase = node(m, unreal.MaterialExpressionAdd, -900, 450)
    link(t_fast, "", phase, "A"); link(vc, "G", phase, "B")
    wave = node(m, unreal.MaterialExpressionSine, -750, 450)
    link(phase, "", wave, "")
    half = node(m, unreal.MaterialExpressionMultiply, -600, 450, const_b=0.5)
    link(wave, "", half, "A")
    pulse01 = node(m, unreal.MaterialExpressionAdd, -450, 450, const_b=0.5)
    link(half, "", pulse01, "A")
    pulse = node(m, unreal.MaterialExpressionLinearInterpolate, -300, 450, const_a=0.3, const_b=1.0)
    link(pulse01, "", pulse, "Alpha")

    strength = sparam(m, "EmissiveStrength", 2.5, -450, 650)
    glow = node(m, unreal.MaterialExpressionMultiply, -150, 450)
    link(pulse, "", glow, "A"); link(strength, "", glow, "B")
    emissive = node(m, unreal.MaterialExpressionMultiply, 0, 300)
    link(col, "", emissive, "A"); link(glow, "", emissive, "B")
    to_prop(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    rough = sparam(m, "Roughness", 0.2, -150, 150)
    to_prop(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    sss = vparam(m, "SubsurfaceColor", (0.9, 0.2, 0.7), -150, 750)
    to_prop(sss, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    amount = sparam(m, "SubsurfaceAmount", 0.8, -150, 900)
    to_prop(amount, "", unreal.MaterialProperty.MP_OPACITY)
    finish(m)


# ------------------------------------------------------------------ Metal (rusted machinery)
def build_metal():
    m = new_material("M_ZombieMetal")

    vc = node(m, unreal.MaterialExpressionVertexColor, -1000, 0)
    steel = vparam(m, "Color", (0.16, 0.17, 0.21), -1000, -250)
    rust = vparam(m, "RustColor", (0.30, 0.10, 0.05), -1000, -100)

    rusted = node(m, unreal.MaterialExpressionLinearInterpolate, -700, -150)
    link(steel, "", rusted, "A"); link(rust, "", rusted, "B"); link(vc, "G", rusted, "Alpha")
    dark = node(m, unreal.MaterialExpressionLinearInterpolate, -700, 150, const_a=1.0, const_b=0.35)
    link(vc, "B", dark, "Alpha")
    base = node(m, unreal.MaterialExpressionMultiply, -450, 0)
    link(rusted, "", base, "A"); link(dark, "", base, "B")
    to_prop(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

    metallic = node(m, unreal.MaterialExpressionLinearInterpolate, -450, 200, const_a=1.0, const_b=0.0)
    link(vc, "G", metallic, "Alpha")
    to_prop(metallic, "", unreal.MaterialProperty.MP_METALLIC)

    rough = node(m, unreal.MaterialExpressionLinearInterpolate, -450, 350, const_a=0.28, const_b=0.85)
    link(vc, "G", rough, "Alpha")
    to_prop(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    add_glow(m, vc, -1000, 600, strength=8.0)
    finish(m)


build_flesh()
build_growth()
build_metal()

import os
with open(os.path.join(unreal.Paths.project_saved_dir(), "zombie_materials.txt"), "w") as f:
    f.write("\n".join(LOG))
