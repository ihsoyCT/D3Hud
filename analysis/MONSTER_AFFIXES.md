# Monster affixes

Research notes for the "affixes under the health bar" feature of the health bars plugin.

## The complete list

The game's balance table `MonsterAffixes` (TurboHUD 2.6.10 ships it as `data/gamebalance_monster_affixes.bin`)
has exactly **30 entries**; the ids are GBIDs (a hash of the affix name, so they do not change with patches).
All of them are in `src/core/MonsterAffixes.h`, which the reader and, later, the plugin API use.

| GBID | key | name | affix effect actors seen in `sno_actors.tsv` |
|---:|---|---|---|
| 3775118 | fast | Fast | |
| 106438735 | molten | Molten | `monsterAffix_Molten_*`, `molten_fireRing`, `molten_bomb_buildUp_geo` |
| 106654229 | mortar | Mortar | `MonsterAffix_Mortar_Pending`, `monsteraffix_mortar_blastwave` |
| 127452338 | horde | Horde | |
| 394214687 | illusionist | Illusionist | |
| 395423867 | vampiric | Vampiric | |
| 458872904 | vortex | Vortex | `monsterAffix_Vortex_*`, `monsterAffix_vortex_target_trailActor` |
| 481181063 | waller | Waller | `monsterAffix_waller_wall`, `waller_model` |
| 1156956365 | wormhole | Wormhole | |
| 1165197192 | avenger | Avenger | `*_MonsterAffix_Avenger_*` |
| 1799201764 | health_link | Health Link | `monsterAffix_healthLink_jumpActor` |
| 1886876669 | frozen_pulse | Frozen Pulse | `x1_MonsterAffix_frozenPulse_*` |
| 1905614711 | orbiter | Orbiter | `X1_MonsterAffix_Orbiter_*` |
| 1929212066 | poison | Poison Enchanted | |
| 2206426855 | knockback | Knockback | |
| 2542537664 | electrified | Electrified | `monsterAffix_Electrified_deathExplosion_proxy` |
| 2625377780 | arcane | Arcane Enchanted | `MonsterAffix_ArcaneEnchanted_*` |
| 2782485594 | extra_health | Extra Health | |
| 2882216553 | missile_dampening | Missile Dampening | `monsterAffix_missileDampening_*` |
| 2920375063 | reflect | Reflects Damage | `monsterAffix_reflectDamage_warmUp_sphere` |
| 2961013602 | plagued | Plagued | `monsterAffix_Plagued_endCloud`, `plagued_groundGeo` |
| 3049048382 | nightmarish | Nightmarish | |
| 3569101591 | shielding | Shielding | |
| 3787260902 | teleporter | Teleporter | `X1_MonsterAffix_TeleportMines` |
| 3830498332 | juggernaut | Juggernaut | |
| 3855260060 | fire_chains | Fire Chains | `monsterAffix_linked_chainHit`, `monsterAffix_entangler_ringGlow_geo` |
| 4131130388 | frozen | Frozen | `monsterAffix_Frozen_deathExplosion_Proxy`, `frozen_iceClusters` |
| 4172983340 | desecrator | Desecrator | `monsterAffix_Desecrator_*` |
| 4244410831 | thunderstorm | Thunderstorm | `x1_MonsterAffix_Thunderstorm_Impact`, `LightningStorm_Wanderer` |
| 4267280439 | jailer | Jailer | |

Not every entry is rolled by the current client (for example Horde, Vampiric, Juggernaut, Extra Health and
Nightmarish look like old or special-case affixes); the table is what the game ships. The effect actors
only tell that an affix is *near*, not which monster has it, so they cannot replace a per-monster list.
Other things that look like affixes but are not: `Champion_Ghostly` (attribute 577) is the champion
"Ghostly" behaviour, `Hide_Affixes` (689) hides the affix names, `EnchantedAffix*` (418-421) belong to the
Enchantress follower.

## Where a monster's affixes are stored: not yet known

TurboHUD reads `IMonster.AffixSnoList` for each monster, so the list is in game memory; where exactly in the
current client is unmeasured. The most likely place is the monster's ACD record (0x3C0 bytes, quality at
+0xB8). The probe for it is built:

```
D3Hud.Reader.exe --attach "Diablo III64.exe" --affixes
```

Stand next to a champion pack or a rare (so they are in the actor list) and run it. For every champion,
rare and boss it scans the ACD record for the 30 GBIDs and prints the offsets (`+0x...  Name`), and writes
the raw record to `dumps/acd_<id>.bin`. Outcomes:

- GBIDs found at the same offsets for several monsters: the list is in the ACD record, and the offsets give the
  layout (an array of up to 8 GBIDs is the expected shape). Then the reader publishes the affixes and the
  plugin can show them.
- Nothing found: the list is elsewhere (an attribute array, or a separate structure). Next step is to compare
  the raw records of an affixed and a plain monster, or scan memory for the GBIDs near the actor.

Record the result here.
