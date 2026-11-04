# Tools/inspect_setup.py
#
# Read-only report on the Blueprints/map that decide whether the player spawns,
# gets possessed and receives input. Saves nothing.
#
# Run with the editor closed:
#   "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
#       "<path>\Transfigure.uproject" -run=pythonscript -script="<path>\Tools\inspect_setup.py"
#
# Output goes to the log and to Saved/inspect_setup.txt.

import os
import unreal

OUT = []


def log(msg=""):
    OUT.append(str(msg))
    unreal.log_warning("[INSPECT] " + str(msg))


def name_of(obj):
    if obj is None:
        return "None"
    try:
        return obj.get_path_name()
    except Exception:
        return str(obj)


def prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception as e:
        return "<n/a: %s>" % e


def cdo(bp_path):
    cls = unreal.EditorAssetLibrary.load_blueprint_class(bp_path)
    if not cls:
        log("!! could not load blueprint class %s" % bp_path)
        return None, None
    return cls, unreal.get_default_object(cls)


def section(title):
    log("")
    log("=== %s ===" % title)


# ---------------------------------------------------------------- Game mode
section("GM_Transfiguration")
gm_cls, gm = cdo("/Game/GM_Transfiguration")
if gm:
    log("is GameModeBase     : %s  is GameMode: %s" % (isinstance(gm, unreal.GameModeBase),
                                                       isinstance(gm, unreal.GameMode)))
    for p in ["default_pawn_class", "player_controller_class", "hud_class",
              "player_state_class", "game_state_class", "spectator_class",
              "start_players_as_spectators"]:
        log("%-20s: %s" % (p, name_of(prop(gm, p)) if not isinstance(prop(gm, p), (bool, str)) else prop(gm, p)))

# ---------------------------------------------------------------- Game instance
section("BP_TransfigurationGameInstance")
gi_cls, gi = cdo("/Game/BP_TransfigurationGameInstance")
if gi:
    log("spell_registry      : %s" % name_of(prop(gi, "spell_registry")))

# ---------------------------------------------------------------- Character
section("BP_TMCharacter")
ch_cls, ch = cdo("/Game/Characters/BP_TMCharacter")
if ch:
    log("class               : %s" % name_of(ch_cls))
    sup = ch_cls
    chain = []
    try:
        while sup:
            chain.append(sup.get_name())
            sup = sup.get_super_class() if hasattr(sup, "get_super_class") else None
    except Exception:
        pass
    log("class chain         : %s" % " -> ".join(chain))
    log("is ATMCharacter     : %s" % isinstance(ch, unreal.TMCharacter))
    for p in ["auto_possess_player", "auto_possess_ai", "auto_receive_input"]:
        log("%-20s: %s" % (p, prop(ch, p)))
    log("default_mapping_ctx : %s" % name_of(prop(ch, "default_mapping_context")))
    for p in ["ia_move", "ia_look", "ia_jump", "ia_sprint", "ia_slide", "ia_draw_sigil",
              "ia_place_sigil", "ia_trigger_sigil", "ia_trigger_all", "ia_temporal_clap",
              "ia_grapple", "ia_grapple_kick", "ia_grapple_slam", "ia_grapple_toss",
              "ia_grapple_evade", "ia_grapple_pull", "ia_web_trap", "ia_fire", "ia_aim",
              "ia_reload", "ia_melee", "ia_crouch", "ia_temporal"]:
        log("%-20s: %s" % (p, name_of(prop(ch, p))))
    log("camera_pitch_target : %s (CDO value; set at runtime by Construction Script)"
        % name_of(prop(ch, "camera_pitch_target")))

    # Components added in the Blueprint (SCS) and inherited from C++.
    try:
        sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        handles = sub.k2_gather_subobject_data_for_blueprint(unreal.load_asset("/Game/Characters/BP_TMCharacter"))
        log("components:")
        for h in handles:
            data = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
            obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
            if obj is None:
                continue
            extra = ""
            if isinstance(obj, unreal.SceneComponent):
                extra = " relLoc=%s" % prop(obj, "relative_location")
                try:
                    extra += " attachParent=%s" % (obj.get_attach_parent().get_name() if obj.get_attach_parent() else "-")
                except Exception:
                    pass
            if isinstance(obj, unreal.SpringArmComponent):
                extra += " useControllerRot=%s armLen=%s" % (prop(obj, "use_pawn_control_rotation"),
                                                            prop(obj, "target_arm_length"))
            if isinstance(obj, unreal.CameraComponent):
                extra += " useControllerRot=%s autoActivate=%s" % (prop(obj, "use_pawn_control_rotation"),
                                                                  prop(obj, "auto_activate"))
            if isinstance(obj, unreal.SkeletalMeshComponent):
                extra += " mesh=%s" % name_of(prop(obj, "skeletal_mesh_asset"))
            log("  - %s (%s)%s" % (obj.get_name(), obj.get_class().get_name(), extra))
    except Exception as e:
        log("components: <failed: %s>" % e)

    # Movement component defaults
    try:
        cmc = ch.get_editor_property("character_movement")
        for p in ["gravity_scale", "max_walk_speed", "default_land_movement_mode", "jump_z_velocity"]:
            log("CMC.%-16s: %s" % (p, prop(cmc, p)))
    except Exception as e:
        log("CMC: <failed: %s>" % e)

    # Blueprint graphs: list event/function nodes so we can see BeginPlay overrides etc.
    try:
        bp = unreal.load_asset("/Game/Characters/BP_TMCharacter")
        graphs = unreal.BlueprintEditorLibrary.find_event_graph(bp)
        log("event graph         : %s" % (graphs.get_name() if graphs else "None"))
    except Exception as e:
        log("event graph         : <n/a: %s>" % e)

