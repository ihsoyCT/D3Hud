// Auto-salvage rules (pure, unit-tested): which backpack items the salvage
// runner may destroy. The user marks a "safe area" of backpack cells; every
// piece of gear outside it is salvaged - INCLUDING unidentified items, whose
// type and ancient rank the reader reveals from memory. The only extra
// protection: ancient/primal items of something currently equipped (same
// display name) are kept as possible upgrades. Anything not positively known
// to be gear (materials, gems, keys, potions, unknown types or names) is
// skipped.
#pragma once

#include <cstdint>
#include <cstring>

#include "Snapshot.h"

namespace d3hud {

// Backpack cells the salvage never touches: bit (row * 10 + column).
struct SafeArea {
    uint64_t cells = 0;

    bool Has(int x, int y) const {
        return x >= 0 && x < kBackpackCols && y >= 0 && y < kBackpackRows &&
               ((cells >> (y * kBackpackCols + x)) & 1) != 0;
    }
    void Toggle(int x, int y) {
        if (x >= 0 && x < kBackpackCols && y >= 0 && y < kBackpackRows) cells ^= 1ull << (y * kBackpackCols + x);
    }
    int Count() const {
        int n = 0;
        for (uint64_t c = cells; c != 0; c &= c - 1) ++n;
        return n;
    }
};

struct SalvageRules {
    // Keep ancient/primal backpack items whose display name matches an
    // equipped piece (a possible upgrade); everything else outside the safe
    // area goes. Non-ancient dupes of worn gear are NOT protected.
    bool protectEquippedAncients = true;
    // Which rarities may be salvaged: bit per ItemRarity (Common..Set). Default: all.
    uint32_t rarityMask = 0x1F;
    bool keepAncient = false;  // never salvage an ancient (or primal) item
    bool keepPrimal = false;   // never salvage a primal item
    uint32_t neverFamilies = 0;  // bit per GearFamily: item types that are never salvaged
    // run safety (used by the runner, not by IsSalvageCandidate)
    int maxPerRun = 0;       // stop after this many items; 0 = no limit
    int extraDelayMs = 0;    // added to every wait between clicks
    bool useGameSalvageAll = true;  // press the game's "salvage all blue/yellow" when that is safe
    bool burst = true;       // click ordinary items without waiting for each to vanish (legendaries one by one)
};

// `type` is `name` or a class variant of it ("Helm" covers "Helm_Necromancer").
inline bool TypeIs(const char* type, const char* name) {
    const size_t n = strlen(name);
    return strncmp(type, name, n) == 0 && (type[n] == '\0' || type[n] == '_');
}

// Gear grouped by the slot it is worn in, for the "never salvage" setting.
constexpr int kGearFamilyCount = 13;
inline const char* GearFamilyName(int f) {
    static const char* kNames[kGearFamilyCount] = {"Helm", "Chest", "Shoulders", "Gloves", "Bracers", "Belt", "Legs",
                                                   "Boots", "Cloak", "Ring", "Amulet", "Off-hand", "Weapon"};
    return f >= 0 && f < kGearFamilyCount ? kNames[f] : "?";
}
inline int GearFamily(const char* type) {
    struct Entry { const char* name; int family; };
    static const Entry kMap[] = {
        {"Helm", 0}, {"GenericHelm", 0}, {"WizardHat", 0}, {"VoodooMask", 0}, {"SpiritStone_Monk", 0},
        {"ChestArmor", 1}, {"GenericChestArmor", 1}, {"Shoulders", 2}, {"Gloves", 3}, {"Bracers", 4},
        {"Belt", 5}, {"GenericBelt", 5}, {"Legs", 6}, {"Boots", 7}, {"Cloak", 8}, {"Ring", 9}, {"Amulet", 10},
        {"Shield", 11}, {"CrusaderShield", 11}, {"Mojo", 11}, {"Orb", 11}, {"Quiver", 11}, {"NecromancerOffhand", 11},
        {"TemplarSpecial", 11}, {"ScoundrelSpecial", 11}, {"EnchantressSpecial", 11}};
    for (const Entry& e : kMap)
        if (TypeIs(type, e.name)) return e.family;
    return 12;  // everything else that is gear: weapons
}

// Gear item types (internal type names, items_gbid.tsv). A listed name matches
// itself and its class variants: "Helm" covers "Helm_Necromancer".
inline bool IsGearType(const char* type) {
    static const char* kGear[] = {
        // armor / jewelry / off-hands
        "Helm", "GenericHelm", "WizardHat", "VoodooMask", "SpiritStone_Monk", "ChestArmor",
        "GenericChestArmor", "Cloak", "Shoulders", "Gloves", "Bracers", "Belt", "GenericBelt", "Legs",
        "Boots", "Ring", "Amulet", "Shield", "CrusaderShield", "Mojo", "Orb", "Quiver",
        "NecromancerOffhand", "TemplarSpecial", "ScoundrelSpecial", "EnchantressSpecial",
        // weapons
        "Axe", "Axe2H", "Sword", "Sword2H", "Mace", "Mace2H", "Dagger", "CeremonialDagger", "FistWeapon",
        "Flail1H", "Flail2H", "MightyWeapon1H", "MightyWeapon2H", "Scythe1H", "Scythe2H", "Spear", "Polearm",
        "Staff", "CombatStaff", "Wand", "Bow", "Crossbow", "HandXbow"};
    for (const char* g : kGear) {
        const size_t n = strlen(g);
        if (strncmp(type, g, n) == 0 && (type[n] == '\0' || type[n] == '_')) return true;
    }
    return false;
}

inline bool InSafeArea(const SafeArea& safe, const ItemSnapshot& it) {
    for (int k = 0; k < (it.height > 0 ? it.height : 1); ++k)
        if (safe.Has(it.gridX, it.gridY + k)) return true;
    return false;
}

// True when `it` is an ancient/primal version of an item currently worn.
// Match by display name: the same legendary variant carries the same name in
// every rank. Unnamed items never match (unknown items are never protected).
inline bool IsEquippedUpgrade(const ItemSnapshot& it, const ItemSnapshot* items, uint16_t itemCount) {
    if ((!it.ancient && !it.primal) || it.name[0] == '\0') return false;
    for (uint16_t i = 0; i < itemCount; ++i) {
        const ItemSnapshot& worn = items[i];
        if (worn.location >= 1 && worn.location <= 13 && worn.name[0] != '\0' &&
            strcmp(worn.name, it.name) == 0)
            return true;
    }
    return false;
}

inline bool IsSalvageCandidate(const ItemSnapshot& it, const SafeArea& safe, const SalvageRules& rules,
                               const ItemSnapshot* items, uint16_t itemCount) {
    if (it.location != kItemLocationBackpack) return false;
    if (it.rarity == ItemRarity::Unknown || it.bonus) return false;  // never destroy the unknown
    if (!IsGearType(it.typeName)) return false;
    if (InSafeArea(safe, it)) return false;
    const int rarity = static_cast<int>(it.rarity);
    if (rarity < 0 || rarity > 4 || ((rules.rarityMask >> rarity) & 1) == 0) return false;
    if (rules.keepPrimal && it.primal) return false;
    if (rules.keepAncient && (it.ancient || it.primal)) return false;
    if ((rules.neverFamilies >> GearFamily(it.typeName)) & 1) return false;
    if (rules.protectEquippedAncients && IsEquippedUpgrade(it, items, itemCount)) return false;
    return true;  // identified or not: type and ancient rank are revealed
}

// The game's "salvage all blue / yellow" buttons destroy EVERY item of that quality in the backpack,
// so they may only be pressed when each of those items is a salvage candidate under our rules (not in
// the safe area, not a protected type, ...), when that quality is allowed at all, and when no item
// is still unclassified (an unknown could be of that quality).
inline bool CanSalvageAllOfQuality(ItemRarity quality, const ItemSnapshot* items, uint16_t itemCount,
                                   const SafeArea& safe, const SalvageRules& rules) {
    if (((rules.rarityMask >> static_cast<int>(quality)) & 1) == 0) return false;
    bool any = false;
    for (uint16_t i = 0; i < itemCount; ++i) {
        const ItemSnapshot& it = items[i];
        if (it.location != kItemLocationBackpack) continue;
        if (it.rarity == ItemRarity::Unknown) return false;
        if (it.rarity != quality) continue;
        if (!IsSalvageCandidate(it, safe, rules, items, itemCount)) return false;
        any = true;
    }
    return any;
}

}  // namespace d3hud
