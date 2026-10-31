#include "Effects.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace d3hud {

namespace {

struct BuffSlot {
    int32_t start = 0, end = 0;
    uint32_t count = 0;
};
struct CooldownPair {
    uint32_t finish = kCooldownIdleFinish, start = kCooldownIdleStart;
};

// (kind, power, slot) in one integer
uint64_t EffectKey(const EffectSnapshot& e) {
    return (static_cast<uint64_t>(e.kind) << 40) | (static_cast<uint64_t>(e.sno) << 8) | e.slot;
}

}  // namespace

void ExtractEffects(const std::vector<AttribEntry>& attrs, std::vector<EffectSnapshot>* out) {
    out->clear();
    std::map<std::pair<uint32_t, uint32_t>, BuffSlot> buffs;  // (power, slot)
    std::map<uint32_t, CooldownPair> cooldowns;               // power
    for (const AttribEntry& a : attrs) {
        if (a.modifier == kAttribNoModifier || a.modifier == 0) continue;
        if (a.index >= kAttrBuffStartTick && a.index < kAttrBuffStartTick + kBuffSlots)
            buffs[{a.modifier, a.index - kAttrBuffStartTick}].start = static_cast<int32_t>(a.value);
        else if (a.index >= kAttrBuffEndTick && a.index < kAttrBuffEndTick + kBuffSlots)
            buffs[{a.modifier, a.index - kAttrBuffEndTick}].end = static_cast<int32_t>(a.value);
        else if (a.index >= kAttrBuffCount && a.index < kAttrBuffCount + kBuffSlots)
            buffs[{a.modifier, a.index - kAttrBuffCount}].count = a.value;
        else if (a.index == kAttrPowerCooldown)
            cooldowns[a.modifier].finish = a.value;
        else if (a.index == kAttrPowerCooldownStart)
            cooldowns[a.modifier].start = a.value;
    }
    for (const auto& [key, b] : buffs) {
        if (b.count == 0 || b.end <= 0 || out->size() >= static_cast<size_t>(kMaxEffects)) continue;
        // One buff can sit in several icon slots with the very same times (Vengeance: four, live): that is a
        // single effect. Slots of one power with different times stay separate.
        const auto same = [&](const EffectSnapshot& o) {
            return o.kind == EffectKind::Buff && o.sno == key.first && o.startTick == b.start && o.endTick == b.end;
        };
        if (std::any_of(out->begin(), out->end(), same)) continue;
        EffectSnapshot e;
        e.sno = key.first;
        e.slot = static_cast<uint8_t>(key.second);
        e.kind = EffectKind::Buff;
        e.startTick = b.start;
        e.endTick = b.end;
        e.count = static_cast<uint16_t>(std::min<uint32_t>(b.count, 0xFFFF));
        out->push_back(e);
    }
    for (const auto& [sno, c] : cooldowns) {
        if (c.finish == kCooldownIdleFinish || c.start == kCooldownIdleStart || c.finish == 0 ||
            out->size() >= static_cast<size_t>(kMaxEffects))
            continue;
        EffectSnapshot e;
        e.sno = sno;
        e.kind = EffectKind::Cooldown;
        e.startTick = static_cast<int32_t>(c.start);
        e.endTick = static_cast<int32_t>(c.finish);
        out->push_back(e);
    }
}

void GameClock::Update(double nowMs, const EffectSnapshot* fx, size_t n) {
    const double est = valid_ ? anchorTick_ + (nowMs - anchorMs_) * (kTicksPerSecond / 1000.0) : 0.0;
    // the newest start tick that changed since the previous poll was set within this poll interval
    bool haveFresh = false;
    int32_t freshStart = 0;
    std::unordered_map<uint64_t, int32_t> now;
    now.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        now[EffectKey(fx[i])] = fx[i].startTick;
        const auto it = lastStart_.find(EffectKey(fx[i]));
        if (it != lastStart_.end() && it->second == fx[i].startTick) continue;
        if (!haveFresh || fx[i].startTick > freshStart) {
            freshStart = fx[i].startTick;
            haveFresh = true;
        }
    }
    lastStart_ = std::move(now);
    // Adopt it when the extrapolation is certainly too low (the start is in its future) or far too high
    // (e.g. the game paused); otherwise keep the anchor and its steadier extrapolation. Starts that are
    // far in the past are old stacks being decremented, not "now".
    if (haveFresh && (!valid_ || freshStart > est || est - freshStart > 90.0) && (!valid_ || est - freshStart <= 180.0)) {
        anchorTick_ = freshStart;
        anchorMs_ = nowMs;
        valid_ = true;
    }
    if (!valid_) return;
    // an effect that still runs after its end tick: the estimate is ahead of the game
    const double cur = anchorTick_ + (nowMs - anchorMs_) * (kTicksPerSecond / 1000.0);
    for (size_t i = 0; i < n; ++i)
        if (cur > fx[i].endTick + 30.0) {
            anchorTick_ = fx[i].endTick + 30.0;
            anchorMs_ = nowMs;
            break;
        }
}

int32_t GameClock::Tick(double nowMs) const {
    if (!valid_) return -1;
    return static_cast<int32_t>(std::lround(anchorTick_ + (nowMs - anchorMs_) * (kTicksPerSecond / 1000.0)));
}

bool PowerNames::Load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const unsigned long sno = strtoul(line.c_str(), nullptr, 10);
        if (sno != 0) codes_[static_cast<uint32_t>(sno)] = line.substr(tab + 1);
    }
    return !codes_.empty();
}

const std::string* PowerNames::Code(uint32_t sno) const {
    const auto it = codes_.find(sno);
    return it == codes_.end() ? nullptr : &it->second;
}

std::string PowerNames::Readable(uint32_t sno) const {
    const std::string* code = Code(sno);
    return code != nullptr ? Prettify(*code) : "Power " + std::to_string(sno);
}

std::string PowerNames::Prettify(const std::string& code) {
    std::string s = code;
    const size_t us = s.find('_');
    if (us != std::string::npos && us + 1 < s.size()) s = s.substr(us + 1);  // "Generic_", "DemonHunter_", ...
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '_') {
            if (!out.empty() && out.back() != ' ') out += ' ';
            continue;
        }
        if (i > 0 && !out.empty() && out.back() != ' ') {
            const unsigned char p = static_cast<unsigned char>(s[i - 1]);
            const unsigned char nx = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
            const bool lowerToUpper = std::isupper(c) && (std::islower(p) || std::isdigit(p));
            const bool acronymEnd = std::isupper(c) && std::isupper(p) && std::islower(nx);  // "DOTSomething"
            const bool letterToDigit = std::isdigit(c) && std::isalpha(p) && !(i >= 2 && std::isdigit(static_cast<unsigned char>(s[i - 2])));  // "735x1" stays one token
            if (lowerToUpper || acronymEnd || letterToDigit) out += ' ';
        }
        out += static_cast<char>(c);
    }
    return out;
}

}  // namespace d3hud
