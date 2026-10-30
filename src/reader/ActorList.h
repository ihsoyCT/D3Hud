// Actor list (current client): every world object — players, monsters, gizmos
// (pylons, shrines, chests), ground items — is a record in one flat array.
//
// Record layout (stride 0x4F8), see analysis/ENGINE_MINING.md:
//   +0x00 u32 id      low 16 bits = slot index; 0xFFFFFFFF = free slot
//   +0x08 char[128]   name (empty in this client)
//   +0x88 u32 acd id  0xFFFFFFFF = no game-state link: client-only actor, or a
//                     DEAD monster (cleared on death: 10/10 alive->dead in F10 dumps)
//   +0x8C u32 actor SNO
//   +0x90 u32         0xFFFFFFFF for gizmos/players/items; monsters carry a value
//                     (used only to anchor the scan signature)
//   +0xA0 f32[4]      rotation quaternion (rotation about the up axis: z, w)
//   +0xB0 f32[3]      position (x, z, y) — y is height
//   +0x250 u32        activity/LOD state: 5 near the player (< ~60 u), 0 or 4 far
//                     away - alive or dead alike (NOT a death flag; F10 dumps)
//
// The array is found offset-free: a live record whose id index equals its slot,
// followed by the next slot's record, implies base = addr - slot * stride.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Process.h"
#include "SharedScan.h"

namespace d3hud {

constexpr uint32_t kActorStride = 0x4F8;
constexpr uint32_t kActorHeaderBytes = 0x260;  // bytes of each record we decode

struct RawActor {
    uint32_t slot = 0;
    uint32_t id = 0;
    uint32_t acd = 0xFFFFFFFF;
    uint32_t sno = 0;
    float x = 0.f, z = 0.f, y = 0.f;
    float heading = 0.f;  // radians, from the quaternion; not yet validated in-game
    uint32_t state = 0;   // +0x250 activity/LOD state (debug only)
    uint32_t tag90 = 0;   // +0x90: -1 for gizmos/players/items, a value for monsters
    bool dead = false;    // monsters only: acd link cleared (see +0x88)
    bool hasAcd() const { return acd != 0xFFFFFFFF; }
};

// Pure helpers (unit-tested). `rec` must hold kActorHeaderBytes bytes.
bool DecodeActor(const uint8_t* rec, uint32_t slot, RawActor* out);
// Scan signature: `rec` holds kActorStride + kActorHeaderBytes bytes (record + next record).
// On match, *slot receives the record's slot index.
bool MatchesArraySignature(const uint8_t* rec, uint32_t* slot);

// Sweepable actor-array base discovery (see SharedScan.h): Match collects
// implied-base votes, Finish picks the winner.
class ActorBaseScan : public ChunkMatcher {
public:
    size_t tail() const override { return kActorStride + kActorHeaderBytes; }
    void Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) override;
    // Best-voted implied array base; false when the sweep saw nothing.
    bool Finish(uint64_t* base, int* votes, bool verbose = false);

private:
    std::unordered_map<uint64_t, int> tally_;
};

// Cheap cache validation: the record shape must still hold at `base`.
bool ValidateActorBase(const Process& proc, uint64_t base);

class ActorList {
public:
    // One pass over private memory; picks the array base with the most signature votes.
    static bool FindBase(const Process& proc, uint64_t* base, int* votes, bool verbose = false);

    // Reads all live actors; false if the array is no longer readable.
    [[nodiscard]] bool Read(const Process& proc, std::vector<RawActor>* out) const;

    void setBase(uint64_t base) { base_ = base; }
    uint64_t base() const { return base_; }
    bool valid() const { return base_ != 0; }

private:
    uint64_t base_ = 0;
};

}  // namespace d3hud
