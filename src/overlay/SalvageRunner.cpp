#include "SalvageRunner.h"

#include <algorithm>
#include <cstdlib>
#include <format>

namespace d3hud {

static constexpr ULONGLONG kItemTimeoutMs = 3000;  // a clicked item must vanish within this
static constexpr ULONGLONG kBatchTimeoutMs = 6000;  // "salvage all" must finish within this
static constexpr ULONGLONG kFocusGraceMs = 1500;   // after Start: the game regains focus
static constexpr ULONGLONG kBurstHoverMs = 30, kBurstPostMs = 40;    // ordinary items, burst mode
static constexpr ULONGLONG kCarefulHoverMs = 60, kCarefulPostMs = 150;  // legendaries / burst off
static constexpr size_t kMaxInFlight = 4;

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
    step_ = Step::Batch;
    startedAt_ = GetTickCount64();
    nextAt_ = startedAt_ + 150;
    clickPending_ = false;
    cursor_ = {-1, -1};
    flights_.clear();
    batchIdx_ = 0;
    done_ = 0;
    status_ = "starting";
}

void SalvageRunner::Stop(const char* why) {
    if (!Active()) return;
    step_ = Step::Idle;
    clickPending_ = false;
    flights_.clear();
    status_ = std::format("stopped after {} item(s): {}", done_, why);
}

void SalvageRunner::Finish(const char* note) {
    step_ = Step::Idle;
    flights_.clear();
    status_ = std::format("done: {} item(s) salvaged{}", done_, note);
}

bool SalvageRunner::CursorMovedByUser() const {
    if (cursor_.x < 0) return false;
    POINT p;
    if (!GetCursorPos(&p)) return false;
    return std::abs(p.x - cursor_.x) > 12 || std::abs(p.y - cursor_.y) > 12;
}

