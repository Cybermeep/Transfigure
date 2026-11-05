# Tools/import_zombie_test.py
#
# Import-test one generated body FBX onto the existing UE5 Mannequin skeleton and report
# what came in (bones, bounds, material slots, vertex colours). Writes Saved/import_test.txt.
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this> -unattended -nosplash -AllowCommandletRendering

import os
import unreal

OUT = []
def log(m):
    OUT.append(str(m)); unreal.log_warning("[ZIMPORT] " + str(m))

proj = unreal.Paths.project_dir()
fbx = os.path.abspath(os.path.join(proj, "Tools", "blender", "output", "Husk_001.fbx"))
dest = "/Game/Zombie/ImportTest"
skeleton = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SK_Mannequin")
log("fbx: %s exists=%s  skeleton=%s" % (fbx, os.path.exists(fbx), skeleton))

ui = unreal.FbxImportUI()
ui.set_editor_property("import_mesh", True)
ui.set_editor_property("import_as_skeletal", True)
ui.set_editor_property("import_materials", False)
ui.set_editor_property("import_textures", False)
ui.set_editor_property("import_animations", False)
ui.set_editor_property("create_physics_asset", False)
ui.set_editor_property("skeleton", skeleton)
ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
sk = ui.get_editor_property("skeletal_mesh_import_data")
sk.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
sk.set_editor_property("import_morph_targets", False)

task = unreal.AssetImportTask()
task.set_editor_property("filename", fbx)
task.set_editor_property("destination_path", dest)
task.set_editor_property("destination_name", "SKM_Husk_001_test")
task.set_editor_property("automated", True)
task.set_editor_property("replace_existing", True)
task.set_editor_property("save", True)
task.set_editor_property("options", ui)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
log("imported: %s" % list(task.get_editor_property("imported_object_paths")))

mesh = unreal.load_asset(dest + "/SKM_Husk_001_test")
if not mesh:
    log("!! no mesh imported")
else:
    log("mesh skeleton: %s" % mesh.skeleton.get_path_name())
    b = mesh.get_bounds()
    log("bounds origin %s extent %s" % (b.origin, b.box_extent))
    mats = mesh.get_editor_property("materials")
    log("material slots (%d): %s" % (len(mats), [str(m.get_editor_property("material_slot_name")) for m in mats]))
    try:
        ref = mesh.get_editor_property("skeleton").get_reference_pose()
        names = [str(n) for n in ref.get_bone_names()]
        log("skeleton bones %d, first: %s" % (len(names), names[:4]))
    except Exception as e:
        log("bone list: %s" % e)
    try:
        lib = unreal.SkeletalMeshEditorSubsystem() if hasattr(unreal, "SkeletalMeshEditorSubsystem") else None
        if lib:
            log("LOD0 verts: %s" % lib.get_num_verts(mesh, 0))
    except Exception as e:
        log("verts: %s" % e)

with open(os.path.join(proj, "Saved", "import_test.txt"), "w") as f:
    f.write("\n".join(OUT))
