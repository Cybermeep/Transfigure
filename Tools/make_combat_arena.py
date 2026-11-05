# Tools/make_combat_arena.py
#
# Builds /Game/Maps/L_CombatArena (COMBAT_LOOP_PLAN C2): an 80 x 80 m arena for the
# wave mode, made of engine shapes.
#   - outer walls 12 m high (wall running), 8 pillars and 4 tall corner towers
#     (grapple anchors, swings, corner whips), 2 raised platforms with ramps, low cover
#   - 11 enemy spawn points (TargetPoints tagged EnemySpawn) along the walls
#   - a NavMeshBoundsVolume (the nav mesh builds at runtime: RuntimeGeneration=Dynamic)
#   - movable sun, sky light, sky atmosphere, light fog (no lighting build needed)
#   - an ACombatWaveDirector (endless waves) and a PlayerStart
# Surfaces use /Game/CombatArena/M_ArenaSurface: a world-aligned 2 m checker with a Tint.
#
# Re-runnable: deletes and rebuilds the map and its materials. Other maps are untouched.
# Run (editor closed):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this> -unattended -nosplash
# Output: Saved/make_combat_arena.txt

import math
import os
import unreal

MAP = "/Game/Maps/L_CombatArena"
MAT_DIR = "/Game/CombatArena"
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LOG = []


def log(m):
    LOG.append(str(m))
    unreal.log_warning("[ARENA] " + str(m))


# ---------------------------------------------------------------- materials
SURFACES = {
    "MI_ArenaFloor": (0.08, 0.09, 0.13),
    "MI_ArenaWall": (0.46, 0.37, 0.28),
    "MI_ArenaPillar": (0.14, 0.33, 0.35),
    "MI_ArenaTower": (0.30, 0.22, 0.42),
    "MI_ArenaPlatform": (0.33, 0.35, 0.42),
    "MI_ArenaCover": (0.42, 0.30, 0.21),
}


def make_materials():
    for name in list(SURFACES) + ["M_ArenaSurface"]:
        if EAL.does_asset_exist(MAT_DIR + "/" + name):
            EAL.delete_asset(MAT_DIR + "/" + name)
    m = TOOLS.create_asset("M_ArenaSurface", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())

    def node(cls, x, y, **props):
        e = MEL.create_material_expression(m, cls, x, y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    # checker = frac((floor(P.x/200) + floor(P.y/200) + floor(P.z/200)) * 0.5) * 2   -> 0 or 1
    tint = node(unreal.MaterialExpressionVectorParameter, -500, -100, parameter_name="Tint",
                default_value=unreal.LinearColor(0.4, 0.4, 0.45, 1.0))
    wp = node(unreal.MaterialExpressionWorldPosition, -1400, 200)
    div = node(unreal.MaterialExpressionDivide, -1250, 200, const_b=200.0)
    MEL.connect_material_expressions(wp, "", div, "A")
    flo = node(unreal.MaterialExpressionFloor, -1100, 200)
    MEL.connect_material_expressions(div, "", flo, "")
    ones = node(unreal.MaterialExpressionConstant3Vector, -1100, 320, constant=unreal.LinearColor(1.0, 1.0, 1.0, 0.0))
    dot = node(unreal.MaterialExpressionDotProduct, -950, 200)
    MEL.connect_material_expressions(flo, "", dot, "A")
    MEL.connect_material_expressions(ones, "", dot, "B")
    half = node(unreal.MaterialExpressionMultiply, -800, 200, const_b=0.5)
    MEL.connect_material_expressions(dot, "", half, "A")
    frac = node(unreal.MaterialExpressionFrac, -650, 200)
    MEL.connect_material_expressions(half, "", frac, "")
    two = node(unreal.MaterialExpressionMultiply, -520, 200, const_b=2.0)
    MEL.connect_material_expressions(frac, "", two, "A")
    shade = node(unreal.MaterialExpressionLinearInterpolate, -380, 120, const_a=0.82, const_b=1.0)
    MEL.connect_material_expressions(two, "", shade, "Alpha")
    col = node(unreal.MaterialExpressionMultiply, -200, 0)
    MEL.connect_material_expressions(tint, "", col, "A")
    MEL.connect_material_expressions(shade, "", col, "B")
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = node(unreal.MaterialExpressionConstant, -200, 200, r=0.85)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m, False)

    out = {}
    for name, c in SURFACES.items():
        mi = TOOLS.create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, m)
        MEL.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(c[0], c[1], c[2], 1.0))
        EAL.save_loaded_asset(mi, False)
        out[name] = mi
    log("materials: M_ArenaSurface + %d instances" % len(out))
    return out


# ---------------------------------------------------------------- map
if EAL.does_asset_exist(MAP):
    EAL.delete_asset(MAP)
mats = make_materials()

world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
subsys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")
counts = {}


def rot(pitch=0.0, yaw=0.0, roll=0.0):
    return unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)


def block(label, center, size, mat, r=None, folder="Arena"):
    a = subsys.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center), r or rot())
    a.set_actor_label(label)
    smc = a.static_mesh_component
    smc.set_static_mesh(CUBE)
    smc.set_material(0, mats[mat])
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    a.set_folder_path(folder)
    counts[folder] = counts.get(folder, 0) + 1
    return a


H = 4000.0          # half size of the playable floor
WALL_H = 1200.0

