// Game UI controls, found by their path (e.g. "Root.NormalLayer.inventory_dialog_
// mainPage.inventory_button_backpack"). Layout (TurboHUD 2.6.10 code, unchanged in
// the current client - verified live):
//   +0x18 u64 flags (bit 2 = visible)   +0x30 u64 path hash   +0x38 char[] path
//   +1176 f32 x0, y0, x1, y1 (rect, D3 virtual UI units)    +1224 i32 anim state
//   +0xCB0 u32 ACD id of the item a tooltip control shows
// The UI manager's control table has 536-byte entries holding the path inline and,
// 512 bytes after the path, a pointer to the control - used to tell the control
// apart from the other copies of its path string in memory.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Process.h"
#include "SharedScan.h"
#include "Snapshot.h"

namespace d3hud {

struct UiControlState {
    bool found = false;
    bool visible = false;
    UiRect rect;
    int32_t animState = 0;
};

struct UiControlInfo {
    std::string path;
    uint64_t address = 0;
    UiControlState state;
};

// Sweepable UI-control discovery (see SharedScan.h): Match collects copies of
// every wanted path string; Finish chases the control pointer for each.
class UiControls;

// Sweepable UI-control discovery (see SharedScan.h): Match collects copies of
// every wanted path string; Finish chases the control pointer for each.
class UiPathScan : public ChunkMatcher {
public:
    explicit UiPathScan(const std::vector<std::string>& paths);
    size_t tail() const override { return longest_ + 1; }
    void Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) override;
    // Resolves the control of every path and installs it in `ui`; returns found count.
    int Finish(const Process& proc, UiControls* ui, bool verbose = false);

private:
    const std::vector<std::string>& paths_;
    std::unordered_map<std::string_view, size_t> index_;
    std::vector<std::vector<uint64_t>> copies_;
    size_t longest_ = 0;
};

class UiControls {
public:
    explicit UiControls(std::vector<std::string> paths) : paths_(std::move(paths)), controls_(paths_.size()) {}

    // One pass over private memory for all paths; returns how many were found.
    int Find(const Process& proc, bool verbose = false);

    UiControlState Read(const Process& proc, size_t index) const;
    // ACD id shown by a tooltip control; false if hidden or unknown.
    bool TooltipItem(const Process& proc, size_t index, uint32_t* acdId) const;
    // Raw read of `count` floats at control + offset (e.g. the big map's pan
    // offsets at localmap +3216/+3220); false if the control is unknown.
    bool ReadFloats(const Process& proc, size_t index, uint64_t offset, float* out, size_t count) const;

    size_t size() const { return paths_.size(); }
    const std::vector<std::string>& paths() const { return paths_; }
    // Controls not yet resolved (dialog never opened / not created at scan time).
    size_t MissingCount() const;
    uint64_t Control(size_t i) const;

    // Cache support: install/extract resolved control addresses. Thread-safe:
    // the publish loop reads while a background scan fills missing controls.
    void SetControls(const std::vector<uint64_t>& controls);
    void SetControl(size_t i, uint64_t addr);
    std::vector<uint64_t> GetControls() const;
    // Cheap sanity check for a cached control: readable flags + the path at +0x38.
    bool ValidateControl(const Process& proc, size_t index) const;

    // Diagnostics: every live control whose path contains `filter` (one pass
    // over private memory), sorted by path.
    static std::vector<UiControlInfo> List(const Process& proc, const std::string& filter);

private:
    std::vector<std::string> paths_;
    mutable std::mutex mu_;
    std::vector<uint64_t> controls_;  // 0 = not found
};

}  // namespace d3hud
