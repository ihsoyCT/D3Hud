// Placement of the plugins' movable on-screen boxes (pure logic, unit-tested). A box remembers where its top left
// corner is as a fraction of the screen (so it survives another resolution); a negative fraction means "never moved":
// the box then sits at the default position its plugin asked for. A box always stays fully on the screen.
#pragma once

#include <algorithm>

#include "Snapshot.h"

namespace d3hud {

constexpr float kBoxUnset = -1.f;

// Top left corner in pixels of a box of `size` pixels.
inline Vec2 BoxOrigin(Vec2 stored, Vec2 defaults, Vec2 size, float screenW, float screenH) {
    const float fx = stored.x >= 0.f ? stored.x : defaults.x;
    const float fy = stored.y >= 0.f ? stored.y : defaults.y;
    return {std::clamp(fx * screenW, 0.f, std::max(0.f, screenW - size.x)),
            std::clamp(fy * screenH, 0.f, std::max(0.f, screenH - size.y))};
}

// The corner after the mouse moved by `delta` pixels, kept on the screen.
inline Vec2 BoxMoved(Vec2 origin, Vec2 delta, Vec2 size, float screenW, float screenH) {
    return {std::clamp(origin.x + delta.x, 0.f, std::max(0.f, screenW - size.x)),
            std::clamp(origin.y + delta.y, 0.f, std::max(0.f, screenH - size.y))};
}

// What is stored for a corner in pixels.
inline Vec2 BoxFraction(Vec2 origin, float screenW, float screenH) {
    if (screenW <= 0.f || screenH <= 0.f) return {kBoxUnset, kBoxUnset};
    return {std::clamp(origin.x / screenW, 0.f, 1.f), std::clamp(origin.y / screenH, 0.f, 1.f)};
}

}  // namespace d3hud