void SalvageRunner::QueueClick(const UiRect& uiRect, float screenW, float screenH, ULONGLONG hoverMs,
                               ULONGLONG postMs) {
    const UiRect r = UiToScreen(uiRect, screenW, screenH);
    POINT p{static_cast<LONG>((r.x0 + r.x1) * 0.5f), static_cast<LONG>((r.y0 + r.y1) * 0.5f)};
    ClientToScreen(overlay_, &p);
    SetCursorPos(p.x, p.y);
    cursor_ = p;
    clickPending_ = true;
    clickAt_ = GetTickCount64() + hoverMs;
    postClickMs_ = postMs;
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
        nextAt_ = now + postClickMs_ + static_cast<ULONGLONG>(rules.extraDelayMs);  // let the game see the click
        return;
    }
    if (now < nextAt_) return;

    // the legendary "are you sure?" prompt can appear after any item click
    if (snap.ui.confirmOpen) {
        if (now - confirmAt_ > 500) {
            QueueClick(snap.ui.confirmOk, w, h, kCarefulHoverMs, 250);
            confirmAt_ = now;
        }
        return;
    }

    // clicked items that are gone from the backpack are done; one that stays is an error
    for (size_t i = 0; i < flights_.size();) {
        bool present = false;
        for (uint16_t k = 0; k < snap.itemCount && !present; ++k)
            present = snap.items[k].id == flights_[i].id && snap.items[k].location == kItemLocationBackpack;
        if (!present) {
            ++done_;
            flights_.erase(flights_.begin() + static_cast<std::ptrdiff_t>(i));
        } else if (now - flights_[i].at > kItemTimeoutMs) {
            return Stop("an item did not salvage (salvage cursor lost?)");
        } else {
            ++i;
        }
    }

    // next backpack candidate that is not already in flight: column by column, top to bottom
    // (the order the game lays items out)
    auto nextCandidate = [&]() -> const ItemSnapshot* {
        const ItemSnapshot* best = nullptr;
        for (uint16_t i = 0; i < snap.itemCount; ++i) {
            const ItemSnapshot& it = snap.items[i];
            if (!IsSalvageCandidate(it, safe, rules, snap.items, snap.itemCount)) continue;
            bool inFlight = false;
            for (const Flight& f : flights_) inFlight |= f.id == it.id;
            if (inFlight) continue;
            if (best == nullptr || it.gridX < best->gridX || (it.gridX == best->gridX && it.gridY < best->gridY)) best = &it;
        }
        return best;
    };

    switch (step_) {
        case Step::Batch: {
            // the game's own "salvage all blue / yellow" buttons, where safe: one click clears a quality
            while (batchIdx_ < 2) {
                const ItemRarity quality = batchIdx_ == 0 ? ItemRarity::Magic : ItemRarity::Rare;
                const UiRect& button = batchIdx_ == 0 ? snap.ui.salvageMagicButton : snap.ui.salvageRareButton;
                ++batchIdx_;
                if (!rules.useGameSalvageAll || button.x1 <= button.x0) continue;
                if (!CanSalvageAllOfQuality(quality, snap.items, snap.itemCount, safe, rules)) continue;
                batchQuality_ = quality;
                batchCount_ = 0;
                for (uint16_t i = 0; i < snap.itemCount; ++i)
                    batchCount_ += snap.items[i].location == kItemLocationBackpack && snap.items[i].rarity == quality;
                QueueClick(button, w, h, kCarefulHoverMs, 200);
                batchSince_ = now;
                status_ = quality == ItemRarity::Magic ? "salvage all blue" : "salvage all yellow";
                step_ = Step::WaitBatch;
                return;
            }
            step_ = Step::Anvil;
            break;
        }
        case Step::WaitBatch: {
            bool any = false;
            for (uint16_t i = 0; i < snap.itemCount && !any; ++i)
                any = snap.items[i].location == kItemLocationBackpack && snap.items[i].rarity == batchQuality_;
            if (!any) {
                done_ += batchCount_;
                nextAt_ = now + 120;
                step_ = Step::Batch;  // the other quality, then the anvil
            } else if (now - batchSince_ > kBatchTimeoutMs) {
                Stop("the game's salvage-all button did not finish");
            }
            break;
        }
        case Step::Anvil:
            if (nextCandidate() == nullptr) return Finish("");  // nothing left for the item-by-item pass
            if (snap.ui.salvageButton.x1 <= snap.ui.salvageButton.x0) return Stop("anvil button not found");
            QueueClick(snap.ui.salvageButton, w, h, kCarefulHoverMs, 250);
            status_ = "anvil";
            step_ = Step::NextItem;
            break;
        case Step::NextItem: {
            const ItemSnapshot* best = nextCandidate();
            const bool limitReached = rules.maxPerRun > 0 && done_ + static_cast<int>(flights_.size()) >= rules.maxPerRun;
            if (best == nullptr || limitReached) {
                if (!flights_.empty()) return;  // wait for the last clicks to land
                return Finish(limitReached ? " (run limit reached)" : "");
            }
            // legendary/set items raise a confirmation: one at a time, with nothing else in flight
            const bool careful = !rules.burst || best->rarity == ItemRarity::Legendary || best->rarity == ItemRarity::Set;
            if ((careful && !flights_.empty()) || flights_.size() >= kMaxInFlight) return;
            UiRect r;
            if (!ItemScreenRect(snap.ui, best->location, best->gridX, best->gridY, best->height, &r))
                return Stop("item position unknown");
            QueueClick(r, w, h, careful ? kCarefulHoverMs : kBurstHoverMs, careful ? kCarefulPostMs : kBurstPostMs);
            flights_.push_back({best->id, now});
            status_ = std::format("salvaging {} ({} done)", best->name, done_);
            if (careful) step_ = Step::WaitGone;
            break;
        }
        case Step::WaitGone:
            if (flights_.empty()) {
                nextAt_ = now + 60 + static_cast<ULONGLONG>(rules.extraDelayMs);
                step_ = Step::NextItem;
            }
            break;
        case Step::Idle:
            break;
    }
}

}  // namespace d3hud
