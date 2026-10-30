// Auto-salvage executor: with the blacksmith's salvage tab open it
//   1. presses the game's "salvage all blue / yellow" buttons for qualities where that is safe
//      (every item of that quality is a salvage candidate, see CanSalvageAllOfQuality),
//   2. arms the anvil and clicks every remaining candidate in the backpack (see Salvage.h),
//      confirming the legendary prompt.
// Ordinary items are clicked in a burst: the next click does not wait for the previous item to
// vanish (a few clicks are "in flight" and verified afterwards). Legendary/set items raise a
// confirmation: the runner waits only for that prompt, clicks its OK and carries on. Sends real
// mouse input to the game.
//
// Stops at once on: Esc, the user moving the mouse, the game losing focus,
// the salvage tab or inventory closing, or an item that does not vanish.
#pragma once

#include "Win32.h"

#include <string>
#include <vector>

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
    enum class Step { Idle, Batch, WaitBatch, Anvil, NextItem, WaitGone, WaitPrompt };
    // Moves the cursor now, clicks on a later tick (the game wants a hover first); the next
    // action waits `postMs` after the click.
    void QueueClick(const UiRect& uiRect, float screenW, float screenH, ULONGLONG hoverMs, ULONGLONG postMs);
    bool CursorMovedByUser() const;
    void Finish(const char* note);

    struct Flight {
        uint64_t id;
        ULONGLONG at;
    };

    Step step_ = Step::Idle;
    HWND overlay_ = nullptr, game_ = nullptr;
    ULONGLONG nextAt_ = 0, startedAt_ = 0, confirmAt_ = 0;
    bool clickPending_ = false;
    ULONGLONG clickAt_ = 0, postClickMs_ = 100;
    POINT cursor_ = {-1, -1};  // where we last put the cursor (screen px)
    std::vector<Flight> flights_;  // clicked items not yet seen gone
    int batchIdx_ = 0;             // 0 = blue next, 1 = yellow next, 2 = done
    ItemRarity batchQuality_ = ItemRarity::Magic;
    int batchCount_ = 0;
    ULONGLONG batchSince_ = 0;
    uint64_t promptItem_ = 0;      // legendary whose confirmation we are waiting for
    ULONGLONG promptSince_ = 0;
    bool promptHandled_ = false;   // its OK was clicked
    int done_ = 0;
    std::string status_ = "idle";
};

}  // namespace d3hud
