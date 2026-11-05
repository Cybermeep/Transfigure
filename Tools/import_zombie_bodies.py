# Tools/import_zombie_bodies.py
#
# Imports every generated body in Tools/blender/output (<name>.fbx + <name>.json) into
#   /Game/Zombie/Bodies/SKM_<name>   skeletal mesh on the UE5 Mannequin skeleton,
#                                    PA_Mannequin physics asset, vertex colours
#   /Game/Zombie/Bodies/DA_<name>    UZombieBodyVariant (form, seed, templates, limb radii)
# AProceduralZombie finds the DA_ assets by archetype at runtime.
#
# Run (editor closed):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this> -unattended -nosplash -AllowCommandletRendering
# Optional: set ZOMBIE_IMPORT_ONLY=Husk_001,Bloat_002 to import a subset.

import glob, json, os
import unreal

PROJ = unreal.Paths.project_dir()
SRC = os.path.abspath(os.path.join(PROJ, "Tools", "blender", "output"))
DEST = "/Game/Zombie/Bodies"
SKELETON = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SK_Mannequin")
PHYSICS = unreal.load_asset("/Game/Characters/Mannequins/Rigs/PA_Mannequin")
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LOG = []

def log(m):
    LOG.append(str(m)); unreal.log_warning("[ZBODIES] " + str(m))

only = set(filter(None, os.environ.get("ZOMBIE_IMPORT_ONLY", "").split(",")))


def import_mesh(fbx, name):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("create_physics_asset", False)
    ui.set_editor_property("skeleton", SKELETON)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    sk = ui.get_editor_property("skeletal_mesh_import_data")
    sk.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    sk.set_editor_property("import_morph_targets", False)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx)
    task.set_editor_property("destination_path", DEST)
    task.set_editor_property("destination_name", "SKM_" + name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    TOOLS.import_asset_tasks([task])
    mesh = unreal.load_asset("%s/SKM_%s" % (DEST, name))
    if mesh:
        mesh.set_editor_property("physics_asset", PHYSICS)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    return mesh


def make_variant(name, mesh, meta):
    path = "%s/DA_%s" % (DEST, name)
    da = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if not da:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.ZombieBodyVariant)
        da = TOOLS.create_asset("DA_" + name, DEST, unreal.ZombieBodyVariant, factory)
    da.set_editor_property("mesh", mesh)
    da.set_editor_property("archetype", meta["archetype"])
    da.set_editor_property("seed", int(meta["seed"]))
    da.set_editor_property("templates", {unreal.Name(k): unreal.Name(v) for k, v in meta["templates"].items()})
    da.set_editor_property("part_radius", {unreal.Name(k): float(v) for k, v in meta.get("part_radius", {}).items()})
    unreal.EditorAssetLibrary.save_loaded_asset(da)
    return da


count = 0
for meta_path in sorted(glob.glob(os.path.join(SRC, "*.json"))):
    meta = json.load(open(meta_path))
    name = meta["name"]
    if only and name not in only:
        continue
    fbx = os.path.join(SRC, name + ".fbx")
    if not os.path.exists(fbx):
        log("skip %s: no fbx" % name)
        continue
    mesh = import_mesh(fbx, name)
    if not mesh:
        log("!! %s: import failed" % name)
        continue
    make_variant(name, mesh, meta)
    slots = [str(m.get_editor_property("material_slot_name")) for m in mesh.get_editor_property("materials")]
    log("%s: %s, %d slots, templates %s" % (name, meta["archetype"], len(slots),
        ", ".join("%s=%s" % kv for kv in meta["templates"].items() if kv[1] != "normal")))
    count += 1

# The one-off import test is no longer needed.
if unreal.EditorAssetLibrary.does_directory_exist("/Game/Zombie/ImportTest"):
    unreal.EditorAssetLibrary.delete_directory("/Game/Zombie/ImportTest")

log("imported %d bodies into %s" % (count, DEST))
with open(os.path.join(PROJ, "Saved", "import_zombie_bodies.txt"), "w") as f:
    f.write("\n".join(LOG))
