// Auto-salvage executor: with the blacksmith's salvage tab open, clicks the
// anvil, then every salvage candidate in the backpack (see Salvage.h),
// confirming the legendary prompt. Sends real mouse input to the game, one
// step per tick, and verifies each item really disappeared before the next.
//
// Stops at once on: Esc, the user moving the mouse, the game losing focus,
// the salvage tab or inventory closing, or an item that does not vanish.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>

#include "Salvage.h"
#include "Snapshot.h"

namespace d3hud {

class SalvageRunner {
public:
    // `overlay` is the HUD window (client px = overlay px), `game` the D3 window.
    void Start(HWND overlay, HWND game);
    void Stop(const char* why);
    bool Active() const { return step_ != Step::Idle; }
    void Tick(const WorldSnapshot& snap, const SafeArea& safe, const SalvageRules& rules);
    const std::string& status() const { return status_; }

private:
    enum class Step { Idle, Anvil, NextItem, WaitGone };
    // Moves the cursor now, clicks on a later tick (the game wants a hover first).
    void QueueClick(const UiRect& uiRect, float screenW, float screenH);
    bool CursorMovedByUser() const;

    Step step_ = Step::Idle;
    HWND overlay_ = nullptr, game_ = nullptr;
    ULONGLONG nextAt_ = 0, startedAt_ = 0, clickedAt_ = 0, confirmAt_ = 0;
    bool clickPending_ = false;
    ULONGLONG clickAt_ = 0;
    POINT cursor_ = {-1, -1};  // where we last put the cursor (screen px)
    uint64_t target_ = 0;      // item being salvaged
    int done_ = 0;
    std::string status_ = "idle";
};

}  // namespace d3hud
