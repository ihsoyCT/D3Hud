// Plain data the reader publishes about the player and about itself (part of WorldSnapshot, so no dependencies).
#pragma once

#include <cstddef>
#include <cstdint>

namespace d3hud {

enum class HeroClass : uint8_t { Unknown = 0, Barbarian, Crusader, DemonHunter, Monk, Necromancer, WitchDoctor, Wizard };

inline const char* HeroClassName(HeroClass c) {
    static const char* const k[] = {"?", "Barbarian", "Crusader", "Demon Hunter", "Monk", "Necromancer", "Witch Doctor", "Wizard"};
    return static_cast<size_t>(c) < sizeof(k) / sizeof(k[0]) ? k[static_cast<size_t>(c)] : "?";
}

// The player's own numbers. `valid` is 0 until the attribute map was read; then every field is set (0 where the game has
// none, e.g. the second resource of a class with one). Life is in game units, speeds are multipliers (1.0 = base).
struct PlayerSnapshot {
    uint8_t valid = 0;
    uint8_t heroClass = 0;               // HeroClass
    uint8_t resourceType[2] = {};        // the game's resource ids (Demon Hunter: 5 Hatred, 6 Discipline)
    uint16_t level = 0;
    uint16_t paragonLevel = 0;
    uint16_t paragonPointsFree = 0;
    uint16_t highestSoloRift = 0;        // account records
    uint16_t highestUnlockedRift = 0;
    uint16_t lastRiftLevel = 0;
    uint16_t deaths = 0;                 // how often the character has died (Death_Count): take differences
    float life = 0.f, lifeMax = 0.f, lifeRegen = 0.f;
    float resource[2] = {}, resourceMax[2] = {}, resourceRegen[2] = {};
    uint64_t xpToNext = 0;               // experience still missing for the next level (0 at the cap)
    uint64_t paragonXpToNext = 0;        // the same for the next paragon level: falls while you play, jumps up at a level-up
    float strength = 0.f, dexterity = 0.f, intelligence = 0.f, vitality = 0.f;
    float armor = 0.f, resistAll = 0.f;
    float resist[6] = {};                // physical, fire, lightning, cold, poison, arcane
    float attackSpeed = 0.f, moveSpeed = 0.f, cooldownReduction = 0.f, goldFind = 0.f, magicFind = 0.f;
};

// One skill the player has equipped (an active with its rune, or a passive). Cooldown ticks are the game's clock (as in
// EffectSnapshot); -1 = not on cooldown. The slot on the action bar is not an attribute and is not known yet.
constexpr int kMaxSkills = 16;
struct SkillSnapshot {
    uint32_t sno = 0;           // the power
    uint8_t rune = 0;           // 0 none, 1..5 = rune A..E
    uint8_t passive = 0;
    uint16_t reserved = 0;
    int32_t cooldownStartTick = -1;
    int32_t cooldownEndTick = -1;
};

// A power that is on the character without a timer: a passive skill, a legendary item or gem power, a set bonus, an
// event buff. The game keeps them as buffs with a stack count and no end tick (Buff_Icon_Count set, Buff_Icon_End_Tick 0).
// `slot` is the icon slot (a power can hold several; Squirt's Necklace keeps its 10 stacks in slot 5).
constexpr int kMaxPassives = 128;
struct PassiveSnapshot {
    uint32_t sno = 0;
    uint16_t slot = 0;
    uint16_t stacks = 0;
};

// What the reader itself knows about how well it sees the game: one state per data source, so that a table that went
// stale (the game rebuilt it) is visible instead of silently showing old or empty data.
enum class HealthSource : uint8_t { Actors = 0, Records, Attributes, Inventory, Player, Ui, Scenes, Wallet, Count };
constexpr int kHealthSources = static_cast<int>(HealthSource::Count);

enum class HealthState : uint8_t {
    Unknown = 0,  // nothing to judge yet
    Ok,           // found and read
    Degraded,     // found, but part of it does not resolve (stale or still being located)
    Missing,      // not found
};

inline const char* HealthSourceName(HealthSource s) {
    static const char* const k[] = {"actors", "records", "attributes", "inventory", "player", "ui", "scenes", "wallet"};
    return static_cast<size_t>(s) < sizeof(k) / sizeof(k[0]) ? k[static_cast<size_t>(s)] : "?";
}

struct ReaderHealth {
    uint8_t state[kHealthSources] = {};  // HealthState per HealthSource
    uint16_t actorsSeen = 0;             // actors the game listed (before the cap of kMaxActors)
    uint16_t actorsDropped = 0;          // of those, not published (cap reached)
    uint16_t itemsDropped = 0;           // items not published (cap of kMaxItems)
    uint16_t monstersUnresolved = 0;     // monsters whose record was not found yet
    uint16_t itemsUnresolved = 0;        // items whose attributes were not found yet
    uint16_t uiMissing = 0;              // UI controls not found
    float ticksPerSecond = 0.f;          // how fast the reader loop runs
};

}  // namespace d3hud
