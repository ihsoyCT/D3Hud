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
