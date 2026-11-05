# Transfigure — notes for Claude sessions

UE 5.6.1 C++ project (Windows). Stylized space high-fantasy on a ring world.
GitHub: Cybermeep/Transfigure. The owner is not a C++ expert: explain fixes plainly and keep updates short.

## Build (editor must be closed)

```
"C:\Program Files\Epic Games\UE_5.6\Engine\Build\BatchFiles\Build.bat" TransfigureEditor Win64 Development -Project="C:\Users\Lynette\Desktop\Unreal Projects\Transfigure\Transfigure.uproject" -WaitMutex
```

- After changing any header, USTRUCT or UCLASS: full build like this with the editor closed. Never rely on Live Coding for those.
- Build.bat fails (exit code 6) while the editor is open. Ask the owner to close it, and tell them to wait for "build succeeded" before reopening — otherwise they test the previous DLL.
- Check `Binaries/Win64/UnrealEditor-Transfigure.dll` is newer than the edited source. If the build fails, the editor silently keeps running the OLD dll, so in-game behaviour won't reflect the code.

## Logs

- Editor/PIE log: `Saved/Logs/Transfigure.log` (older sessions: `Transfigure-backup-*.log`).
- Diagnostic lines are prefixed `[TM]` (`ATMCharacter::BeginPlay`, `NotifyControllerChanged`, once per second in `Tick`).
- UBT log: `%LOCALAPPDATA%\UnrealBuildTool\Log.txt`.

## Inspecting Blueprints / maps (binary assets)

Python Editor Script Plugin is enabled. Scripts live in `Tools/`. Run headless (editor closed):

```
"C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "C:\Users\Lynette\Desktop\Unreal Projects\Transfigure\Transfigure.uproject" -run=pythonscript -script="C:\Users\Lynette\Desktop\Unreal Projects\Transfigure\Tools\inspect_setup.py" -unattended -nosplash
```

`Tools/inspect_setup.py` writes `Saved/inspect_setup.txt` (game mode, pawn, input, components, map actors).

Never edit `.uasset` / `.umap` as text. Inspect with Python, then give the owner exact editor clicks.

Exporting a skeletal mesh (FBX) from a commandlet needs `-AllowCommandletRendering`, otherwise it asserts (`MeshObject`).

## Blender zombie body pipeline (`Tools/blender/`)

Blender 5.0: `C:\Program Files\Blender Foundation\Blender 5.0\blender.exe`. Always run headless with `--background --factory-startup`.

