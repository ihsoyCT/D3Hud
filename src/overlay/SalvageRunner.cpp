#include "SalvageRunner.h"

#include <cstdlib>
#include <format>

namespace d3hud {

static constexpr ULONGLONG kHoverMs = 60;        // cursor rests on a target before the click
static constexpr ULONGLONG kItemTimeoutMs = 3000;  // an item must vanish within this
static constexpr ULONGLONG kFocusGraceMs = 1500;  // after Start: the game regains focus

void SalvageRunner::Start(HWND overlay, HWND game) {
    if (Active()) return;
    overlay_ = overlay;
    game_ = game;
    if (game_ == nullptr || !IsWindow(game_)) {
        status_ = "needs the game window (start the overlay with --target \"Diablo III\")";
        return;
    }
    // click came from the overlay's ImGui: hand focus to the game (retry once -
    // Windows sometimes refuses the first switch)
    for (int i = 0; i < 2 && GetForegroundWindow() != game_; ++i) {
        SetForegroundWindow(game_);
        Sleep(50);
    }
    step_ = Step::Anvil;
    startedAt_ = GetTickCount64();
    nextAt_ = startedAt_ + 150;
    clickPending_ = false;
    cursor_ = {-1, -1};
    target_ = 0;
    done_ = 0;
    status_ = "starting";
}

void SalvageRunner::Stop(const char* why) {
    if (!Active()) return;
    step_ = Step::Idle;
    clickPending_ = false;
    status_ = std::format("stopped after {} item(s): {}", done_, why);
}

bool SalvageRunner::CursorMovedByUser() const {
    if (cursor_.x < 0) return false;
    POINT p;
    if (!GetCursorPos(&p)) return false;
    return std::abs(p.x - cursor_.x) > 12 || std::abs(p.y - cursor_.y) > 12;
}

void SalvageRunner::QueueClick(const UiRect& uiRect, float screenW, float screenH) {
    const UiRect r = UiToScreen(uiRect, screenW, screenH);
    POINT p{static_cast<LONG>((r.x0 + r.x1) * 0.5f), static_cast<LONG>((r.y0 + r.y1) * 0.5f)};
    ClientToScreen(overlay_, &p);
    SetCursorPos(p.x, p.y);
    cursor_ = p;
    clickPending_ = true;
    clickAt_ = GetTickCount64() + kHoverMs;
}

void SalvageRunner::Tick(const WorldSnapshot& snap, const SafeArea& safe, const SalvageRules& rules) {
    if (!Active()) return;
    const ULONGLONG now = GetTickCount64();
    // --- abort conditions (checked every frame) ---
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) return Stop("Esc");
    if (CursorMovedByUser()) return Stop("mouse moved");
    if (now - startedAt_ > kFocusGraceMs && GetForegroundWindow() != game_) return Stop("game lost focus");
    if (!snap.ui.salvageOpen || !snap.ui.inventoryOpen) return Stop("salvage tab / inventory closed");

    RECT client;
    GetClientRect(overlay_, &client);
    const float w = static_cast<float>(client.right), h = static_cast<float>(client.bottom);

    if (clickPending_) {
        if (now < clickAt_) return;
        INPUT in[2] = {};
        in[0].type = in[1].type = INPUT_MOUSE;
        in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, in, sizeof(INPUT));
        clickPending_ = false;
        return;
    }
    if (now < nextAt_) return;

    // the legendary "are you sure?" prompt can appear after any item click
    if (snap.ui.confirmOpen) {
        if (now - confirmAt_ > 500) {
            QueueClick(snap.ui.confirmOk, w, h);
            confirmAt_ = now;
            nextAt_ = now + 250;
        }
        return;
    }

    switch (step_) {
        case Step::Anvil:
            if (snap.ui.salvageButton.x1 <= snap.ui.salvageButton.x0) return Stop("anvil button not found");
            QueueClick(snap.ui.salvageButton, w, h);
            status_ = "anvil";
            step_ = Step::NextItem;
            nextAt_ = now + 350 + rules.extraDelayMs;
            break;
        case Step::NextItem: {
            // column by column, top to bottom (the order the game lays items out)
            const ItemSnapshot* best = nullptr;
            for (uint16_t i = 0; i < snap.itemCount; ++i) {
                const ItemSnapshot& it = snap.items[i];
                if (!IsSalvageCandidate(it, safe, rules, snap.items, snap.itemCount)) continue;
                if (best == nullptr || it.gridX < best->gridX || (it.gridX == best->gridX && it.gridY < best->gridY))
                    best = &it;
            }
            const bool limitReached = rules.maxPerRun > 0 && done_ >= rules.maxPerRun;
            if (best == nullptr || limitReached) {
                step_ = Step::Idle;
                status_ = std::format("done: {} item(s) salvaged{}", done_, limitReached ? " (run limit reached)" : "");
                return;
            }
            UiRect r;
            if (!ItemScreenRect(snap.ui, best->location, best->gridX, best->gridY, best->height, &r))
                return Stop("item position unknown");
            target_ = best->id;
            QueueClick(r, w, h);
            clickedAt_ = now;
            status_ = std::format("salvaging {} ({} done)", best->name, done_);
            step_ = Step::WaitGone;
            nextAt_ = now + 150 + rules.extraDelayMs;
            break;
        }
        case Step::WaitGone: {
            bool present = false;
            for (uint16_t i = 0; i < snap.itemCount && !present; ++i)
                present = snap.items[i].id == target_ && snap.items[i].location == kItemLocationBackpack;
            if (!present) {
                ++done_;
                step_ = Step::NextItem;
                nextAt_ = now + 120 + rules.extraDelayMs;
            } else if (now - clickedAt_ > kItemTimeoutMs) {
                Stop("an item did not salvage (salvage cursor lost?)");
            }
            break;
        }
        case Step::Idle:
            break;
    }
}

}  // namespace d3hud
