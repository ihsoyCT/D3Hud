// Smooth rendering of a lower-rate snapshot stream: blend positions between
// the previous and the newest snapshot. Actors are matched by id; actors only
// present in the newest snapshot are taken as-is (no blending).
#pragma once

#include "Snapshot.h"

namespace d3hud {

// out = b with player/actor positions and headings blended from a (t = 0) to b (t = 1).
void InterpolateSnapshot(const WorldSnapshot& a, const WorldSnapshot& b, float t, WorldSnapshot* out);

}  // namespace d3hud
