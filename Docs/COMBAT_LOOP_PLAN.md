# Combat loop slice: build plan

Goal: one arena fight of about 5–10 minutes where you **need** movement, magic and the gun together to survive. Waves get harder, you can die, and the run ends with a score. Each item has an ID (pick by ID), a size (**S** about an hour, **M** a few hours, **L** a long block) and what it needs first. It gets built in a new map, so `L_TestArena` (which has your uncommitted changes) stays untouched.

## What's there now

- **Movement:** sprint, slide, double jump, wall run, wall jump, ceiling run, grapple (boost, anchor swings, corner snaps), launch pads on the ring.
- **Magic:** 27 spells, 11 combos plus Overcharge and Amplify, status effects, the spell wheel (Q), the spellbook (B) and the temporal clap (F).
- **Gun:** hold to fire, headshots, elemental infusion.
- **Enemies:** procedural zombies (Husk, Bloat, Gaunt) plus `BP_BaseEnemy`. They walk straight at you (the test arena has no navigation mesh) and hit for a flat 10 every 1.5 s at 1.5 m range, with no wind-up.
- **Enemy pods:** they activate when you come near and send bigger groups if you escape, from a pool of 8.
- **Mana:** 100 max, regenerates at 15/s after 1 s. The gun gives 2 per hit, 10 per kill and +15 per headshot, so regeneration does almost all the work.
- **Missing:**
  - You can't die (at 0 health nothing happens).
  - There are no sounds, and nothing tells you whether you're doing well.
  - Melee (`IA_Melee`) is declared but never bound to a key.

## A. Stakes and flow

| ID | Item | Size | Needs |
|---|---|---|---|
| A1 | **Death and restart:** at 0 health, a short slow-motion death, then a "You died, wave 6, score 12 400" screen with Restart (the run starts over) or Quit. Same as SPELL_PLAN B7. | M | none |
| A2 | **Pause menu** (Esc): resume, mouse sensitivity, field of view, restart, quit. Same as SPELL_PLAN B6. | M | none |
| A3 | **Run HUD:** wave number, enemies left, timer, score, plus a "Wave 3 cleared" toast and a 5 s breather countdown. | S | C1 |
| A4 | **Pickups:** health and mana orbs dropped by kills (more for headshots and combo kills), pulled toward you when you pass at speed. | S | none |

## B. Enemies that each call for a different tool

| ID | Item | Size | Needs |
|---|---|---|---|
| B1 | **Navigation:** a nav mesh in the new arena so zombies path around pillars. Flat maps are fine. On the ring, the nav mesh only works where the floor is roughly level in world space, so ring maps keep the straight-line chase plus steering around obstacles (see F2). | S | C2 |
| B2 | **Readable melee:** a 0.4 s wind-up (a glow and a lean), then a lunge. Dodging, sliding or jumping during the wind-up avoids it. Damage per archetype: Husk normal, Bloat slow and heavy (bursts on death, which hurts you too), Gaunt fast and weak. | M | none |
| B3 | **Spitter** (new): stays 15–25 m away and lobs slow acid shots along ring gravity. It **forces movement**: stay mobile, wall run out of its line, or grapple in and kill it. It's a weak, fragile zombie variant. | M | B2 |
| B4 | **Brute** (new, machine-heavy): big, armoured machine plates on the front, charges in a straight line. Bullets barely hurt the plates (×0.2). Lightning, Earth stagger, headshots from behind, or a combo breaks them. It **forces magic and aim**. Uses the existing machine-part rules. | L | B2 |
| B5 | **Leaper** (new): jumps at you when you camp up high (on a pillar or a wall), so high ground isn't a safe spot. It **forces you to keep moving through the arena**. | M | B2 |
| B6 | **Attack tokens:** at most 3 enemies attack at once, while the rest circle or reposition. Big waves then feel dangerous without being unfair. | S | B2 |
| B7 | **Enemy health bars** above damaged enemies, with status icons. Same as SPELL_PLAN B5. | S | none |

## C. Waves and the arena

