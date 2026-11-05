# Tools/export_mannequin_fbx.py
#
# Exports the UE5 Mannequin (skeleton + reference mesh) to Tools/blender/input/SKM_Manny_Simple.fbx,
# so the Blender body generator builds around exactly the same bones.
# Run headless (editor closed):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nosplash

import os
import unreal

OUT_DIR = os.path.join(unreal.Paths.project_dir(), "Tools", "blender", "input")
os.makedirs(OUT_DIR, exist_ok=True)
out_file = os.path.join(OUT_DIR, "SKM_Manny_Simple.fbx")

task = unreal.AssetExportTask()
task.object = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
task.filename = out_file
task.automated = True
task.prompt = False
task.replace_identical = True

opts = unreal.FbxExportOption()
opts.set_editor_property("ascii", False)
opts.set_editor_property("collision", False)
opts.set_editor_property("level_of_detail", False)
task.options = opts

ok = unreal.Exporter.run_asset_export_task(task)
unreal.log_warning("[EXPORT] %s -> %s (%s)" % (task.object.get_path_name(), out_file, ok))
