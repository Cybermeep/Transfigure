# Tools/create_spells.py
#
# Builds every spell from one table (SPELL_PLAN A7): a DA_<Id> data asset per
# spell in /Game/Spells, all registered in /Game/DA_SpellRegistry. Each spell
# picks an effect archetype (Source/.../Effects/SpellEffects.h) and sets the
# Spell|Behaviour fields. Also makes:
#   /Game/Spells/Materials/M_SpellGlow   glowing additive material for spell visuals
#   /Game/Input/IA_Spellbook             + key B in IMC_Default (spellbook screen)
#
# Re-runnable: updates existing assets in place. To add a spell, add a row below.
#
# Run (editor closed):
#   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this> -unattended -nosplash
# Output: Saved/create_spells.txt

import os
import unreal

MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
LOG = []


def log(m):
    LOG.append(str(m))
    unreal.log_warning("[SPELLS] " + str(m))


# ---------------------------------------------------------------- glow material
def make_glow_material():
    folder, name = "/Game/Spells/Materials", "M_SpellGlow"
    path = folder + "/" + name
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    m = TOOLS.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)

    def node(cls, x, y, **props):
        e = MEL.create_material_expression(m, cls, x, y)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    color = node(unreal.MaterialExpressionVectorParameter, -900, 0, parameter_name="Color",
                 default_value=unreal.LinearColor(1.0, 0.5, 0.1, 1.0))
    inten = node(unreal.MaterialExpressionScalarParameter, -900, 200, parameter_name="Intensity", default_value=6.0)
    opac = node(unreal.MaterialExpressionScalarParameter, -900, 320, parameter_name="Opacity", default_value=1.0)
    fres = node(unreal.MaterialExpressionFresnel, -900, 440, exponent=2.5)
    rim = node(unreal.MaterialExpressionLinearInterpolate, -650, 440, const_a=0.06, const_b=1.0)
    MEL.connect_material_expressions(fres, "", rim, "Alpha")
    a = node(unreal.MaterialExpressionMultiply, -650, 100)
    MEL.connect_material_expressions(color, "", a, "A")
    MEL.connect_material_expressions(inten, "", a, "B")
    b = node(unreal.MaterialExpressionMultiply, -450, 200)
    MEL.connect_material_expressions(a, "", b, "A")
    MEL.connect_material_expressions(opac, "", b, "B")
    c = node(unreal.MaterialExpressionMultiply, -250, 300)
    MEL.connect_material_expressions(b, "", c, "A")
    MEL.connect_material_expressions(rim, "", c, "B")
    # Fade out right in front of the camera: an aura ring or burst passing under/around the player
    # otherwise floods the bottom of the view with glow.
    # saturate((PixelDepth - 40) / 320): 0 at the camera, full brightness from 3.6 m away.
    depth = node(unreal.MaterialExpressionPixelDepth, -900, 560)
    sub = node(unreal.MaterialExpressionSubtract, -700, 560, const_b=40.0)
    MEL.connect_material_expressions(depth, "", sub, "A")
    div = node(unreal.MaterialExpressionDivide, -550, 560, const_b=320.0)
    MEL.connect_material_expressions(sub, "", div, "A")
    sat = node(unreal.MaterialExpressionSaturate, -400, 560)
    MEL.connect_material_expressions(div, "", sat, "")
    # NearFade 0 turns the fade off (thin gun tracers should show right from the muzzle).
    near = node(unreal.MaterialExpressionScalarParameter, -400, 680, parameter_name="NearFade", default_value=1.0)
    fade = node(unreal.MaterialExpressionLinearInterpolate, -250, 560, const_a=1.0)
    MEL.connect_material_expressions(sat, "", fade, "B")
    MEL.connect_material_expressions(near, "", fade, "Alpha")
    d = node(unreal.MaterialExpressionMultiply, -100, 340)
    MEL.connect_material_expressions(c, "", d, "A")
    MEL.connect_material_expressions(fade, "", d, "B")
    MEL.connect_material_property(d, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(m)
    EAL.save_loaded_asset(m)
    log("material %s" % path)


# ---------------------------------------------------------------- spellbook input
def make_spellbook_input():
    path = "/Game/Input/IA_Spellbook"
    action = unreal.load_asset(path) if EAL.does_asset_exist(path) else None
    if not action:
        action = TOOLS.create_asset("IA_Spellbook", "/Game/Input", unreal.InputAction, unreal.InputAction_Factory())
    action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    EAL.save_loaded_asset(action)

    imc = unreal.load_asset("/Game/Input/IMC_Default")
    if not imc:
        log("!! IMC_Default not found - map B to IA_Spellbook by hand")
        return
    mapped = False
    try:
        maps = imc.get_editor_property("default_key_mappings")
        maps = maps.get_editor_property("mappings")
    except Exception:
        maps = imc.get_editor_property("mappings")
    for mp in maps:
        if mp.get_editor_property("action") == action:
            mapped = True
    if not mapped:
        key = unreal.Key()
        key.set_editor_property("key_name", "B")
        imc.modify()
        imc.map_key(action, key)
        EAL.save_loaded_asset(imc, False)
        log("mapped B -> IA_Spellbook in IMC_Default")
    else:
        log("IA_Spellbook already mapped")


# ---------------------------------------------------------------- spells
E = unreal.TransfigurationElement
T = unreal.SigilTriggerType
S = unreal.SpellStatus
SH = unreal.SpellStructureShape
SA = unreal.SpellSelfAction
B = unreal.BuffType
AREA, LINE, CHAIN, PULL, STRUCT, SELF = ("SpellEffect_Area", "SpellEffect_Line", "SpellEffect_Chain",
                                         "SpellEffect_Pull", "SpellEffect_Structure", "SpellEffect_Self")

# id, name, element, archetype, trigger, cost, cooldown, damage, extra fields, description
SPELLS = [
    # ---- Fire: burning, explosions, momentum
    ("FireBlast", "Fire Blast", E.FIRE, AREA, T.MANUAL, 20, 1.0, 45,
     dict(impact_radius=300, falloff_radius=650, knockback=500, status=S.BURNING, status_stacks=1, status_duration=4,
          scales_with_speed=True, impact_shake=0.4),
     "Explodes when you trigger it and sets enemies burning. Hits harder the faster you are moving when it goes off."),
    ("FlamePillar", "Flame Pillar", E.FIRE, AREA, T.PROXIMITY_ANY, 20, 3.0, 30,
     dict(impact_radius=220, falloff_radius=380, launch=1400, caster_launch=1600, status=S.BURNING, status_stacks=1,
          trigger_radius=180, impact_shake=0.3),
     "A column of fire erupts when anything steps on it, throwing enemies into the air. Step on it yourself to ride it up."),
    ("EmberMine", "Ember Mine", E.FIRE, AREA, T.PROXIMITY_ENEMY, 10, 0.5, 35,
     dict(impact_radius=200, falloff_radius=420, knockback=300, status=S.BURNING, status_stacks=2, trigger_radius=160,
          impact_shake=0.2),
     "A cheap mine that bursts and sets enemies alight when one walks over it."),
    ("Meteor", "Meteor", E.FIRE, AREA, T.MANUAL, 40, 8.0, 120,
     dict(impact_radius=420, falloff_radius=950, knockback=900, delay=1.5, status=S.BURNING, status_stacks=2,
          temporal_damage_multiplier=2.0, impact_shake=0.9),
     "Calls down a meteor 1.5 seconds after you trigger it. Huge damage - set it off with the temporal clap for double."),
    ("FireTrail", "Fire Trail", E.FIRE, AREA, T.ON_PLACE, 20, 10.0, 6,
     dict(impact_radius=180, falloff_radius=260, duration=6, tick_interval=0.25, follow_caster=True,
          status=S.BURNING, status_stacks=1, status_duration=3, impact_shake=0.0),
     "For 6 seconds you burn everything you run past. Run fast."),

    # ---- Ice: control, shatter, surfaces
    ("FrostNova", "Frost Nova", E.ICE, AREA, T.MANUAL, 20, 2.0, 15,
     dict(impact_radius=350, falloff_radius=700, status=S.CHILLED, status_stacks=2, status_duration=5, impact_shake=0.3),
     "A burst of cold that chills everything nearby. Chill an enemy twice and it freezes solid - shoot a frozen limb and it shatters."),
    ("IceWall", "Ice Wall", E.ICE, STRUCT, T.ON_PLACE, 15, 3.0, 10,
     dict(structure_shape=SH.WALL, structure_size=unreal.Vector(900, 100, 550), structure_lifetime=12,
          impact_radius=200, falloff_radius=450, knockback=700, status=S.CHILLED, status_stacks=1, impact_shake=0.2),
     "Raises a wall of ice where you aim, facing you. Wall run on it, grapple it, or hide behind it."),
    ("GlacierSlide", "Glacier Slide", E.ICE, STRUCT, T.ON_PLACE, 15, 4.0, 0,
     dict(structure_shape=SH.RAMP, structure_size=unreal.Vector(260, 0, 380), structure_lifetime=10,
          structure_slippery=True, impact_shake=0.0),
     "A frictionless ice ramp from your feet to where you aim. Keeps every bit of your speed and launches you off the end."),
    ("IceSpikes", "Ice Spikes", E.ICE, LINE, T.ON_PLACE, 20, 2.0, 25,
     dict(impact_radius=130, falloff_radius=170, line_spacing=170, line_speed=3500, launch=500,
          structure_size=unreal.Vector(0, 0, 220), status=S.CHILLED, status_stacks=1, impact_shake=0.1),
     "A row of ice spikes races from you to where you aim, impaling and chilling everything on the way."),

    # ---- Lightning: chains, speed, machines
    ("ChainLightning", "Chain Lightning", E.LIGHTNING, CHAIN, T.MANUAL, 20, 1.5, 35,
     dict(falloff_radius=600, chain_count=4, chain_range=900, status=S.SHOCKED, status_duration=1, impact_shake=0.25),
     "Strikes the enemy nearest the sigil, then jumps to four more. Double damage to machine parts."),
    ("StormSigil", "Storm Sigil", E.LIGHTNING, AREA, T.MANUAL, 30, 10.0, 18,
     dict(falloff_radius=950, duration=8, tick_interval=0.5, one_random_target_per_tick=True,
          status=S.SHOCKED, status_duration=1, impact_shake=0.05),
     "Once triggered, lightning strikes a random enemy near it every half second for 8 seconds."),
    ("ThunderStep", "Thunder Step", E.LIGHTNING, SELF, T.ON_PLACE, 15, 2.0, 25,
     dict(self_action=SA.DASH_TO_SIGIL, self_amount=3800, follow_caster=True, impact_radius=250, falloff_radius=420,
          knockback=600, status=S.SHOCKED, status_duration=1, impact_shake=0.3),
     "Dash to where you aim, bursting with lightning where you leave and where you land. Keeps your momentum."),
    ("StaticCharge", "Static Charge", E.LIGHTNING, AREA, T.ON_PLACE, 25, 12.0, 12,
     dict(impact_radius=220, falloff_radius=280, duration=6, tick_interval=0.3, follow_caster=True,
          status=S.SHOCKED, status_duration=0.6, impact_shake=0.0),
     "For 6 seconds anything you get close to is shocked. At speed, wall runs and slams become weapons."),

    # ---- Earth: stagger, terrain, armour
    ("EarthSpike", "Earth Spike", E.EARTH, STRUCT, T.MANUAL, 15, 1.5, 30,
     dict(structure_shape=SH.SPIKE, structure_size=unreal.Vector(150, 0, 380), structure_lifetime=1.5,
          impact_radius=180, falloff_radius=320, launch=1300, status=S.STAGGERED, impact_shake=0.35),
     "A stone spike bursts up under the sigil, impaling enemies and throwing them into the air. Crushes legs."),
    ("StonePillar", "Stone Pillar", E.EARTH, STRUCT, T.ON_PLACE, 15, 4.0, 0,
     dict(structure_shape=SH.PILLAR, structure_size=unreal.Vector(320, 0, 1800), structure_lifetime=20,
          impact_radius=200, falloff_radius=350, knockback=500, impact_shake=0.3),
     "Raises a tall stone pillar where you aim: an instant anchor for swings and corner whips, anywhere."),
    ("Quake", "Quake", E.EARTH, AREA, T.MANUAL, 30, 6.0, 40,
     dict(impact_radius=500, falloff_radius=1100, knockback=400, launch=500, status=S.STAGGERED,
          scales_with_speed=True, impact_shake=0.7),
     "A shockwave through the ground that staggers and throws everything around it."),
    ("RockArmour", "Rock Armour", E.EARTH, SELF, T.ON_PLACE, 20, 15.0, 0,
     dict(self_action=SA.BUFF, self_buff=B.DEFENSE, self_buff_multiplier=2.0, self_buff_duration=8),
     "Stone skin: you take half damage for 8 seconds."),

    # ---- Arcane: marks, utility
    ("ArcaneMark", "Arcane Mark", E.ARCANE, AREA, T.MANUAL, 15, 4.0, 5,
     dict(impact_radius=450, falloff_radius=700, status=S.MARKED, status_duration=8, impact_shake=0.1),
     "Marks everything nearby. Marked enemies take 50% more damage from everything for 8 seconds."),
    ("ArcaneLance", "Arcane Lance", E.ARCANE, CHAIN, T.ON_PLACE, 12, 0.8, 30,
     dict(falloff_radius=1400, chain_count=1, chain_range=1000, status=S.MARKED, status_duration=4, impact_shake=0.15),
     "An instant bolt at the enemy nearest where you aim, then one more. Marks what it hits."),
    ("Recall", "Recall", E.ARCANE, SELF, T.TIMED_AUTO, 10, 6.0, 0,
     dict(self_action=SA.RECALL_TO_SIGIL, time_to_live=5),
     "Place it and keep moving: 5 seconds later you snap back to it."),

    # ---- Void: pull, weaken, dismember
    ("VoidRift", "Void Rift", E.VOID, PULL, T.MANUAL, 30, 6.0, 50,
     dict(impact_radius=250, falloff_radius=650, pull_strength=3200, duration=2, status=S.VOIDED, status_duration=4,
          impact_shake=0.5),
     "Drags enemies into a point for 2 seconds, then collapses on them. Pulled enemies are weakened and can't attack."),
    ("SeveringVoid", "Severing Void", E.VOID, AREA, T.MANUAL, 35, 8.0, 15,
     dict(impact_radius=400, falloff_radius=650, severs_nearest_limb=True, status=S.VOIDED, status_duration=3,
          impact_shake=0.5),
     "Tears the nearest limb off every enemy in range."),
    ("NullZone", "Null Zone", E.VOID, AREA, T.MANUAL, 25, 12.0, 3,
     dict(falloff_radius=600, duration=8, tick_interval=0.5, status=S.VOIDED, status_duration=1.2, impact_shake=0.0),
     "For 8 seconds enemies inside can't attack and take more damage."),

    # ---- Support
    ("MightSigil", "Might", E.NONE, SELF, T.ON_PLACE, 25, 15.0, 0,
     dict(self_action=SA.BUFF, self_buff=B.DAMAGE, self_buff_multiplier=1.5, self_buff_duration=10),
     "Your shots and spells deal 50% more damage for 10 seconds."),
    ("LeapSigil", "Leap Sigil", E.NONE, AREA, T.PROXIMITY_ANY, 10, 1.0, 0,
     dict(impact_radius=200, falloff_radius=200, caster_launch=2200, trigger_radius=150, time_to_live=30, impact_shake=0.0),
     "A launch pad: step on it to be thrown high into the air, keeping your speed."),
]

# Persistent sigils are copied from the old test assets (their behaviour lives in APersistentSigilActor).
COPIES = [
    ("HealingSigil", "/Game/Spells/DA_TestHeal", "Healing Sigil", "Stand in it to heal over time."),
    ("HasteSigil", "/Game/Spells/DA_TestSpeed", "Haste Sigil", "Stand in it for a burst of speed that lingers after you leave."),
]


# Combos: (id, name, element A, element B, archetype, damage, extra, description). Fired when two sigils of
# A and B go off within the casting component's ComboRadius. Same element twice = Overcharge and Arcane + any
# other = Amplify are handled in code (no asset).
COMBOS = [
    ("SteamBurst", "Steam Burst", E.FIRE, E.ICE, AREA, 45,
     dict(impact_radius=500, falloff_radius=1000, knockback=900, status=S.BURNING, status_stacks=2, impact_shake=0.6),
     "Fire meets ice: a scalding blast of steam that hurls everything back and burns."),
    ("PlasmaNova", "Plasma Nova", E.FIRE, E.LIGHTNING, AREA, 90,
     dict(impact_radius=450, falloff_radius=1000, knockback=700, launch=400, status=S.SHOCKED, status_duration=1, impact_shake=0.9),
     "Fire and lightning collapse into a huge plasma explosion that stuns."),
    ("MagmaField", "Magma Field", E.FIRE, E.EARTH, AREA, 14,
     dict(falloff_radius=650, duration=7, tick_interval=0.4, status=S.BURNING, status_stacks=1, status_duration=2, impact_shake=0.0),
     "The ground turns to magma for 7 seconds, burning anything that stands on it."),
    ("BlackFlame", "Black Flame", E.FIRE, E.VOID, AREA, 10,
     dict(falloff_radius=700, duration=8, tick_interval=0.4, status=S.BURNING, status_stacks=3, status_duration=3, impact_shake=0.0),
     "Void-fed fire that burns three times as hot for 8 seconds."),
    ("ShatterStorm", "Shatter Storm", E.ICE, E.LIGHTNING, CHAIN, 45,
     dict(falloff_radius=900, chain_count=8, chain_range=1100, status=S.CHILLED, status_stacks=3, status_duration=4, impact_shake=0.5),
     "Frozen lightning leaps between nine enemies and freezes each one solid - then shoot them to shatter."),
    ("GlacierSpire", "Glacier Spire", E.ICE, E.EARTH, STRUCT, 30,
     dict(structure_shape=SH.PILLAR, structure_size=unreal.Vector(500, 0, 2600), structure_lifetime=20,
          impact_radius=300, falloff_radius=700, knockback=900, status=S.CHILLED, status_stacks=2, impact_shake=0.6),
     "A towering spire of ice bursts from the ground, freezing everything around its base. Climb it, swing from it."),
    ("Hoarfrost", "Hoarfrost Rift", E.ICE, E.VOID, PULL, 40,
     dict(impact_radius=250, falloff_radius=700, pull_strength=3500, duration=2, status=S.CHILLED, status_stacks=3,
          status_duration=4, impact_shake=0.5),
     "A freezing rift that drags enemies in and freezes them solid."),
    ("MagnetPulse", "Magnet Pulse", E.LIGHTNING, E.EARTH, PULL, 60,
     dict(impact_radius=300, falloff_radius=900, pull_strength=4500, duration=1.5, status=S.SHOCKED, status_duration=1, impact_shake=0.6),
     "A magnetic surge yanks everything in, tearing at machine parts."),
    ("TempestRift", "Tempest Rift", E.LIGHTNING, E.VOID, AREA, 22,
     dict(falloff_radius=1000, duration=6, tick_interval=0.3, one_random_target_per_tick=True, status=S.SHOCKED,
          status_duration=0.6, impact_shake=0.05),
     "A storm out of the void: lightning strikes a random enemy every third of a second for 6 seconds."),
    ("GravityWell", "Gravity Well", E.EARTH, E.VOID, AREA, 30,
     dict(impact_radius=450, falloff_radius=800, launch=1700, status=S.VOIDED, status_duration=4, impact_shake=0.7),
     "Gravity breaks: everything nearby is thrown high into the air and weakened."),
    ("Singularity", "Singularity", E.VOID, E.ARCANE, PULL, 140,
     dict(impact_radius=350, falloff_radius=1000, pull_strength=5000, duration=3, status=S.VOIDED, status_duration=5, impact_shake=1.0),
     "A singularity drags in everything around it for 3 seconds, then detonates."),
]


def build_combos(table):
    for sid, name, a, b, arch, dmg, extra, desc in COMBOS:
        da = get_or_make_spell(sid)
        da.set_editor_property("spell_id", sid)
        da.set_editor_property("display_name", unreal.Text(name))
        da.set_editor_property("description", unreal.Text(desc))
        da.set_editor_property("element", a)
        da.set_editor_property("is_combo", True)
        da.set_editor_property("combo_element_a", a)
        da.set_editor_property("combo_element_b", b)
        da.set_editor_property("effect_class", effect_class(arch))
        da.set_editor_property("trigger_type", T.MANUAL)
        da.set_editor_property("mana_cost", 0.0)
        da.set_editor_property("cooldown_duration", 0.0)
        da.set_editor_property("base_damage", float(dmg))
        da.set_editor_property("causes_environmental_destruction", False)
        for k, v in extra.items():
            da.set_editor_property(k, v)
        EAL.save_loaded_asset(da, False)
        table[unreal.Name(sid)] = da
    log("combos: %d built" % len(COMBOS))


def effect_class(name):
    return getattr(unreal, name).static_class()


def get_or_make_spell(sid):
    path = "/Game/Spells/DA_%s" % sid
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.TransfigurationDefinition)
    return TOOLS.create_asset("DA_%s" % sid, "/Game/Spells", unreal.TransfigurationDefinition, factory)


