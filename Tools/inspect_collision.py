# Tools/inspect_collision.py
#
# Read-only: reports collision setup for every StaticMeshActor in L_TestArena.
# Output: Saved/inspect_collision.txt

import os
import unreal

OUT = []


def log(m):
    OUT.append(str(m))
    unreal.log_warning("[INSPECT] " + str(m))


def v(x):
    return "(%.0f, %.0f, %.0f)" % (x.x, x.y, x.z)


unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Maps/L_TestArena")
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if not isinstance(a, unreal.StaticMeshActor):
        continue
    comp = a.static_mesh_component
    mesh = comp.static_mesh
    origin, extent = a.get_actor_bounds(False)
    log("== %s  mesh=%s" % (a.get_actor_label(), mesh.get_path_name() if mesh else None))
    log("   bounds center=%s half-size=%s" % (v(origin), v(extent)))
    log("   component: profile=%s collisionEnabled=%s objectType=%s mobility=%s" % (
        comp.get_collision_profile_name(), comp.get_collision_enabled(),
        comp.get_collision_object_type(), comp.mobility))
    log("   response to Pawn=%s  WorldStatic=%s  Visibility=%s" % (
        comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN),
        comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_WORLD_STATIC),
        comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY)))
    if mesh:
        try:
            log("   simple collision shapes=%d  trace flag=%s" % (
                unreal.EditorStaticMeshLibrary.get_simple_collision_count(mesh),
                unreal.EditorStaticMeshLibrary.get_collision_complexity(mesh)))
        except Exception as e:
            log("   simple collision: <n/a %s>" % e)

with open(os.path.join(unreal.Paths.project_saved_dir(), "inspect_collision.txt"), "w") as f:
    f.write("\n".join(OUT))
