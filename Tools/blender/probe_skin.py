# Tools/blender/probe_skin.py - does an exported body FBX carry real skin weights?
#   blender --background --factory-startup --python probe_skin.py -- <fbx>
import bpy, sys
fbx = sys.argv[sys.argv.index("--") + 1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=fbx, automatic_bone_orientation=False)
for ob in bpy.data.objects:
    print("SKIN obj", ob.name, ob.type, "parent", ob.parent.name if ob.parent else None,
          "mods", [(m.type, getattr(m, "object", None) and m.object.name) for m in ob.modifiers])
    if ob.type == 'MESH':
        me = ob.data
        names = {g.index: g.name for g in ob.vertex_groups}
        counts = {}
        unweighted = 0
        for v in me.vertices:
            if not v.groups:
                unweighted += 1
            for g in v.groups:
                if g.weight > 0.01:
                    counts[names[g.group]] = counts.get(names[g.group], 0) + 1
        top = sorted(counts.items(), key=lambda kv: -kv[1])[:12]
        print("SKIN groups", len(ob.vertex_groups), "verts", len(me.vertices), "unweighted", unweighted)
        print("SKIN top bones", top)
