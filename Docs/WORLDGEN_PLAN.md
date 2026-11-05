# Ring regions: procedural world generation plan

Goal: the ring reads as many distinct places, both up close and from afar:
- **Up close:** ground shape, colours, structures, enemies and air.
- **From afar:** the arch overhead is a patchwork map of those places.
- **Names:** every place has a name you see when you arrive.

Hand-built levels come later and sit inside `ARingAuthoredZone`s, which the generator keeps flat and empty. Everything is deterministic from `WorldSeed`, so the same spot always looks the same. Sizes: **S** about an hour, **M** a few hours, **L** a long block.

## Status (2026-11-05)

**Phase 1 (R1–R7) is in.** Seed 1337 gives 185 regions.

`-TMRingTest` checks it headlessly: it visits one region of each biome, lands on the terrain and takes screenshots. Notes from building it:
- **Terrain tiles** take about 70 ms to build, about once per km travelled. Moving this to a background thread is a later item.
- **Fog:** Unreal's height fog can't work on the ring, because it measures height along world Z, which is only "up" near where the map was built. In ring maps the content streamer turns it into an even haze that follows the player, with a capped thickness so the arch overhead always shows. Each biome tints it.
- **Landing on terrain:** the engine's movement component sometimes keeps a character resting on the terrain mesh in Falling mode. `ATMCharacter::SettleOntoTerrain` switches it back to walking when there's walkable terrain floor right underneath.

## What was there before this plan

- **The ring:** 1,600 km radius, 200 km wide (about 10,050 km round), 100 km rim walls.
- **Band:** `ARingBandGenerator` draws the whole band as one grey mesh, turns on ring gravity, and spawns the near field and the content streamer.
- **Near field:** `ARingNearField` puts a flat 2 km slab under the player, re-placed every 500 m.
- **Content streamer:** `ARingContentStreamer` fills 200 m cells, 5 × 5 around the player.
  - It uses boxes and cylinders: pillar forests, wall corridors, canopies, mesas with ramps, cover, and a few zombies.
  - Each 50 km square gets a random colour palette and feature mix.
- **Launch pads** fly you across the ring; the destination builds during the flight and gets a return pad.

## Phase 1: regions you can see and feel

| ID | Item | Size |
|---|---|---|
| R1 | **Region map** (`FRingRegions`): the ring split along its length into 70–160 km stretches, each split across into 1–3 regions, with wavy (noise-warped) borders. About 85 stretches, roughly 170 regions. Each region gets a biome, a generated name ("The Cinder Barrens"), a danger level (1–5) and its own variation. Neighbouring regions blend over about 1.5 km so borders aren't hard lines. | M |
| R2 | **Eight biomes**, each tied to an element where it fits (table below): ground colours, terrain shape, structure mix, zombie mix, fog and sun tint. | M |
| R3 | **Terrain:** the flat slab becomes a 4 km height-field tile (20 m grid, with collision) shaped by the biome: dunes, terraces, ice ridges, rolling hills, craters. It blends between regions, stays flat in authored zones, at launch pads and at landing spots, and fades out at the tile edge. Double precision noise, so it doesn't repeat or shimmer anywhere on the ring. | L |
| R4 | **Region-coloured band:** the band overhead gets 24 strips across its width, coloured by the region underneath. Looking up shows the whole ring as a map of regions. | M |
| R5 | **Content by biome:** each biome's own structure mix, plus new pieces (crystal clusters, spikes, arches, ruined walls). Pieces and zombies sit on the terrain. Zombie body types are weighted by biome, and danger sets how many. | M |
| R6 | **Arrival banner and HUD label:** "THE CINDER BARRENS · Ember Wastes · Danger 3" when you cross into a region, plus the region name in the corner. | S |
| R7 | **Atmosphere per region:** fog colour, fog density and sun tint blend toward the biome's as you travel. | S |

### Biomes

| Biome | Element | Ground | Terrain | Structures | Zombies |
|---|---|---|---|---|---|
| Ember Wastes | Fire | rust, ochre, black | terraced plateaus | mesas, spikes, walls | Bloats, danger high |
| Frost Shelf | Ice | white, pale blue | flat sheets, pressure ridges | ice spikes, crystal clusters | Gaunts |
| Storm Highlands | Lightning | slate, steel blue | rolling high hills | tall pillar forests (conductors), arches | mixed |
| Verdant Canopy | Earth | moss, olive | gentle hills | canopies (ceiling runs), pillar forests (trees) | Husks, danger low |
| Prism Fields | Arcane | lilac, teal | soft mounds and basins | crystal clusters, arches | mixed, danger low |
| Void Scar | Void | violet-black | craters | spikes, broken walls | everything, danger very high |
| Foundry Ruins | none (machine) | grey, rust | flat with low steps | wall mazes, canopies, cover | machine-heavy |
| Dune Sea | none | sand | big smooth dunes | sparse arches, mesas | few |

## Phase 2: places worth going to

| ID | Item | Size |
|---|---|---|
| R8 | **Landmarks:** a few giant silhouettes per region (200–800 m crystal spires, ruined arches, machine towers, frozen falls), visible from tens of km away and usable as launch destinations. | M |
| R9 | **Points of interest:** shrines that teach a spell (COMBAT plan C3 / SPELL plan C3), elite nests (a wave fight in the open, using the wave director), mana fonts and supply caches. | M |
| R10 | **Ground scatter:** instanced rocks, crystals and tufts per biome, for speed reading and detail. | M |
| R11 | **Biome hazards:** burning cracks (Ember), slick ice (Frost), lightning strikes (Storm), void pulls (Void). These use the spell system's damage and status. | M |
| R12 | **Far terrain:** a coarse height field out to 10–20 km, so hills don't stop at the tile edge. | L |
| R13 | **World map** (key M): regions, names, danger, launch pads, where you've been. | M |
| R14 | **Ring-safe enemy movement** (COMBAT plan F2): steering instead of the nav mesh, for open ring fights. | M |

## Notes

- The terrain height is a pure function of (arc, lateral), so the near field, content placement, landing spots and later systems all agree without traces.
- Flatten points (authored zones, pads, landing spots) are registered at runtime and reset each play.
- Region borders are warped, blended rectangles in (arc, lateral) space; a lookup is a binary search plus a few noise samples.
- Known limit: the noise has one seam where the arc wraps (arc 0). It's 558 km from the test arena.
