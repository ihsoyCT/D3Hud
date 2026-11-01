// The player's own numbers and equipped skills, from its attribute map (pure logic, unit-tested).
//
// Indices are those of the current client (analysis/attributes.tsv + 5 from ~355 up); measured live on a Demon Hunter,
// see analysis/ENGINE_MINING.md "Player stats and skills". A resource (Hatred, Discipline, Mana...) is an attribute
// with the resource type as modifier; resistances have the damage type as modifier; skills have the power SNO.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "Effects.h"
#include "PlayerData.h"

namespace d3hud {

// Attribute indices (no modifier unless noted).
constexpr uint32_t kAttrDeathCount = 3;
constexpr uint32_t kAttrStrengthTotal = 14, kAttrDexterityTotal = 15, kAttrIntelligenceTotal = 16, kAttrVitalityTotal = 17;
constexpr uint32_t kAttrArmorTotal = 38;
constexpr uint32_t kAttrExperienceNextHi = 41, kAttrExperienceNextLo = 42;  // experience still missing for the next level
constexpr uint32_t kAttrAltExperienceNextHi = 43, kAttrAltExperienceNextLo = 44;  // the same for the next paragon level
constexpr uint32_t kAttrGoldFindTotal = 56;
constexpr uint32_t kAttrLevel = 57, kAttrAltLevel = 59;  // Alt_Level = paragon level
constexpr uint32_t kAttrMagicFindTotal = 65;
constexpr uint32_t kAttrResistanceTotal = 95;  // modifier = damage type 1..6
constexpr uint32_t kAttrResistanceAll = 96;
constexpr uint32_t kAttrSkillTotal = 102;  // modifier = power SNO
constexpr uint32_t kAttrLifeCur = 115, kAttrLifeMaxTotal = 124;  // as kAttrHitpoints* in AttribTable.h
constexpr uint32_t kAttrHitpointsRegenTotal = 133;
constexpr uint32_t kAttrResourceTypePrimary = 148, kAttrResourceTypeSecondary = 149;
constexpr uint32_t kAttrResourceCur = 150;       // modifier = resource type
constexpr uint32_t kAttrResourceMaxTotal = 153;  // modifier = resource type
constexpr uint32_t kAttrResourceRegenTotal = 160;  // modifier = resource type
constexpr uint32_t kAttrHighestSoloRift = 167, kAttrHighestUnlockedRift = 168, kAttrLastRiftLevel = 169;
constexpr uint32_t kAttrMovementScalarTotal = 185;
constexpr uint32_t kAttrAttacksPerSecondTotal = 201;
constexpr uint32_t kAttrCooldownReductionCapped = 211;
constexpr uint32_t kAttrRuneFirst = 695;  // Rune_A..Rune_E (+0..4), modifier = power SNO: the rune of that skill
constexpr uint32_t kAttrTrait = 1074;     // modifier = power SNO: a passive skill is "on"
constexpr uint32_t kAttrParagonPointsAvailable = 1315;

// "DemonHunter_Female" and the like (the player actor's SNO name); Unknown if it names no class.
HeroClass HeroClassFromActorName(const char* name);

// Fills `out` from the player's attributes (`valid` = 1). `heroClass` is kept (the attribute map has no class).
void ExtractPlayerStats(const std::vector<AttribEntry>& attrs, PlayerSnapshot* out);

// The equipped skills among `attrs`, at most kMaxSkills, ordered by power SNO. `ignore(sno)` drops powers that are not
// skills of the class (the game gives every hero "Generic_*" powers: use item, punch, potion...); may be empty.
void ExtractSkills(const std::vector<AttribEntry>& attrs, const std::function<bool(uint32_t)>& ignore,
                   std::vector<SkillSnapshot>* out);

// The permanent powers among `attrs` (buff icon count above 0, no end tick), ordered by power then slot, at most
// kMaxPassives.
void ExtractPassives(const std::vector<AttribEntry>& attrs, std::vector<PassiveSnapshot>* out);

}  // namespace d3hud
