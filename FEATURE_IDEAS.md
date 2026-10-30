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

- Elite affixes (Mortar, Frozen, ...) next to champion/rare dots - affix
  actors (`x1_monsteraffix_*`) exist near the pack; per-monster affixes are
  probably in the ACD/attribute layer.
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
