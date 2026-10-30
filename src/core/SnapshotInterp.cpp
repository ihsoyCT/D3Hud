#include "SnapshotInterp.h"

#include <cmath>
#include <unordered_map>

namespace d3hud {

static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static float LerpAngle(float a, float b, float t) {
    const float kTwoPi = 6.28318531f;
    float d = std::fmod(b - a, kTwoPi);
    if (d > 3.14159265f) d -= kTwoPi;
    if (d < -3.14159265f) d += kTwoPi;
    return a + d * t;
}

void InterpolateSnapshot(const WorldSnapshot& a, const WorldSnapshot& b, float t, WorldSnapshot* out) {
    *out = b;
    if (t >= 1.f) return;
    if (t < 0.f) t = 0.f;
    out->playerPos = {Lerp(a.playerPos.x, b.playerPos.x, t), Lerp(a.playerPos.y, b.playerPos.y, t)};
    out->playerHeading = LerpAngle(a.playerHeading, b.playerHeading, t);

    std::unordered_map<uint64_t, const ActorSnapshot*> prev;
    prev.reserve(a.actorCount);
    for (uint16_t i = 0; i < a.actorCount; ++i) prev[a.actors[i].id] = &a.actors[i];
    for (uint16_t i = 0; i < out->actorCount; ++i) {
        ActorSnapshot& o = out->actors[i];
        auto it = prev.find(o.id);
        if (it == prev.end()) continue;
        const ActorSnapshot& p = *it->second;
        o.world = {Lerp(p.world.x, o.world.x, t), Lerp(p.world.y, o.world.y, t)};
        o.heading = LerpAngle(p.heading, o.heading, t);
    }
}

}  // namespace d3hud