| ID | Item | Size | Needs |
|---|---|---|---|
| C1 | **Wave director:** a data table of waves (which enemies, how many, spawn rate, max alive). Uses the existing pool and spawns out of your line of sight. Waves 1–3 are zombies only; then Spitters join, then a Brute every 5th wave; it scales without an end. | M | none |
| C2 | **Combat arena map** (`L_CombatArena`, built by a Python script like `make_ring_arena.py`): about 80 × 80 m. Walls and pillars to wall run and grapple, two raised platforms, ramps, a central pit, spawn tunnels at the edges, and a nav mesh volume. Placeholder shapes. | M | none |
| C3 | **Score and style:** points per kill, multiplied by a combo meter that grows with variety (gun kill, then spell kill, then combo, then airborne kill…) and decays if you repeat yourself or stand still. Speed kills, wall-run kills, headshots and combo kills give bonuses. This tells you how to play without a tutorial. | M | C1 |
| C4 | **Results screen** at death: score, wave, time, and kills split by gun / spell / combo / grapple / melee, plus the best combo meter reached. Saves a best score. | S | A1, C3 |

## D. Economy and tuning

| ID | Item | Size | Needs |
|---|---|---|---|
| D1 | **Mana economy** *(your design choice)*: make the gun feed the magic. Proposed: passive regeneration 15/s → 4/s, gun hits 2 → 4, headshots +15, kills 10, melee 20, mana orbs from A4. Spells then come from fighting, not waiting. | S | none |
| D2 | **One tuning asset** (`DA_CombatTuning`): enemy health, damage and speed per archetype, wave scaling, mana numbers, fire rate, recoil. Lets you balance in the editor without code. | S | none |
| D3 | **Run summary in the log:** at death, one `[TM] RUN ...` line with time, wave, damage taken by source, kills by source, mana spent and earned, and time spent airborne, wall running and grappling. I read it after each of your play sessions to tune. | S | C1 |

## E. Feel

| ID | Item | Size | Needs |
|---|---|---|---|
| E1 | **Placeholder sounds**, made by a script (synthesised WAVs, imported): gunshot, hit, headshot ping, kill, hurt, a cast sound per element, combo, wave start and clear, and footsteps or wind at speed. | M | none |
| E2 | **Hit-stop:** a 40–80 ms freeze on kills, headshots and big spells; a light shake on gun kills (already there). | S | none |
| E3 | **Hit reactions:** zombies flinch and step back from gun hits; big hits stagger. Shooting a zombie's leg makes it stumble. | S | none |
| E4 | **Melee kick** (`IA_Melee`, e.g. V or the mouse thumb button): a kick that scales with your speed. A slide kick sends enemies flying, and a wall-run kick knocks them off ledges. It gives 20 mana (already set up). A movement-first close-range tool. | M | none |

## F. Then on the ring

| ID | Item | Size | Needs |
|---|---|---|---|
| F1 | **Ring version of the arena:** place `L_CombatArena` on the ring like `L_RingArena`, so the fight happens under ring gravity with the ring overhead. | S | C2 |
| F2 | **Ring-safe enemy movement:** steering (seek the player, avoid obstacles and each other) for maps where the nav mesh can't work. | M | F1 |

## Suggested order

1. **A1, C2, B1, C1, A3**: you can die, there's a proper arena, enemies path, and waves come. This is the first playable loop.
2. **B2, B6, E1, E2, D1, D3**: enemies become readable and fair, sound and hit-stop arrive, and the gun feeds mana. This is the first proper "is it fun?" test; you play, I read the run summary.
3. **B3, B4**: the Spitter and the Brute, so each tool has a job.
4. **C3, C4, A4, E3, E4**: score, results, pickups, hit reactions and the kick.
5. **A2, B5, B7, D2**, then **F1, F2** on the ring.

Steps 1–2 are about one long session; 3–4 are a second.

## Decisions for you

- **Mana:** should the gun feed the magic (D1), or keep passive regeneration as the main source?
- **Death:** restart the whole run, or restart the current wave?
- **Run length:** endless waves for a score, or a fixed 10 waves with a boss-ish final wave?
- **Melee key:** V, the mouse thumb button, or something else?
