# Spells, sigils, spellbook and UI: build plan

Draft for prioritising. Each item has an ID (pick by ID), a size (**S** about an hour, **M** a few hours, **L** a long block) and what it needs first. Items marked *(exists)* are already in the game and only get extended.

## Status (2026-11-05)

Built and checked by the headless smoke test (commits `0ec0798`, `d29d054`, `a2d3179`, `64dd30b`):

- **Done:** A1, A2, A3, A5, A6, A7, B1, B2, B3, B4, C1, E1–E11, most of A4 and A8.
- **Spells (27):** every D spell except **D19 Portal Pair, D20 Mana Font, D25 Void Tether**. Extra: **Arcane Lance** (instant bolt that marks). Fire Trail and Static Charge are auras that follow you (no special wall-run or slam hook yet).
- **Combos:** a matching pair fires its combo when either sigil goes off within 6 m (`ComboRadius`). Differences from the table below: Steam Burst knocks back and burns (no blinding); Shatter Storm is a freezing chain across 9 enemies; Black Flame burns at 3 stacks but doesn't spread; Magma Field has no slow. Three extra: **Hoarfrost Rift** (Ice + Void), **Tempest Rift** (Lightning + Void), **Gravity Well** (Earth + Void). Overcharge = 1.6x damage and 1.5x radius; Amplify = 1.3x damage and 2x radius.
- **Spellbook (B):** canvas-drawn, not Slate/UMG. It lists combos (??? until found).
- **Gunplay (not in this plan):** hold to fire at 9 shots/s, tracers and impact sparks in the infusion colour, muzzle flash, recoil that settles, headshots (gold numbers, x2 damage, extra mana), shots shove loose limbs and corpses.
- **Not done:** A8 hit-stop and sounds; B5–B7; C2–C4; D19, D20, D25.
- **Tools:** `Tools/create_spells.py` (editor closed) rebuilds every spell, combo, the glow material and the registry. Smoke test: run the game with `-TMSpellTest` (see CLAUDE.md).

## What was there before this plan

- **Casting:** tap Q places the selected sigil at your aim; hold Q opens a radial wheel of every spell in `DA_SpellRegistry`; trigger keys fire the nearest sigil or all of them. The temporal clap fires all sigils together.
- **Spells:** three data assets: `DA_FireBlast` (radial damage), `DA_TestHeal`, `DA_TestSpeed`. Effect code also exists for an ice wall. Portals exist but are broken.
- **Sigil trigger types:** Manual, ProximityEnemy, ProximityAny, TimedAuto, Stepped. Persistent sigils and buffs (speed, damage, defense, jump) work.
- **Combos:** the Fire+Fire recipe works.
- **Elements:** Fire, Ice, Lightning, Arcane, Earth, Void.
- **HUD:** a placeholder canvas: crosshair, one status line, the wheel, the speed border and launch markers.

## A. Foundation (everything else builds on this)

| ID | Item | Size | Needs |
|---|---|---|---|
| A1 | **Effect archetypes in C++.** Not one class per spell: about 8 reusable behaviours, each spell being a data asset that configures one. Burst (radial hit), Column (vertical launch), Line (spikes or beam toward the aim), Chain (jumps between enemies), Aura (persistent ticking zone), Pull (vortex), Structure (spawns a wall or pillar you can run on), Buff/Self (dash, armour, haste). | L | none |
| A2 | **Shared hit pipeline:** finds enemies, corpses and severed parts in an area; damages through `TakePoolDamage` with the element; knocks back along ring-up; applies a status; handles momentum scaling (`bScalesWithSpeed`) and the damage-buff multiplier. | M | none |
| A3 | **Status effects on enemies** (a component): Burning (damage over time, spreads to nearby corpses), Chilled then Frozen at 3 stacks (slow, then frozen solid), Shocked (short stun, chains), Marked (Arcane: takes +50% damage), Staggered (Earth: interrupts, knock-up), Voided (weakened, pulled). Each shows a colour tint on the zombie. | M | A2 |
| A4 | **Limb interactions per element:** frozen limbs shatter on any hit (instant sever); burning lowers limb health; Earth crushes legs (crawl); Void rips the limb nearest the rift; lightning vs. machine parts does double damage and sparks. | M | A3, limb system |
| A5 | **Element resistances per zombie type and machine parts:** machine parts resist fire and are weak to lightning; Bloats are weak to fire (they burst); Gaunts resist ice. | S | A3 |
| A6 | **Sigil visuals:** coloured glyph disc per element, a ring showing the trigger radius while aiming, a pulse while arming, a flash on trigger. Placeholder shapes and lights now, Niagara later. | M | none |
| A7 | **Spell asset generator script** (`Tools/create_spells.py`): creates every `DA_<Spell>` and registry entry from one table, so adding a spell is a line of data, not editor clicks. | S | A1 |
| A8 | **Feedback:** hit marker on the crosshair, floating damage numbers (coloured by element), screen shake and a brief hit-stop on big spells, placeholder sounds. | M | none |

