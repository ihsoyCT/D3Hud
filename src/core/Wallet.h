// The account's counters the game keeps in one small table: gold, blood shards and the crafting materials
// (the numbers in the game's material panel). Pure decoding; the reader finds the table in memory (WalletTable).
//
// Layout (measured, see analysis/ENGINE_MINING.md): consecutive 24-byte records, record k = currency id k:
//   +0x00 u64 amount   +0x08 u64 0   +0x10 u32 currency id   +0x14 u32 0
// id 1 gold, 2 blood shards, 4..13 the ten materials below in the panel's order, 14..16 Vial of Putridness, Idol of Terror, Heart of Fright (their order among the three is a guess), 17 Leoric's Regret (a guess), 21 Greater Rift Keystones, 22 Primordial Ashes
// (22 is the last record: its id word is not filled, so it is found by position, not by id).
#pragma once

#include <cstddef>
#include <cstdint>

namespace d3hud {

enum class Material : uint8_t {
    ReusableParts, ArcaneDust, VeiledCrystal, DeathsBreath, ForgottenSoul,
    KhanduranRune, CaldeumNightshade, ArreatWarTapestry, CorruptedAngelFlesh, WestmarchHolyWater,
    PrimordialAshes, GreaterRiftKeystone,
    VialOfPutridness, IdolOfTerror, HeartOfFright, LeoricsRegret,  // the Hellfire ring materials, ids 14..17
    Count,
};
constexpr int kMaterialCount = static_cast<int>(Material::Count);
constexpr int kMaterialSlots = 16;  // room in the snapshot

// Lower case key for plugins ("reusable_parts"), "" for an index out of range.
const char* MaterialKey(int index);

// In the snapshot (trivially copyable). valid = 0: the reader has not found the table (yet).
struct WalletSnapshot {
    uint64_t gold = 0;
    uint32_t bloodShards = 0;
    uint32_t valid = 0;
    uint32_t materials[kMaterialSlots] = {};
};

constexpr size_t kWalletRecord = 24;
constexpr size_t kWalletRecords = 22;  // ids 1..22
constexpr size_t kWalletBytes = kWalletRecord * kWalletRecords;

// Does a table that starts with the gold record begin at `p` (>= kWalletBytes readable bytes)? The ids of the
// first twelve records are 1, 2, 3, ... and the filler words are zero: unlikely to occur by chance.
bool LooksLikeWallet(const uint8_t* p);

// Decode a table that LooksLikeWallet. Amounts above 32 bits are clamped for blood shards and materials.
void DecodeWallet(const uint8_t* p, WalletSnapshot* out);

}  // namespace d3hud
