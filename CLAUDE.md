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

Templates (rolled per region, weighted per archetype, force with `--set arm_L=claws`): arm normal/withered/swollen/tumorous/blade/claws/tendrils/machine; leg normal/withered/swollen/tumorous/spiked/machine; head normal/bulbous/crown/cluster_eyes/machine_plate; torso normal/hump/spine_spikes/open_ribs/back_tendrils/machine_core. Organic features are blob chains fused by the remesh and bone-locked in the weights; machine parts (`zombie_machine.py`) are crisp hard-surface meshes on a `Metal` slot. Each body's `.json` lists its templates. Seeds are deterministic (never use Python `hash()` on strings for seeding — it is randomised per process).

Gotchas: the imported mesh is in **centimetres** under a 0.01-scale parent empty — do geometry in local space, never delete that empty (deleting it once made the remesh try to allocate 65 GB). The mesh duplicates vertices on UV seams — displacements must use a shared normal per position or seams crack and the voxel remesh shreds the body. The generator refuses to remesh above `--max-voxels`. Outputs are git-ignored (regenerated in seconds).

## Layout (`Source/Transfigure/`)

- `Ringworld/` — spin axis world +Y, ring in XZ. `URingWorldGeometry`, `URingFrameUtils`, `FRingWorldSpec` in `URingWorldSettings` (`Config/DefaultGame.ini`). `bFlatWorldForTesting=True` makes gravity -Z for flat test maps.
- `Character/` `ATMCharacter` + `StateMachine/`, `States/TMState_*`. Actor yaw and camera pitch are driven in `UpdateRingGravity`, NOT by ControlRotation.
- `Enemy/`, `Grapple/`, `Transfiguration/` (spells/sigils), `Temporal/`, `Combat/`.
- `Limb/` — procedural limb system. `FLimbMeshGenerator` (math-generated parts), `AProceduralZombie` (parts on a hidden UE5 Mannequin skeleton, capsule hit zones, severing through `TakePoolDamage`, ragdoll corpses), `ASeveredLimb` (physics limb on ring gravity). Needs the Mannequin in `/Game/Characters/Mannequins`.
- `Character/TMHUD` — placeholder canvas HUD (crosshair, status, hold-Q spell wheel).
- `Destruction/` — known non-functional; leave it (`ChunkPoolSize = 0`).
- Test map: `/Game/Maps/L_TestArena`. Placeholder assets only (cubes, engine shapes, UE5 Mannequin).

## Plan and working rules

- `Docs/FIX_PLAN.md` — phases 0–5, then "Ready for the limb system". Line numbers there refer to the original commit.
- One phase at a time: implement → build → give the owner a short in-editor test list → read the log after they test → commit.
- Skip "Fixes to leave for later". Don't start the limb system until the checklist passes and the owner says go.
- Commit before any large change.
- Severed parts / anything physical must use ring down (`URingFrameUtils::GetDownFor`), never world -Z.