## B. Basic UI

| ID | Item | Size | Needs |
|---|---|---|---|
| B1 | **HUD layout:** health and mana bars (bottom-left), selected spell with icon colour, cost and cooldown sweep (bottom-right), active sigil count, low-health vignette, small speed meter tied to the speed zoom-out (the ceiling-run border stays). | M | none |
| B2 | **Spell wheel upgrade** *(exists)*: up to 8 slots showing only equipped spells; element colours, cost, cooldown ring, and red when you can't afford it. | M | C1 |
| B3 | **Spellbook screen** (key **B**): known spells grouped by element; a detail panel (description, damage, radius, cost, cooldown, trigger type, combo partners); click a spell then a wheel slot to equip it; locked spells greyed out. The game slows while it's open. Built in code (Slate/UMG from C++, no hand-made widget assets). | L | C1 |
| B4 | **Toasts:** "Learned Frost Nova", "Combo: Steam Burst", "Not enough mana". | S | none |
| B5 | **Enemy health bars** above zombies once damaged, with status icons. | S | A3 |
| B6 | **Pause menu** (Esc): resume, mouse sensitivity, field of view, quit. | M | none |
| B7 | **Death and respawn screen** (the player currently stops at 0 health). | M | player death |

## C. Spellbook data and progression

| ID | Item | Size | Needs |
|---|---|---|---|
| C1 | **Known and equipped spells on the player:** a set of known spells plus 8 wheel slots. Q uses the wheel's selection. Starter set: FireBlast, Frost Nova, Chain Lightning, Earth Spike, Healing Sigil, Haste. | S | none |
| C2 | **Save and load** of known and equipped spells (SaveGame). | S | C1 |
| C3 | **Spell scrolls as procedural loot:** a pickup that teaches a spell, placed by the ring content streamer (rarer in dangerous regions); hand-built levels can place specific ones. | M | C1, streamer |
| C4 | **Spell ranks:** ranks 1–3 raise damage and radius and add a perk (e.g. Chain Lightning +2 jumps). | M | C1 |

## D. The spell list

Each spell shows its archetype, trigger, effect and movement synergy (most spells reward speed). Sizes assume A1–A3 exist; most are then **S** (data plus a small tweak).

### Fire: burning, explosions, momentum
- **D1 FireBlast** *(exists)*. Burst, manual trigger. Radial explosion that now applies Burning. Synergy: damage scales with your speed when you trigger it.
- **D2 Flame Pillar.** Column, manual or proximity trigger. A fire column launches enemies straight up (ring-up), then they burn. You can ride it too: standing in it launches you, so it's a movement tool.
- **D3 Ember Mine.** Burst, ProximityEnemy trigger. Small, cheap, arms after 1 s, chains into other mines.
- **D4 Fire Trail.** Self/Aura, toggle. While sprinting or wall running you leave burning ground behind you; its length grows with speed.
- **D5 Meteor.** Burst, manual with a 1.5 s delay. Huge strike at the sigil. If the temporal clap triggers it, it lands instantly.

### Ice: control, shatter, surfaces
- **D6 Frost Nova.** Burst. Radial Chill (two hits freeze). Frozen zombies shatter when shot.
- **D7 Ice Wall** *(effect exists)*. Structure. A wall rises at the sigil, facing you; you can wall run and grapple on it, and it melts after 12 s.
- **D8 Glacier Slide.** Structure. A frictionless ice ramp from you to the sigil that keeps all your momentum (a skating lane).
- **D9 Ice Spikes.** Line. A row of spikes from you to the sigil that impales (stagger) and freezes.