def build_spells(registry):
    table = registry.get_editor_property("spell_table")
    made = 0
    for sid, name, element, arch, trigger, cost, cd, dmg, extra, desc in SPELLS:
        da = get_or_make_spell(sid)
        da.set_editor_property("spell_id", sid)
        da.set_editor_property("display_name", unreal.Text(name))
        da.set_editor_property("description", unreal.Text(desc))
        da.set_editor_property("element", element)
        da.set_editor_property("effect_class", effect_class(arch))
        da.set_editor_property("trigger_type", trigger)
        da.set_editor_property("mana_cost", float(cost))
        da.set_editor_property("cooldown_duration", float(cd))
        da.set_editor_property("base_damage", float(dmg))
        da.set_editor_property("causes_environmental_destruction", False)   # destruction is off for now
        da.set_editor_property("is_persistent_sigil", False)
        da.set_editor_property("is_healing_sigil", False)
        da.set_editor_property("is_buff_sigil", False)
        if "trigger_radius" not in extra:
            da.set_editor_property("trigger_radius", 200.0)
        if "time_to_live" not in extra:
            da.set_editor_property("time_to_live", 45.0)
        for k, v in extra.items():
            da.set_editor_property(k, v)
        EAL.save_loaded_asset(da)
        table[unreal.Name(sid)] = da
        made += 1
    log("spells: %d built" % made)

    for sid, src, name, desc in COPIES:
        path = "/Game/Spells/DA_%s" % sid
        if not EAL.does_asset_exist(path):
            if not EAL.duplicate_asset(src, path):
                log("!! could not copy %s" % src)
                continue
        da = unreal.load_asset(path)
        da.set_editor_property("spell_id", sid)
        da.set_editor_property("display_name", unreal.Text(name))
        da.set_editor_property("description", unreal.Text(desc))
        da.set_editor_property("element", E.NONE)
        # Their behaviour is the persistent sigil itself; triggering them must not run an effect
        # (the old test assets had FireBlast's effect attached).
        da.set_editor_property("effect_class", None)
        da.set_editor_property("trigger_type", T.MANUAL)
        EAL.save_loaded_asset(da, False)
        table[unreal.Name(sid)] = da
        log("copied %s -> %s" % (src, sid))

    build_combos(table)

    # The old test entries are replaced by the copies above.
    for key in list(table.keys()):
        if str(key) in ("TestHeal", "TestSpeed"):
            del table[key]
    registry.modify()
    registry.set_editor_property("spell_table", table)
    EAL.save_loaded_asset(registry, False)   # force: changing the map alone doesn't mark it dirty
    check = unreal.load_asset("/Game/DA_SpellRegistry").get_editor_property("spell_table")
    log("registry: %d spells (saved: %d)" % (len(table), len(check)))


make_glow_material()
make_spellbook_input()
reg = unreal.load_asset("/Game/DA_SpellRegistry")
if not reg:
    log("!! /Game/DA_SpellRegistry not found")
else:
    build_spells(reg)

with open(os.path.join(unreal.Paths.project_saved_dir(), "create_spells.txt"), "w") as f:
    f.write("\n".join(LOG))
