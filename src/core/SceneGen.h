// Deterministic synthetic world — the M0 data source and regression-test fixture.
// Positions are closed-form functions of the frame index, so a replay of the
// same seed reproduces the exact same scene.
#pragma once

#include "Snapshot.h"

namespace d3hud {

class SceneGen {
public:
    explicit SceneGen(uint32_t seed = 1, double frameIntervalMs = 1000.0 / 15.0);

    // Advance internal time by one frame and fill `out`.
    void Generate(WorldSnapshot* out);

    uint64_t frameIndex() const { return frame_; }
    double frameIntervalMs() const { return intervalMs_; }

private:
    uint32_t seed_;
    double intervalMs_;
    uint64_t frame_ = 0;
};

}  // namespace d3hud