# ---------------------------------------------------------------- Mapping context
section("IMC_Default")
imc = unreal.load_asset("/Game/Input/IMC_Default")
if imc:
    maps = None
    for p in ["mappings", "default_key_mappings"]:
        v = prop(imc, p)
        if not isinstance(v, str):
            maps = v
            break
    try:
        if hasattr(maps, "get_editor_property"):
            maps = maps.get_editor_property("mappings")
        for m in maps:
            act = m.get_editor_property("action")
            key = m.get_editor_property("key")
            mods = [mm.get_class().get_name() for mm in (m.get_editor_property("modifiers") or []) if mm]
            if mods:
                # Show swizzle order / negate axes so WASD direction can be checked.
                details = []
                for mm in m.get_editor_property("modifiers"):
                    if isinstance(mm, unreal.InputModifierSwizzleAxis):
                        details.append("Swizzle(%s)" % mm.get_editor_property("order"))
                    elif isinstance(mm, unreal.InputModifierNegate):
                        details.append("Negate(x=%s,y=%s,z=%s)" % (mm.get_editor_property("x"),
                                       mm.get_editor_property("y"), mm.get_editor_property("z")))
                    elif mm:
                        details.append(mm.get_class().get_name())
                mods = details
            vt = act.get_editor_property("value_type") if act else None
            log("  %-22s <- %-16s type=%s mods=%s" % (act.get_name() if act else "None",
                key.get_editor_property("key_name"), vt, mods))
    except Exception as e:
        log("mappings: <failed: %s>" % e)

# ---------------------------------------------------------------- Map
section("L_TestArena")
try:
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ok = les.load_level("/Game/Maps/L_TestArena")
    log("load_level          : %s" % ok)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    log("world               : %s" % name_of(world))
    ws = world.get_world_settings()
    log("WorldSettings.GameMode override: %s" % name_of(prop(ws, "default_game_mode")))
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    log("actor count         : %d" % len(actors))
    for a in actors:
        cls = a.get_class().get_name()
        interesting = isinstance(a, (unreal.PlayerStart, unreal.Pawn, unreal.CameraActor,
                                     unreal.NavMeshBoundsVolume, unreal.StaticMeshActor))
        if interesting or "Ring" in cls or "Enemy" in cls or "Destruction" in cls:
            extra = ""
            if isinstance(a, unreal.Pawn):
                extra = " autoPossessPlayer=%s" % prop(a, "auto_possess_player")
            if isinstance(a, unreal.CameraActor):
                extra = " autoActivateForPlayer=%s" % prop(a, "auto_activate_for_player")
            log("  %-28s %-24s loc=%s rot=%s scale=%s%s" % (
                a.get_actor_label(), cls, a.get_actor_location(), a.get_actor_rotation(),
                a.get_actor_scale3d(), extra))
except Exception as e:
    log("map: <failed: %s>" % e)

path = os.path.join(unreal.Paths.project_saved_dir(), "inspect_setup.txt")
with open(path, "w") as f:
    f.write("\n".join(OUT))
log("written to %s" % path)
