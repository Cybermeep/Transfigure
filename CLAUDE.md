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

## Layout (`Source/Transfigure/`)

- `Ringworld/` — spin axis world +Y, ring in XZ. `URingWorldGeometry`, `URingFrameUtils`, `FRingWorldSpec` in `URingWorldSettings` (`Config/DefaultGame.ini`). `bFlatWorldForTesting=True` makes gravity -Z for flat test maps.
- `Character/` `ATMCharacter` + `StateMachine/`, `States/TMState_*`. Actor yaw and camera pitch are driven in `UpdateRingGravity`, NOT by ControlRotation.
- `Enemy/`, `Grapple/`, `Transfiguration/` (spells/sigils), `Temporal/`, `Combat/`.
- `Destruction/` — known non-functional; leave it (`ChunkPoolSize = 0`).
- Test map: `/Game/Maps/L_TestArena`. Placeholder assets only (cubes, engine shapes, UE5 Mannequin).

## Plan and working rules

- `Docs/FIX_PLAN.md` — phases 0–5, then "Ready for the limb system". Line numbers there refer to the original commit.
- One phase at a time: implement → build → give the owner a short in-editor test list → read the log after they test → commit.
- Skip "Fixes to leave for later". Don't start the limb system until the checklist passes and the owner says go.
- Commit before any large change.
- Severed parts / anything physical must use ring down (`URingFrameUtils::GetDownFor`), never world -Z.