1. `Tools/export_mannequin_fbx.py` (UE commandlet + `-AllowCommandletRendering`) writes `Tools/blender/input/SKM_Manny_Simple.fbx`.
2. `blender --background --factory-startup --python Tools/blender/zombie_body_gen.py -- --fbx Tools/blender/input/SKM_Manny_Simple.fbx --archetype Husk|Bloat|Gaunt --seed N --out Tools/blender/output` → `<Arch>_<NNN>.blend` (owner can sculpt it), `.fbx`, and `output/previews/*.png`. Takes ~3 s plus ~10 s of renders.
3. `python Tools/blender/montage.py <out.png> front|threequarter|side Husk_001 Bloat_001 ...` makes a comparison sheet.
4. `python Tools/blender/batch_generate.py --archetypes Husk Bloat Gaunt --count 6 --jobs 8` generates many bodies in parallel (18 in ~35 s on the owner's 28-thread / 128 GB PC) and writes `output/previews/batch_threequarter.png`.

5. `Tools/import_zombie_bodies.py` (UE commandlet + `-AllowCommandletRendering`, editor closed) imports every `output/*.fbx` as `/Game/Zombie/Bodies/SKM_<name>` (SK_Mannequin skeleton, PA_Mannequin physics) plus a `DA_<name>` `UZombieBodyVariant`. `AProceduralZombie` (`bUseBakedBodies`) picks a variant for its form by the asset-registry `Archetype` tag; severing hides region material sections and spawns `ASeveredBodyPart`. `Tools/create_zombie_materials.py` rebuilds the materials. Bodies (~146 MB for 18) are regenerable, so not committed unless the owner asks.

Templates (rolled per region, weighted per archetype, force with `--set arm_L=claws`): arm normal/withered/swollen/tumorous/blade/claws/tendrils/machine; leg normal/withered/swollen/tumorous/spiked/machine; head normal/bulbous/crown/cluster_eyes/machine_plate; torso normal/hump/spine_spikes/open_ribs/back_tendrils/machine_core. Organic features are blob chains fused by the remesh and bone-locked in the weights; machine parts (`zombie_machine.py`) are crisp hard-surface meshes on a `Metal` slot. Each body's `.json` lists its templates. Seeds are deterministic (never use Python `hash()` on strings for seeding — it is randomised per process).

Gotchas: the imported mesh is in **centimetres** under a 0.01-scale parent empty — do geometry in local space, never delete that empty (deleting it once made the remesh try to allocate 65 GB). The mesh duplicates vertices on UV seams — displacements must use a shared normal per position or seams crack and the voxel remesh shreds the body. The generator refuses to remesh above `--max-voxels`. Outputs are git-ignored (regenerated in seconds).

## Layout (`Source/Transfigure/`)

- `Ringworld/` — spin axis world +Y, ring in XZ. `URingWorldGeometry`, `URingFrameUtils`, `FRingWorldSpec` in `URingWorldSettings` (`Config/DefaultGame.ini`). `bFlatWorldForTesting=True` makes gravity -Z for flat test maps.
  - Ring maps: an `ARingBandGenerator` (`RingBand`, at the world origin) in a map switches that map to curved ring gravity while it plays (overrides the flag, restored on EndPlay), draws the whole ring, and spawns `ARingNearField`: a 2 km collision tile under the player, re-spawned (not moved) when they near its edge. Tiles exist only during play.
  - Regions (`Docs/WORLDGEN_PLAN.md`): `FRingRegions` (`RingRegions.h`, plain C++, cached per seed) gives every (arc, lateral) a region (biome, name, danger), the terrain height (`Height`, cm, >= 0) and colours. The near field's tile is a 4 km procedural-mesh height field built from it. The band overhead is coloured by it. `ARingContentStreamer` places biome structures and zombies on it, tracks the current region (HUD banner) and tints the fog and sun. Flat spots (authored zones, pads, landing spots) are registered with `FRingRegions::AddFlatten` and reset by `ARingBandGenerator::BeginPlay`. Never use `FMath::PerlinNoise` there (it repeats every 256 units); use `RingNoise`.
  - `Tools/create_ring_materials.py` makes `/Game/RingWorld/M_RingGround` and `M_RingBand` (vertex-colour materials). The vertex colours are linear values written straight in.
  - Ring test (editor closed, ~50 s): `UnrealEditor.exe <uproject> /Game/Maps/L_RingArena -game -TMRingTest -windowed -ResX=1280 -ResY=720 -unattended -nosound -ABSLOG=<project>/Saved/Logs/RingTest.log`. Expect `grounded 1` in every biome and `RING test done (8 biomes visited)`. Screenshots `Ring_*.png`.
  - `/Game/Maps/L_RingArena` = L_TestArena placed 20° round the ring (so down is tilted in world space). Rebuild it with `Tools/make_ring_arena.py` (editor closed) after changing L_TestArena; then Build Paths in the editor if enemies need the navmesh.
- `Character/` `ATMCharacter` + `StateMachine/`, `States/TMState_*`. Actor yaw and camera pitch are driven in `UpdateRingGravity`, NOT by ControlRotation.
- `Enemy/`, `Grapple/`, `Transfiguration/` (spells/sigils), `Temporal/`, `Combat/`.
- `Limb/` — procedural limb system. `FLimbMeshGenerator` (math-generated parts), `AProceduralZombie` (parts on a hidden UE5 Mannequin skeleton, capsule hit zones, severing through `TakePoolDamage`, ragdoll corpses), `ASeveredLimb` (physics limb on ring gravity). Needs the Mannequin in `/Game/Characters/Mannequins`.
- `Character/TMHUD` — placeholder canvas HUD (crosshair, status, hold-Q spell wheel).
- `Destruction/` — known non-functional; leave it (`ChunkPoolSize = 0`).
- Test map: `/Game/Maps/L_TestArena`. Placeholder assets only (cubes, engine shapes, UE5 Mannequin).

## Spells, combos, gun (`Transfiguration/`, `Combat/`; plan and status in `Docs/SPELL_PLAN.md`)

- Each spell is a `UTransfigurationDefinition` data asset; `EffectClass` picks an archetype in `Effects/SpellEffects` (Area, Line, Chain, Pull, Structure, Self). All spell damage goes through `SpellHit::` (`SpellHitLibrary`); spells that last run in `ASpellZone`. Visuals are `ASpellFX` (engine shapes + `M_SpellGlow`, which fades within ~3.6 m of the camera; `SetNearFade(0)` turns that off).
- `Tools/create_spells.py` (editor closed, run like `inspect_setup.py`) rebuilds every `DA_<Spell>`, the combo assets, `M_SpellGlow`, `IA_Spellbook` (key B) and `DA_SpellRegistry`. Expect `registry: 38 spells (saved: 38)`. Change the SPELLS / COMBOS tables, not the assets. It deletes and recreates `M_SpellGlow`, so a failed run leaves it missing until it's run again.
- Combos: `UTransfigurationCastingComponent::TryFireCombo` (the old `UTransfigurationComboDetector` no longer ticks).
- Gun: `UWeaponComponent` (hold to fire, `FireRate`, recoil through `ATMCharacter::AddRecoil`). Headshots: `ABaseEnemy::IsHeadHit` (zombies: the nearest part is the head).
- Smoke test (editor closed, ~2 min): `UnrealEditor.exe <uproject> /Game/Maps/L_TestArena -game -TMSpellTest -windowed -ResX=1280 -ResY=720 -unattended -nosound -ABSLOG=<project>/Saved/Logs/SmokeTest.log`. Casts every spell and combo and holds the trigger on zombies, logs `[TM] SMOKE ...`, saves `Saved/Screenshots/WindowsEditor/Smoke_*.png`, quits. Must end with `spell test done (27 spells, 11 combos)`.

## Combat arena (`Docs/COMBAT_LOOP_PLAN.md`)

- `/Game/Maps/L_CombatArena` is generated by `Tools/make_combat_arena.py` (editor closed; re-runnable; makes `/Game/CombatArena/M_ArenaSurface` + MIs). Edit the script, not the map.
- `ACombatWaveDirector` (`Combat/`, placed in the map) runs endless waves from TargetPoints tagged `EnemySpawn`, keeps score, unsticks enemies, logs `[TM] WAVE ...` / `[TM] RUN over ...`. The HUD finds it with `ACombatWaveDirector::Find`.
- Player death: `ATMCharacter::HandleDeath` (input off, 0.3 slow-mo, death screen); click / Space / R → `RestartRun` reloads the map. `bGodModeForTesting` ignores damage.
- Nav mesh is built at runtime (`[/Script/NavigationSystem.RecastNavMesh] RuntimeGeneration=Dynamic`): a map only needs a NavMeshBoundsVolume. Python-spawned volumes get a 200 cm box brush (scale it).
- Arena test (editor closed, ~90 s): `UnrealEditor.exe <uproject> /Game/Maps/L_CombatArena -game -TMArenaTest -windowed -ResX=1280 -ResY=720 -unattended -nosound -ABSLOG=<project>/Saved/Logs/ArenaTest.log`. Auto-fights 30 s, stops defending, dies, restarts once, quits. Expect `WAVE 1 cleared`, `RUN over`, `restart worked`, and no `no nav path` lines. Screenshots `Arena_0*.png`.

## Plan and working rules

- `Docs/FIX_PLAN.md` — phases 0–5, then "Ready for the limb system". Line numbers there refer to the original commit.
- One phase at a time: implement → build → give the owner a short in-editor test list → read the log after they test → commit.
- Skip "Fixes to leave for later". Don't start the limb system until the checklist passes and the owner says go.
- Commit before any large change.
- Severed parts / anything physical must use ring down (`URingFrameUtils::GetDownFor`), never world -Z.
