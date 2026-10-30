// Every elite monster affix of the game client, by game-balance id (GBID). The ids and names come from
// the game's MonsterAffixes table (analysis/MONSTER_AFFIXES.md); they are stable across patches because
// a GBID is a hash of the affix's name.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace d3hud {

struct MonsterAffixInfo {
    uint32_t gbid;
    const char* key;   // short stable id for settings and plugins ("health_link")
    const char* name;  // as the game shows it
};

inline constexpr MonsterAffixInfo kMonsterAffixes[] = {
    {3775118, "fast", "Fast"},
    {106438735, "molten", "Molten"},
    {106654229, "mortar", "Mortar"},
    {127452338, "horde", "Horde"},
    {394214687, "illusionist", "Illusionist"},
    {395423867, "vampiric", "Vampiric"},
    {458872904, "vortex", "Vortex"},
    {481181063, "waller", "Waller"},
    {1156956365, "wormhole", "Wormhole"},
    {1165197192, "avenger", "Avenger"},
    {1799201764, "health_link", "Health Link"},
    {1886876669, "frozen_pulse", "Frozen Pulse"},
    {1905614711, "orbiter", "Orbiter"},
    {1929212066, "poison", "Poison Enchanted"},
    {2206426855, "knockback", "Knockback"},
    {2542537664, "electrified", "Electrified"},
    {2625377780, "arcane", "Arcane Enchanted"},
    {2782485594, "extra_health", "Extra Health"},
    {2882216553, "missile_dampening", "Missile Dampening"},
    {2920375063, "reflect", "Reflects Damage"},
    {2961013602, "plagued", "Plagued"},
    {3049048382, "nightmarish", "Nightmarish"},
    {3569101591, "shielding", "Shielding"},
    {3787260902, "teleporter", "Teleporter"},
    {3830498332, "juggernaut", "Juggernaut"},
    {3855260060, "fire_chains", "Fire Chains"},
    {4131130388, "frozen", "Frozen"},
    {4172983340, "desecrator", "Desecrator"},
    {4244410831, "thunderstorm", "Thunderstorm"},
    {4267280439, "jailer", "Jailer"},
};
inline constexpr size_t kMonsterAffixCount = std::size(kMonsterAffixes);

// The affix with this GBID, or nullptr.
inline const MonsterAffixInfo* FindMonsterAffix(uint32_t gbid) {
    for (const MonsterAffixInfo& a : kMonsterAffixes)
        if (a.gbid == gbid) return &a;
    return nullptr;
}

// Index of the affix in kMonsterAffixes, or -1.
inline int FindMonsterAffixIndex(uint32_t gbid) {
    for (size_t i = 0; i < kMonsterAffixCount; ++i)
        if (kMonsterAffixes[i].gbid == gbid) return static_cast<int>(i);
    return -1;
}

// Where a monster keeps its affixes: eight u32 slots in its ACD record from +0x1E4 (unused slots are
// 0xFFFFFFFF; the first slot of a champion holds an id that is not one of the 30, an elite marker).
// Measured live on champions (analysis/MONSTER_AFFIXES.md). The scan starts a little earlier and runs a
// little later so a shifted list is still caught; only ids of the known table count.
constexpr uint32_t kAcdAffixFirst = 0x1DC, kAcdAffixEnd = 0x208;

// Bit i of the result = the monster has kMonsterAffixes[i]. `words` = the ACD record as u32 words.
inline uint32_t AffixMaskFromAcdWords(const uint32_t* words, size_t wordCount) {
    uint32_t mask = 0;
    for (uint32_t off = kAcdAffixFirst; off < kAcdAffixEnd && off / 4 < wordCount; off += 4) {
        const int i = FindMonsterAffixIndex(words[off / 4]);
        if (i >= 0) mask |= 1u << i;
    }
    return mask;
}

}  // namespace d3hud