### Lightning: chains, speed, machines
- **D10 Chain Lightning.** Chain. Hits the nearest enemy, jumps 4 times, double damage to machine parts.
- **D11 Storm Sigil.** Aura, persistent. Zaps a random enemy in range every 0.5 s for 8 s.
- **D12 Thunder Step.** Self/Dash. You dash to the sigil and leave a shock burst where you start and where you arrive. Momentum is kept, which makes it a movement spell.
- **D13 Static Charge.** Self/Buff. For 6 s, anything you touch at speed is shocked (wall runs, slams and grapple kicks deal shock damage).

### Earth: stagger, terrain, armour
- **D14 Earth Spike.** Column. A single spike that impales and knocks up (great with grapple kicks).
- **D15 Stone Pillar.** Structure. Raises a tall pillar at the sigil: an instant anchor for swings and corner whips anywhere.
- **D16 Quake.** Burst along the ground. A shockwave that staggers; stronger when it's triggered by a slam landing next to it.
- **D17 Rock Armour.** Self/Buff. +50% defence and no knockback for 8 s, but slower acceleration.

### Arcane: marks, mana, utility
- **D18 Arcane Mark.** Burst. Marks enemies so they take +50% damage for 8 s.
- **D19 Portal Pair** *(exists, broken)*. Two linked sigils: enter one, leave the other with your momentum and new facing. Needs the portal fixes from FIX_PLAN.
- **D20 Mana Font.** Aura, persistent. Fast mana regeneration while you stand in it.
- **D21 Recall.** Self. Snaps you back to the sigil (an escape tool).

### Void: pull, weaken, dismember
- **D22 Void Rift.** Pull. Drags enemies in for 2 s, then collapses for damage.
- **D23 Severing Void.** Burst. Tears the nearest limb off every enemy in range: the dismemberment showcase.
- **D24 Null Zone.** Aura. Enemies inside can't attack.
- **D25 Void Tether.** Chain. Links up to 4 enemies, and damage to one is shared.

### Support: heal and buffs (some exist)
- **D26 Healing Sigil** *(exists)*.
- **D27 Haste Sigil** *(exists)*.
- **D28 Might Sigil:** a damage buff zone.
- **D29 Leap Sigil:** a launch pad sigil (a big ring-up jump that keeps momentum).

## E. Combos (two sigils of different elements placed close together fire as one spell)

The Fire+Fire recipe already exists; the recipe system takes any pair.

| ID | Pair | Combo | Effect |
|---|---|---|---|
| E1 | Fire + Ice | **Steam Burst** | Big cloud that scalds (damage over time) and blinds enemies (they lose track of you) |
| E2 | Fire + Lightning | **Plasma Nova** | Large explosion plus Shocked |
| E3 | Fire + Earth | **Magma Field** | Burning ground that lasts, plus slow |
| E4 | Ice + Lightning | **Shatter Storm** | Every frozen enemy in range explodes into shards |
| E5 | Earth + Ice | **Glacier Spire** | A tall ice pillar you can run on, that freezes around its base |
| E6 | Lightning + Earth | **Magnet Pulse** | Pulls machine parts: rips robot limbs off and yanks machine zombies in |
| E7 | Void + Arcane | **Singularity** | Strong pull, then a big detonation |
| E8 | Void + Fire | **Black Flame** | Burning that can't be put out and spreads between enemies |
| E9 | Arcane + anything | **Amplify** | The other sigil's spell at 2x radius |
| E10 | Same element twice | **Overcharge** *(Fire+Fire exists)* | A bigger version of that element's base spell |

Also: **E11** combo discovery is logged in the spellbook (unknown combos show as "???" until found), with a toast the first time.

## F. Suggested order

1. **A1, A2, A7**: the framework; after that, spells are mostly data.
2. **C1 + D-core** (12 spells: D1 FireBlast, D2 Flame Pillar, D6 Frost Nova, D7 Ice Wall, D10 Chain Lightning, D12 Thunder Step, D14 Earth Spike, D15 Stone Pillar, D18 Arcane Mark, D22 Void Rift, D23 Severing Void, D26 Healing Sigil, plus D27 Haste): two or three per element, picked for combat and movement synergy.
3. **A3, A4**: status effects and limb interactions.
4. **B1, B2, A8, B4**: HUD, the upgraded wheel, feedback, toasts.
5. **B3**: the spellbook screen.
6. **A6**: sigil visuals.
7. **E1–E10**: combos.
8. The remaining spells, then **C2–C4, B5–B7**.

Rough total for steps 1–6 is one long session; combos and the remaining spells are a second one.
