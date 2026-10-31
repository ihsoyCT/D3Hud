# Feature ideas

Parked ideas, with what we already know, so they can be picked up later.

## Faster reader startup

Live: ~18 s from attach to the first published frame. Every block discovery
is its own full pass over the game's private memory: actor array ~3.7 s,
ACD blocks ~5.4 s, attribute groups ~6 s, UI controls ~3 s (scenes ~5 s
more, in the background).

- **One shared pass:** read each memory chunk once and run all matchers on
  it (~4x less reading).
- **Publish first, discover later:** start publishing right after the actor
  array (dots after ~4 s); ACD/attribute/UI discovery in the background
  (rarity already shows "Unknown", never "Common", until resolved).
- **Cache per game process:** save the found bases keyed by pid + process
  start time; a reader restart against the same game validates them in
  milliseconds instead of rescanning.

## Pylons: verify the "used" flag

Used pylons/shrines are hidden via Gizmo_State (464) / Gizmo_Has_Been_Operated
(475). Not yet seen live: F10 next to a fresh and a clicked pylon, the dump's
`gizmo` column shows state/operated.

## Smaller follow-ups

- Elite affixes (Mortar, Frozen, ...) next to champion/rare dots and under the health bars. The 30 affixes
  are catalogued in `analysis/MONSTER_AFFIXES.md` / `src/core/MonsterAffixes.h`; where a monster's own list
  lives is still to be measured with `D3Hud.Reader.exe --attach "Diablo III64.exe" --affixes` next to elites.
- Validate the player heading arrow in-game (quaternion at +0xA0).
- Dead monsters are detected by the cleared acd link (+0x88 == -1); if a
  live monster ever goes missing, F10-dump it - +0x2E0 / +0x16C are backups.

## Settings backlog (brainstormed 2026-10-30)

Implemented 2026-10-30, one commit per group (see git log). Notes: "minimum rarity" for
labels is covered by the label mode; item "slot" rules are the never-salvage item types.

**General**
- Hotkeys: rebind F7/F8/F9/End (hard-coded today); a hotkey per panel.
- Dock: corner, opacity, auto-hide when nothing is hovered.
- Panels: background opacity per panel; compact mode.
- Profiles: save/load whole layouts (farming vs rifting).

**Minimap**
- Per-kind max distance; thin out trash dots in huge packs.
- Ring on the nearest rare/champion.
- Pylons: distance label ("Conduit Pylon 34m"); faint arrow to the nearest
  unused pylon when off the map.
- Player arrow colour/size (own-minimap mode).

**Ground labels and drops**
- Which items get labels (all legendaries / ancient+primal only / sets).
- Label font size, colour per rarity, minimum rarity.
- Alert (sound or flash) on primal/ancient drops.
- Drops panel: session reset, CSV export, entry cap.

**Items and inventory**
- A/P badges: size, position, colour; augment-rank toggle.
- Hover card for identified items too; its opacity.

**Auto salvage**
- Rules by rarity / item type / slot; never-salvage item types.
- Safety: confirm step, delay between clicks, max items per run.
- Preview: red X markers on/off and colour.

**Reader and diagnostics**
- Start the reader with the overlay; publish rate.
- Fps/timings overlay; on-screen reader error log.
- "Open ini folder"; "reset everything" with confirmation.

## Rift progress (investigation)

The reader finds the rift UI controls (`...rift_wrapper.greater_rift_container.rift_progress_text`,
`.rift_progress_bar`, `.rift_progress_marker`, `.rift_timer_value`) but only reads their visibility and
rects. The progress value itself is not located yet. To find it, start a rift and run:

    D3Hud.Reader.exe --attach "Diablo III64.exe" --ui-watch rift_progress --seconds 120 > rift_watch.txt

while killing monsters. The summary at the end lists the fields that changed; the one that only grows
with the progress is the value (or the marker rect moves with it). Then publish it in UiLayout.

## TurboHUD plugin review (2026-10-31): what to port to our plugin system

TurboHUD 2.6.10 ships 110 plugin sources (`plugins/Default`, grouped by Actors, BuffLists, Decorators, Inventory,
Items, Monsters, Players, SkillBars, ...). Facts and ideas only: nothing is copied, plugins are written fresh
against our API. What decides a port is the data the reader publishes: actors (position, kind, internal name, life
and affixes of elites, ground item rarity), backpack/equipment items with grid cells, UI state (inventory, stash,
rift progress and timer), the player position and an area id.

**Port next (the data exists)**

| TurboHUD plugin | Our plugin | What it needs |
|---|---|---|
| MonsterPackPlugin | Elite pack finder: groups elites into packs, an arrow at the screen edge to the nearest pack with its distance, its affixes (priority-ordered, TurboHUD ranks Juggernaut, Arcane, Desecrator, Frozen, Molten first), how much of the pack is left | actors + affixes + life (have it), edge arrow helper in `draw` |
| GoblinPlugin | Treasure goblin finder: names `treasuregoblin_*`, the Gelatinous splitters, the Treasure Fiend minions; label and edge arrow, optional sound | actor names (have it); a `d3hud.beep` host function for the sound |
| InventoryFreeSpacePlugin | Free backpack space: free cells and free 2-high slots, shown when the inventory is open | backpack items with grid cells and heights (have it) |
| DangerousMonsterPlugin | A watch list of monster names to flag on the screen | actor names (internal names, so the list matches fragments) |
| Globe / Chest / Portal / Rack / CursedEvent plugins | In-world markers and edge arrows for globes, chests, portals | actors of those kinds (have it; cursed events/racks would need classifying) |

**Needs reader work first**

- ExplosiveMonsterPlugin (Corpulent, Fast Mummy corpses): the reader drops dead monsters, so it would have to publish those few kinds.
- InventoryMaterialCount, BloodShard, KanaiCubedItems: stack sizes are not in `ItemSnapshot`.
- MonsterRiftProgressionColoring: needs a table monster -> rift progress value (game data, not in memory).
- TopExperienceStatistics (XP per hour, paragon): experience counters are not read.
- AttributeLabelList and EliteMonsterSkill: arbitrary attributes; a monster's group is readable, the player's is not found yet.

**Big projects (research needed, high value)**

BuffLists (cheat death, Convention of Elements, buff timers) and CooldownSoundPlayer can now be written: the snapshot has the
player's buffs and cooldowns (`d3hud.effects()`, API 2). SkillBars, PlayerSkill and SkillRangeHelper also need which skills
are equipped on the action bar and their ranges, which the reader does not read yet.

**Low value here:** Pickup / multiplayer XP range circles (an attribute and multiplayer), OtherPlayers, HeadStone and
Banner (multiplayer), StashPreview and StashUsedSpace (the stash is only visible one tab at a time; the overlay's own
stash cache covers search).

**What the port needs from the platform:** a few drawing helpers in `plugins/lib/draw.lua` (an arrow at the screen
edge toward a world position, a circle on the ground made of projected points, a label with a distance), and for
the goblin sound a `d3hud.beep`. TurboHUD's "decorator" idea (a bundle of ground circle + label + map marker per
thing) maps to these.
