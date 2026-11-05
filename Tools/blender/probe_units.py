# Tools/blender/probe_units.py - check local vs world units of the imported Mannequin mesh.
import bpy, sys
fbx = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=fbx, automatic_bone_orientation=False)
for ob in bpy.data.objects:
    print("UNITS", ob.name, ob.type, "parent", ob.parent.name if ob.parent else None,
          "world scale", tuple(round(v, 5) for v in ob.matrix_world.to_scale()),
          "local scale", tuple(round(v, 5) for v in ob.scale))
    if ob.type == 'MESH':
        zs = [v.co.z for v in ob.data.vertices]
        print("UNITS   local z range", round(min(zs), 4), round(max(zs), 4))
