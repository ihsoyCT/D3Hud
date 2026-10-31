// Buffs and cooldowns of the player, from its attribute map (pure logic, unit-tested).
//
// Both are attributes of the player's ACD with the power SNO as modifier (key = index | SNO << 12):
//   buff:     Buff_Icon_Start_Tick<N> / Buff_Icon_End_Tick<N> / Buff_Icon_Count<N>, N = 0..31 icon slots
//   cooldown: Power_Cooldown (tick it is ready again) / Power_Cooldown_Start
// An idle cooldown holds the sentinels 0xFFFFFFFF / 0x7FFFFFFF, an expired buff is zeroed (not removed).
// Ticks are the game's clock, 60 per second. Measured live: analysis/ENGINE_MINING.md.
#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Snapshot.h"

namespace d3hud {

constexpr uint32_t kAttrPowerCooldown = 362;       // finish tick
constexpr uint32_t kAttrPowerCooldownStart = 363;
constexpr uint32_t kAttrBuffStartTick = 594;       // + slot
constexpr uint32_t kAttrBuffEndTick = 626;         // + slot
constexpr uint32_t kAttrBuffCount = 767;           // + slot
constexpr uint32_t kBuffSlots = 32;
constexpr uint32_t kAttribNoModifier = 0xFFFFF;
constexpr uint32_t kCooldownIdleFinish = 0xFFFFFFFFu;
constexpr uint32_t kCooldownIdleStart = 0x7FFFFFFFu;
constexpr float kTicksPerSecond = 60.f;

// One entry of an attribute map: key = index | modifier << 12.
struct AttribEntry {
    uint32_t index;
    uint32_t modifier;  // kAttribNoModifier, or the power SNO for buffs and cooldowns
    uint32_t value;
};

// The live effects among `attrs`, ordered by power then slot (buffs first, then cooldowns); at most
// kMaxEffects. A buff counts while its stack count is above 0 and its end tick is set. Icon slots of one power
// with identical start and end ticks are one effect (the first slot is kept).
void ExtractEffects(const std::vector<AttribEntry>& attrs, std::vector<EffectSnapshot>* out);

struct EffectTimes {
    float duration, remaining, elapsed;  // seconds of game time
};
inline EffectTimes TimesOf(const EffectSnapshot& e, double tickNow) {
    const float dur = std::max(0.f, static_cast<float>(e.endTick - e.startTick) / kTicksPerSecond);
    return {dur, std::max(0.f, static_cast<float>(e.endTick - tickNow) / kTicksPerSecond),
            std::clamp(static_cast<float>(tickNow - e.startTick) / kTicksPerSecond, 0.f, dur)};
}

// The game tick at wall time `nowMs` (the clock of snapshot.timestampMs) from a snapshot's tick, which
// is as old as the snapshot; -1 when unknown. A snapshot older than half a second (the reader stalled) or
// from another clock (a replay) is not extrapolated.
inline double TickAtTime(int32_t snapshotTick, double snapshotMs, double nowMs) {
    if (snapshotTick < 0) return -1.0;
    const double age = nowMs - snapshotMs;
    return snapshotTick + (age > 0.0 && age < 500.0 ? age * (kTicksPerSecond / 1000.0) : 0.0);
}

// The game's clock is not in a place we can read, but every effect carries ticks: a start tick that has just
// changed was set "now". Update() is called with the effects of each poll; Tick() extrapolates from the last
// such moment at 60 ticks per second of wall time. Accurate to about one poll interval whenever something
// (a passive refreshing every 30-60 ticks, a potion, a pylon) changed lately; an end tick that is already
// in the past while its effect still runs shows the estimate is too high and pulls it back.
class GameClock {
public:
    void Update(double nowMs, const EffectSnapshot* fx, size_t n);
    [[nodiscard]] bool valid() const { return valid_; }
    // Estimated tick at wall time `nowMs` (same clock as Update); -1 until an effect was seen.
    [[nodiscard]] int32_t Tick(double nowMs) const;

private:
    std::unordered_map<uint64_t, int32_t> lastStart_;  // (kind, power, slot) -> start tick at the previous poll
    bool valid_ = false;
    double anchorMs_ = 0;
    double anchorTick_ = 0;
};

// Power SNO -> internal name (analysis/sno_powers.tsv: sno, name), e.g. "Generic_EnchantressCheatDeathPassive",
// and a name for people: "Enchantress Cheat Death Passive".
class PowerNames {
public:
    [[nodiscard]] bool Load(const std::filesystem::path& path);
    // nullptr for powers not in the table
    [[nodiscard]] const std::string* Code(uint32_t sno) const;
    // "Power <sno>" for powers not in the table
    [[nodiscard]] std::string Readable(uint32_t sno) const;
    [[nodiscard]] size_t size() const { return codes_.size(); }

    // "Generic_EnchantressCheatDeathPassive" -> "Enchantress Cheat Death Passive" (class prefixes such as
    // "Generic_" or "DemonHunter_" dropped, CamelCase and digits split)
    [[nodiscard]] static std::string Prettify(const std::string& code);

private:
    std::unordered_map<uint32_t, std::string> codes_;
};

}  // namespace d3hud
