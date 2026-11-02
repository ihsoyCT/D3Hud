// Finds the wallet table (gold, blood shards, crafting materials; see core/Wallet.h) in the game's memory and
// reads it. The table is found by shape - twelve 24-byte records with ids 1, 2, 3, ... - and its address is kept;
// the shape is checked on every read, so a moved or freed table is noticed and searched for again.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "Process.h"
#include "SharedScan.h"
#include "Wallet.h"

namespace d3hud {

class WalletScan : public ChunkMatcher {
public:
    size_t tail() const override { return kWalletBytes; }
    void Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) override {
        const size_t limit = n < chunkSize ? n : chunkSize;
        for (size_t o = 0; o + kWalletBytes <= n && o < limit; o += 8) {
            // cheap test first: the id word of the first record
            uint32_t id;
            memcpy(&id, data + o + 0x10, 4);
            if (id == 1 && LooksLikeWallet(data + o)) hits_.push_back(addr + o);
        }
    }
    const std::vector<uint64_t>& hits() const { return hits_; }

private:
    std::vector<uint64_t> hits_;
};

class WalletTable {
public:
    // One pass over private memory. Several copies can exist (the game's UI keeps its own): all are kept and the
    // first that reads is used. Returns the number found.
    int Discover(const Process& proc) {
        WalletScan scan;
        SharedSweep(proc, {&scan});
        std::lock_guard lock(mu_);
        bases_ = scan.hits();
        return static_cast<int>(bases_.size());
    }

    // False when no known address holds a table.
    [[nodiscard]] bool Read(const Process& proc, WalletSnapshot* out) const {
        std::vector<uint64_t> bases;
        {
            std::lock_guard lock(mu_);
            bases = bases_;
        }
        // The game keeps a table per player slot (eight, evenly spaced; the others are empty): the first one that
        // holds anything is ours. An empty account (all zero) is still reported, from the first table that reads.
        uint8_t buf[kWalletBytes];
        bool any = false;
        for (uint64_t b : bases) {
            if (!proc.Read(b, buf, sizeof(buf)) || !LooksLikeWallet(buf)) continue;
            WalletSnapshot w;
            DecodeWallet(buf, &w);
            const bool filled = w.gold != 0 || w.bloodShards != 0;
            if (!any || filled) {
                *out = w;
                any = true;
            }
            if (filled) break;
        }
        return any;
    }

    std::vector<uint64_t> Bases() const {
        std::lock_guard lock(mu_);
        return bases_;
    }

    bool known() const {
        std::lock_guard lock(mu_);
        return !bases_.empty();
    }

private:
    mutable std::mutex mu_;
    std::vector<uint64_t> bases_;
};

}  // namespace d3hud
