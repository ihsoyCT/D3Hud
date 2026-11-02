#include "PlayerStats.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>

namespace d3hud {

HeroClass HeroClassFromActorName(const char* name) {
    if (name == nullptr) return HeroClass::Unknown;
    std::string s(name);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    struct Entry { const char* token; HeroClass cls; };
    static const Entry kTable[] = {
        {"barbarian", HeroClass::Barbarian}, {"crusader", HeroClass::Crusader}, {"demonhunter", HeroClass::DemonHunter},
        {"monk", HeroClass::Monk},           {"necromancer", HeroClass::Necromancer}, {"witchdoctor", HeroClass::WitchDoctor},
        {"wizard", HeroClass::Wizard},
    };
    for (const Entry& e : kTable)
        if (s.find(e.token) != std::string::npos) return e.cls;
    return HeroClass::Unknown;
}

static float AsFloat(uint32_t bits) {
    float f;
    memcpy(&f, &bits, 4);
    return std::isfinite(f) ? f : 0.f;
}

static uint16_t Small(uint32_t v) { return static_cast<uint16_t>(std::min<uint32_t>(v, 0xFFFF)); }

void ExtractPlayerStats(const std::vector<AttribEntry>& attrs, PlayerSnapshot* out) {
    PlayerSnapshot p;
    p.heroClass = out->heroClass;
    p.valid = 1;
    constexpr int kTypes = 16;  // resource ids are small numbers
    float cur[kTypes] = {}, max[kTypes] = {}, regen[kTypes] = {};
    uint32_t type[2] = {0xFFFFFFFF, 0xFFFFFFFF};
    uint32_t xpHi = 0, xpLo = 0, altHi = 0, altLo = 0;
    for (const AttribEntry& e : attrs) {
        const bool plain = e.modifier == kAttribNoModifier;
        const uint32_t m = e.modifier;
        if (plain) {
            switch (e.index) {
                case kAttrDeathCount: p.deaths = Small(e.value); break;
                case kAttrStrengthTotal: p.strength = AsFloat(e.value); break;
                case kAttrDexterityTotal: p.dexterity = AsFloat(e.value); break;
                case kAttrIntelligenceTotal: p.intelligence = AsFloat(e.value); break;
                case kAttrVitalityTotal: p.vitality = AsFloat(e.value); break;
                case kAttrArmorTotal: p.armor = AsFloat(e.value); break;
                case kAttrExperienceNextHi: xpHi = e.value; break;
                case kAttrExperienceNextLo: xpLo = e.value; break;
                case kAttrAltExperienceNextHi: altHi = e.value; break;
                case kAttrAltExperienceNextLo: altLo = e.value; break;
                case kAttrGoldFindTotal: p.goldFind = AsFloat(e.value); break;
                case kAttrMagicFindTotal: p.magicFind = AsFloat(e.value); break;
                case kAttrLevel: p.level = Small(e.value); break;
                case kAttrAltLevel: p.paragonLevel = Small(e.value); break;
                case kAttrResistanceAll: p.resistAll = AsFloat(e.value); break;
                case kAttrLifeCur: p.life = AsFloat(e.value); break;
                case kAttrLifeMaxTotal: p.lifeMax = AsFloat(e.value); break;
                case kAttrHitpointsRegenTotal: p.lifeRegen = AsFloat(e.value); break;
                case kAttrResourceTypePrimary: type[0] = e.value; break;
                case kAttrResourceTypeSecondary: type[1] = e.value; break;
                case kAttrHighestSoloRift: p.highestSoloRift = Small(e.value); break;
                case kAttrHighestUnlockedRift: p.highestUnlockedRift = Small(e.value); break;
                case kAttrLastRiftLevel: p.lastRiftLevel = Small(e.value); break;
                case kAttrMovementScalarTotal: p.moveSpeed = AsFloat(e.value); break;
                case kAttrAttacksPerSecondTotal: p.attackSpeed = AsFloat(e.value); break;
                case kAttrCooldownReductionCapped: p.cooldownReduction = AsFloat(e.value); break;
                case kAttrParagonPointsAvailable: p.paragonPointsFree = Small(e.value); break;
                default: break;
            }
        } else if (m < kTypes) {
            switch (e.index) {
                case kAttrResourceCur: cur[m] = AsFloat(e.value); break;
                case kAttrResourceMaxTotal: max[m] = AsFloat(e.value); break;
                case kAttrResourceRegenTotal: regen[m] = AsFloat(e.value); break;
                default: break;
            }
        }
        if (!plain && e.index == kAttrResistanceTotal && m >= 1 && m <= 6) p.resist[m - 1] = AsFloat(e.value);
    }
    p.xpToNext = (static_cast<uint64_t>(xpHi) << 32) | xpLo;
    p.paragonXpToNext = (static_cast<uint64_t>(altHi) << 32) | altLo;
    for (int i = 0; i < 2; ++i) {
        p.resourceType[i] = 0xFF;  // none
        if (type[i] < static_cast<uint32_t>(kTypes)) {
            p.resourceType[i] = static_cast<uint8_t>(type[i]);
            p.resource[i] = cur[type[i]];
            p.resourceMax[i] = max[type[i]];
            p.resourceRegen[i] = regen[type[i]];
        }
    }
    *out = p;
}

void ExtractSkills(const std::vector<AttribEntry>& attrs, const std::function<bool(uint32_t)>& ignore,
                   std::vector<SkillSnapshot>* out) {
    out->clear();
    std::unordered_map<uint32_t, SkillSnapshot> byPower;
    for (const AttribEntry& e : attrs)
        if (e.index == kAttrSkillTotal && e.modifier != kAttribNoModifier && e.value != 0 && !(ignore && ignore(e.modifier))) {
            SkillSnapshot s;
            s.sno = e.modifier;
            byPower.emplace(e.modifier, s);
        }
    if (byPower.empty()) return;
    for (const AttribEntry& e : attrs) {
        if (e.modifier == kAttribNoModifier) continue;
        const auto it = byPower.find(e.modifier);
        if (it == byPower.end()) continue;
        SkillSnapshot& s = it->second;
        if (e.index >= kAttrRuneFirst && e.index < kAttrRuneFirst + 5 && e.value != 0)
            s.rune = static_cast<uint8_t>(e.index - kAttrRuneFirst + 1);
        else if (e.index == kAttrTrait && e.value != 0)
            s.passive = 1;
        else if (e.index == kAttrPowerCooldown && e.value != kCooldownIdleFinish)
            s.cooldownEndTick = static_cast<int32_t>(e.value);
        else if (e.index == kAttrPowerCooldownStart && e.value != kCooldownIdleStart)
            s.cooldownStartTick = static_cast<int32_t>(e.value);
    }
    for (const auto& [sno, s] : byPower) out->push_back(s);
    std::sort(out->begin(), out->end(), [](const SkillSnapshot& a, const SkillSnapshot& b) { return a.sno < b.sno; });
    if (out->size() > static_cast<size_t>(kMaxSkills)) out->resize(kMaxSkills);
}

void ExtractPassives(const std::vector<AttribEntry>& attrs, std::vector<PassiveSnapshot>* out) {
    out->clear();
    struct Slot { uint32_t count = 0, end = 0; };
    std::map<std::pair<uint32_t, uint32_t>, Slot> slots;  // (power, icon slot)
    for (const AttribEntry& e : attrs) {
        if (e.modifier == kAttribNoModifier) continue;
        if (e.index >= kAttrBuffCount && e.index < kAttrBuffCount + kBuffSlots) slots[{e.modifier, e.index - kAttrBuffCount}].count = e.value;
        else if (e.index >= kAttrBuffEndTick && e.index < kAttrBuffEndTick + kBuffSlots) slots[{e.modifier, e.index - kAttrBuffEndTick}].end = e.value;
    }
    for (const auto& [key, s] : slots) {
        if (s.count == 0 || s.end != 0) continue;
        if (out->size() >= static_cast<size_t>(kMaxPassives)) break;
        PassiveSnapshot p;
        p.sno = key.first;
        p.slot = static_cast<uint16_t>(key.second);
        p.stacks = Small(s.count);
        out->push_back(p);
    }
}

}  // namespace d3hud
