# Tools/create_ring_materials.py
#
# Materials for the ring regions (WORLDGEN_PLAN R3, R4), both coloured by vertex colour:
#   /Game/RingWorld/M_RingGround  terrain tiles: vertex colour times a faint 4 m world-aligned
#                                 checker, so you can read your speed over open ground
#   /Game/RingWorld/M_RingBand    the whole-ring band: plain vertex colour
# Re-runnable. Run (editor closed):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this> -unattended -nosplash

import unreal

DIR = "/Game/RingWorld"
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def log(m):
    unreal.log_warning("[RINGMAT] " + str(m))


def new_material(name):
    path = DIR + "/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    return TOOLS.create_asset(name, DIR, unreal.Material, unreal.MaterialFactoryNew())


def node(m, cls, x, y, **props):
    e = MEL.create_material_expression(m, cls, x, y)
    for k, v in props.items():
        e.set_editor_property(k, v)
    return e


# Ground: vertex colour x (0.9 or 1.0 on a 4 m checker).
g = new_material("M_RingGround")
vc = node(g, unreal.MaterialExpressionVertexColor, -900, -100)
wp = node(g, unreal.MaterialExpressionWorldPosition, -1400, 200)
div = node(g, unreal.MaterialExpressionDivide, -1250, 200, const_b=400.0)
MEL.connect_material_expressions(wp, "", div, "A")
flo = node(g, unreal.MaterialExpressionFloor, -1100, 200)
MEL.connect_material_expressions(div, "", flo, "")
ones = node(g, unreal.MaterialExpressionConstant3Vector, -1100, 320, constant=unreal.LinearColor(1.0, 1.0, 1.0, 0.0))
dot = node(g, unreal.MaterialExpressionDotProduct, -950, 200)
MEL.connect_material_expressions(flo, "", dot, "A")
MEL.connect_material_expressions(ones, "", dot, "B")
half = node(g, unreal.MaterialExpressionMultiply, -800, 200, const_b=0.5)
MEL.connect_material_expressions(dot, "", half, "A")
frac = node(g, unreal.MaterialExpressionFrac, -650, 200)
MEL.connect_material_expressions(half, "", frac, "")
two = node(g, unreal.MaterialExpressionMultiply, -520, 200, const_b=2.0)
MEL.connect_material_expressions(frac, "", two, "A")
shade = node(g, unreal.MaterialExpressionLinearInterpolate, -380, 160, const_a=0.9, const_b=1.0)
MEL.connect_material_expressions(two, "", shade, "Alpha")
col = node(g, unreal.MaterialExpressionMultiply, -200, 0)
MEL.connect_material_expressions(vc, "", col, "A")
MEL.connect_material_expressions(shade, "", col, "B")
MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
rough = node(g, unreal.MaterialExpressionConstant, -200, 220, r=0.9)
MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
MEL.recompile_material(g)
EAL.save_loaded_asset(g, False)
log("M_RingGround")

# Band: plain vertex colour.
b = new_material("M_RingBand")
vc2 = node(b, unreal.MaterialExpressionVertexColor, -400, 0)
MEL.connect_material_property(vc2, "", unreal.MaterialProperty.MP_BASE_COLOR)
rough2 = node(b, unreal.MaterialExpressionConstant, -400, 200, r=1.0)
MEL.connect_material_property(rough2, "", unreal.MaterialProperty.MP_ROUGHNESS)
MEL.recompile_material(b)
EAL.save_loaded_asset(b, False)
log("M_RingBand")
