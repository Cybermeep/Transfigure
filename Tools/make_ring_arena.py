# Tools/make_ring_arena.py
#
# Builds /Game/Maps/L_RingArena: a copy of L_TestArena placed ON the ring world,
# plus an ARingBandGenerator at the world origin (which turns curved ring gravity
# on for that map while it plays, draws the whole ring, and keeps a collidable
# floor under the player).
#
# The arena is put RING_THETA_DEG around the ring from the bottom (theta = 0), so
# "down" is genuinely tilted in world space there and any code that still assumes
# world -Z is down shows up straight away. Every top-level actor's transform is
# re-expressed in that spot's surface frame (Forward = spinward, Right = lateral,
# Up = toward the spin axis); the arena itself is unchanged relative to its floor.
#
# Re-runnable: deletes and rebuilds L_RingArena each time. L_TestArena is untouched.
#
# Run (editor closed):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this> -unattended -nosplash
# Output: Saved/make_ring_arena.txt

import math
import os
import re
import unreal

SRC = "/Game/Maps/L_TestArena"
DST = "/Game/Maps/L_RingArena"
RING_THETA_DEG = 20.0

LOG = []


def log(m):
    LOG.append(str(m))
    unreal.log_warning("[RINGARENA] " + str(m))


# Ring radius from Project Settings (Config/DefaultGame.ini), default 1,600 km.
R = 1.6e8
_ini = os.path.join(unreal.Paths.project_config_dir(), "DefaultGame.ini")
if os.path.exists(_ini):
    _m = re.search(r"Spec=\(.*?Radius=([0-9.eE+-]+)", open(_ini, encoding="utf-8", errors="ignore").read())
    if _m:
        R = float(_m.group(1))
theta = math.radians(RING_THETA_DEG)

# Surface frame at (arc = R * theta, lateral 0, altitude 0); matches URingWorldGeometry.
origin = unreal.Vector(R * math.sin(theta), 0.0, -R * math.cos(theta))
# Forward (cos, 0, sin) / Up (-sin, 0, cos) = the identity basis pitched up by theta.
frame = unreal.Transform(location=origin, rotation=unreal.Rotator(roll=0.0, pitch=RING_THETA_DEG, yaw=0.0),
                         scale=unreal.Vector(1, 1, 1))
log("ring radius %.0f km, arena at theta %.1f deg -> world %s" % (R / 1e5, RING_THETA_DEG, origin))

if unreal.EditorAssetLibrary.does_asset_exist(DST):
    unreal.EditorAssetLibrary.delete_asset(DST)
if not unreal.EditorAssetLibrary.duplicate_asset(SRC, DST):
    raise RuntimeError("could not duplicate " + SRC)

unreal.EditorLoadingAndSavingUtils.load_map(DST)
subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()

moved = 0
for actor in subsys.get_all_level_actors():
    if isinstance(actor, (unreal.WorldSettings, unreal.Brush)) and not isinstance(actor, unreal.Volume):
        continue
    root = actor.get_editor_property("root_component")
    if root is None or root.get_attach_parent() is not None:
        continue   # attached actors follow their parent
    old = actor.get_actor_transform()
    actor.set_actor_transform(old.multiply(frame), False, True)
    moved += 1

    # Height fog is left as authored. It thins with world Z, and at a 20 degree tilt
    # "up" is still mostly world Z, so looking up at the ring it thins out as normal.
    # (Making it uniform hid the ring completely. A ring-aware fog is a later job.)
    # Sky atmosphere: put the planet under the arena instead of under the world origin.
    if isinstance(actor, unreal.SkyAtmosphere):
        comp = actor.get_editor_property("sky_atmosphere_component")
        comp.set_editor_property("transform_mode", unreal.SkyAtmosphereTransformMode.PLANET_TOP_AT_COMPONENT_TRANSFORM)
        log("sky atmosphere anchored to the arena")
log("moved %d actors onto the ring" % moved)

band = subsys.spawn_actor_from_class(unreal.RingBandGenerator, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
band.set_actor_label("RingBand")
log("added RingBand at the world origin")

# A launch pad 8 m in front of the player start (arena-local), on the floor.
pad_local = unreal.Transform(location=unreal.Vector(800.0, 0.0, 10.0), rotation=unreal.Rotator(0, 0, 0),
                             scale=unreal.Vector(1, 1, 1))
pad_world = pad_local.multiply(frame)
pad = subsys.spawn_actor_from_class(unreal.RingLaunchPad, pad_world.translation, pad_world.rotation.rotator())
pad.set_actor_label("LaunchPad")
pad.set_editor_property("pad_name", "Arena")
log("added LaunchPad 'Arena' in front of the player start")

# The arena is hand-built: no procedural content within 3 km of it.
zone = subsys.spawn_actor_from_class(unreal.RingAuthoredZone, frame.translation, frame.rotation.rotator())
zone.set_actor_label("ArenaAuthoredZone")
zone.set_editor_property("radius", 300000.0)
log("added authored zone (3 km) over the arena")

# KillZ / world bounds are world-Z checks: meaningless on a ring 1,600 km from the origin.
ws = world.get_world_settings()
ws.set_editor_property("enable_world_bounds_checks", False)
log("world bounds checks off")

unreal.EditorLoadingAndSavingUtils.save_map(world, DST)
log("saved " + DST)

with open(os.path.join(unreal.Paths.project_saved_dir(), "make_ring_arena.txt"), "w") as f:
    f.write("\n".join(LOG))
