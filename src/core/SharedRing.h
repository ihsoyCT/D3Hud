// Cross-process snapshot exchange: a named shared-memory ring of WorldSnapshots.
// Reader process writes at ~15 Hz; overlay polls for the latest frames.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>

#include "Snapshot.h"

namespace d3hud {

class SharedRing {
public:
    static constexpr uint32_t kCapacity = 16;
    static constexpr const char* kName = "Local\\D3HudRingV1";

    // Create (writer) or open (reader) the ring. Fails if mapping cannot be made.
    bool Create();
    bool Open();
    void Close();

    // Writer side: publish a snapshot (single writer assumed).
    void Publish(const WorldSnapshot& snap);

    // Reader side: non-blocking poll; returns true when a newer snapshot was
    // consumed into `out`. Skips stale slots and always delivers in order.
    bool Poll(WorldSnapshot* out);

    uint64_t latestIndex() const { return header_ ? header_->writeIndex : 0; }

private:
    struct Header {
        uint32_t magic;
        uint32_t capacity;
        volatile uint64_t writeIndex;
    };
    HANDLE map_ = nullptr;
    Header* header_ = nullptr;
    WorldSnapshot* slots_ = nullptr;
    uint64_t nextRead_ = 0;
};

}  // namespace d3hud
