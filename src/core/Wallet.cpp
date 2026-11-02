#include "Wallet.h"

#include <algorithm>
#include <cstring>

namespace d3hud {

namespace {

uint32_t Word(const uint8_t* p, size_t byteOffset) {
    uint32_t v;
    memcpy(&v, p + byteOffset, 4);
    return v;
}

uint64_t Amount(const uint8_t* p, size_t record) {
    uint64_t v;
    memcpy(&v, p + record * kWalletRecord, 8);
    return v;
}

uint32_t Clamp32(uint64_t v) { return static_cast<uint32_t>(std::min<uint64_t>(v, 0xFFFFFFFFu)); }

// record index (0 = gold) of each material
constexpr int kMaterialRecord[kMaterialCount] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 21, 20, 13, 14, 15, 16};

}  // namespace

const char* MaterialKey(int index) {
    static const char* const kKeys[kMaterialCount] = {
        "reusable_parts", "arcane_dust",        "veiled_crystal",       "deaths_breath",
        "forgotten_soul", "khanduran_rune",     "caldeum_nightshade",   "arreat_war_tapestry",
        "corrupted_angel_flesh", "westmarch_holy_water", "primordial_ashes", "greater_rift_keystone",
        "vial_of_putridness", "idol_of_terror", "heart_of_fright", "leorics_regret",
    };
    return index >= 0 && index < kMaterialCount ? kKeys[index] : "";
}

bool LooksLikeWallet(const uint8_t* p) {
    for (size_t k = 0; k < 12; ++k) {
        const size_t o = k * kWalletRecord;
        if (Word(p, o + 0x10) != k + 1 || Word(p, o + 0x14) != 0 || Word(p, o + 0x08) != 0 || Word(p, o + 0x0C) != 0)
            return false;
    }
    return true;
}

void DecodeWallet(const uint8_t* p, WalletSnapshot* out) {
    *out = WalletSnapshot{};
    out->gold = Amount(p, 0);
    out->bloodShards = Clamp32(Amount(p, 1));
    for (int i = 0; i < kMaterialCount; ++i) out->materials[i] = Clamp32(Amount(p, static_cast<size_t>(kMaterialRecord[i])));
    out->valid = 1;
}

}  // namespace d3hud
