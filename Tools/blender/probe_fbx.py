# Tools/blender/probe_fbx.py - print what Blender sees in the exported Mannequin FBX.
# blender --background --factory-startup --python probe_fbx.py -- <fbx>
import bpy, sys

fbx = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=fbx, automatic_bone_orientation=False)

for ob in bpy.data.objects:
    print("OBJ", ob.name, ob.type, "scale", tuple(round(v, 4) for v in ob.scale),
          "rot", tuple(round(v, 4) for v in ob.rotation_euler), "dims", tuple(round(v, 3) for v in ob.dimensions))
    if ob.type == 'ARMATURE':
        arm = ob
        print("BONES", len(arm.data.bones))
        for name in ["root", "pelvis", "spine_05", "neck_01", "head", "upperarm_l", "hand_l", "index_01_l",
                     "thigh_l", "calf_l", "foot_l", "ball_l"]:
            b = arm.data.bones.get(name)
            if b:
                hw = arm.matrix_world @ b.head_local
                tw = arm.matrix_world @ b.tail_local
                print("  %-12s head %s tail %s" % (name, tuple(round(v, 3) for v in hw), tuple(round(v, 3) for v in tw)))
    if ob.type == 'MESH':
        print("  verts", len(ob.data.vertices), "groups", len(ob.vertex_groups),
              "mods", [m.type for m in ob.modifiers], "parent", ob.parent.name if ob.parent else None)