# Floor and outer walls.
block("Floor", (0, 0, -50), (2 * H + 400, 2 * H + 400, 100), "MI_ArenaFloor")
block("Wall_N", (0, H + 100, WALL_H / 2), (2 * H + 400, 200, WALL_H), "MI_ArenaWall")
block("Wall_S", (0, -H - 100, WALL_H / 2), (2 * H + 400, 200, WALL_H), "MI_ArenaWall")
block("Wall_E", (H + 100, 0, WALL_H / 2), (200, 2 * H, WALL_H), "MI_ArenaWall")
block("Wall_W", (-H - 100, 0, WALL_H / 2), (200, 2 * H, WALL_H), "MI_ArenaWall")

# Ring of pillars (wall run, swing, corner whip).
for i in range(8):
    a = math.radians(22.5 + 45.0 * i)
    block("Pillar_%d" % i, (1600 * math.cos(a), 1600 * math.sin(a), 800), (400, 400, 1600), "MI_ArenaPillar")

# Tall corner towers (high grapple anchors).
for i, (sx, sy) in enumerate([(1, 1), (-1, 1), (-1, -1), (1, -1)]):
    block("Tower_%d" % i, (2900 * sx, 2900 * sy, 1200), (600, 600, 2400), "MI_ArenaTower")

# Two raised platforms (5 m up) with ramps toward the middle.
slope = math.atan2(500.0, 1100.0)
ramp_len = math.hypot(500.0, 1100.0)
for side in (1, -1):
    block("Platform_%s" % ("E" if side > 0 else "W"), (2600 * side, 0, 250), (1200, 1600, 500), "MI_ArenaPlatform")
    # Top surface runs from (900, 0) on the ground to (2000, 500) at the platform edge. The slab is
    # 5 m thick so it fills the space under itself (a thin ramp leaves a low wedge that zombies,
    # taller than the nav mesh agent, path into and get stuck in).
    thick = 500.0
    cx = (1450.0 + 0.5 * thick * math.sin(slope)) * side
    cz = 250.0 - 0.5 * thick * math.cos(slope)
    block("Ramp_%s" % ("E" if side > 0 else "W"), (cx, 0, cz), (ramp_len, 500, thick), "MI_ArenaPlatform",
          rot(pitch=math.degrees(slope), yaw=0.0 if side > 0 else 180.0))

# Low cover (jump over, slide behind).
for i, (x, y, yaw) in enumerate([(0, 900, 0), (0, -900, 0), (2200, 2000, 45), (-2200, -2000, 45),
                                 (2200, -2100, -30), (-2200, 2100, -30)]):
    block("Cover_%d" % i, (x, y, 90), (600, 100, 180), "MI_ArenaCover", rot(yaw=yaw))

# Enemy spawn points along the walls.
spawns = [(-2200, 3650), (0, 3650), (2200, 3650), (-2200, -3650), (2200, -3650),
          (3650, -2000), (3650, 0), (3650, 2000), (-3650, -2000), (-3650, 0), (-3650, 2000)]
for i, (x, y) in enumerate(spawns):
    t = subsys.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(x, y, 0), rot())
    t.set_actor_label("EnemySpawn_%d" % i)
    t.set_editor_property("tags", [unreal.Name("EnemySpawn")])
    t.set_folder_path("Spawns")

# Player start near the south wall, facing north.
ps = subsys.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, -3000, 100), rot(yaw=90.0))
ps.set_actor_label("PlayerStart")

# Nav mesh bounds (the volume factory gives it a 200 cm box brush; scale it to the arena).
nav = subsys.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0, 0, 600), rot())
nav.set_actor_label("NavBounds")
nav.set_actor_scale3d(unreal.Vector((2 * H + 200) / 200.0, (2 * H + 200) / 200.0, 2400.0 / 200.0))
nav_origin, nav_extent = nav.get_actor_bounds(False)
log("nav bounds extent %s" % nav_extent)

# Lighting: everything movable, so there's nothing to build.
sun = subsys.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000), rot(pitch=-48.0, yaw=35.0))
sun.set_actor_label("Sun")
sun_c = sun.get_component_by_class(unreal.DirectionalLightComponent)
sun_c.set_mobility(unreal.ComponentMobility.MOVABLE)
sun_c.set_editor_property("atmosphere_sun_light", True)
sun_c.set_editor_property("intensity", 6.0)
sky = subsys.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 600), rot())
sky.set_actor_label("SkyLight")
sky_c = sky.get_component_by_class(unreal.SkyLightComponent)
sky_c.set_mobility(unreal.ComponentMobility.MOVABLE)
sky_c.set_editor_property("real_time_capture", True)
atmo = subsys.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), rot())
atmo.set_actor_label("SkyAtmosphere")
fog = subsys.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, -100), rot())
fog.set_actor_label("Fog")
fog.get_component_by_class(unreal.ExponentialHeightFogComponent).set_editor_property("fog_density", 0.004)
for a in (sun, sky, atmo, fog):
    a.set_folder_path("Lighting")

# Endless waves.
director = subsys.spawn_actor_from_class(unreal.CombatWaveDirector, unreal.Vector(0, 0, 100), rot())
director.set_actor_label("WaveDirector")

unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
log("saved %s: %s, %d spawn points" % (MAP, ", ".join("%s %d" % kv for kv in sorted(counts.items())), len(spawns)))

with open(os.path.join(unreal.Paths.project_saved_dir(), "make_combat_arena.txt"), "w") as f:
    f.write("\n".join(LOG) + "\n")
