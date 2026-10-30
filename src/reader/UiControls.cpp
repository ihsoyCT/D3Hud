#include "UiControls.h"

#include <algorithm>
#include <cstring>
#include <print>
#include <string_view>
#include <unordered_map>

namespace d3hud {

static constexpr uint64_t kFlagsOffset = 0x18, kPathOffset = 0x38, kRectOffset = 1176,
                          kAnimOffset = 1224, kTooltipAcdOffset = 0xCB0;
static constexpr uint64_t kVisibleFlag = 4;

UiPathScan::UiPathScan(const std::vector<std::string>& paths) : paths_(paths), copies_(paths.size()) {
    for (size_t i = 0; i < paths_.size(); ++i) {
        index_[paths_[i]] = i;
        longest_ = std::max(longest_, paths_[i].size());
    }
}

void UiPathScan::Match(const uint8_t* data, size_t n, uint64_t addr, size_t chunkSize) {
    const std::string_view hay(reinterpret_cast<const char*>(data), n);
    const size_t limit = MatchLimit(n, chunkSize, tail());
    for (size_t p = hay.find("Root."); p != std::string_view::npos && p < limit;
         p = hay.find("Root.", p + 1)) {
        const size_t end = hay.find('\0', p);
        if (end == std::string_view::npos) break;
        auto it = index_.find(hay.substr(p, end - p));
        if (it != index_.end()) copies_[it->second].push_back(addr + p);
    }
}

int UiPathScan::Finish(const Process& proc, UiControls* ui, bool verbose) {
    // The control is the copy a table entry points at (entry path + 512 -> control).
    int found = 0;
    for (size_t i = 0; i < paths_.size(); ++i) {
        uint64_t control = 0;
        for (uint64_t entryPath : copies_[i]) {
            uint64_t target = 0;
            if (!proc.Read(entryPath + 512, &target, 8)) continue;
            if (std::find(copies_[i].begin(), copies_[i].end(), target + kPathOffset) != copies_[i].end()) {
                control = target;
                found++;
                break;
            }
        }
        ui->SetControl(i, control);
        if (verbose && control == 0) std::print("  ui control not found: {}\n", paths_[i]);
    }
    if (verbose) std::print("ui controls: {} of {} found\n", found, paths_.size());
    return found;
}

int UiControls::Find(const Process& proc, bool verbose) {
    UiPathScan scan(paths_);
    SharedSweep(proc, {&scan});
    return scan.Finish(proc, this, verbose);
}

void UiControls::SetControls(const std::vector<uint64_t>& controls) {
    std::lock_guard<std::mutex> lock(mu_);
    controls_ = controls;
    controls_.resize(paths_.size());
}

void UiControls::SetControl(size_t i, uint64_t addr) {
    std::lock_guard<std::mutex> lock(mu_);
    if (i < controls_.size()) controls_[i] = addr;
}

std::vector<uint64_t> UiControls::GetControls() const {
    std::lock_guard<std::mutex> lock(mu_);
    return controls_;
}

uint64_t UiControls::Control(size_t i) const {
    std::lock_guard<std::mutex> lock(mu_);
    return i < controls_.size() ? controls_[i] : 0;
}

size_t UiControls::MissingCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    size_t n = 0;
    for (uint64_t c : controls_) n += c == 0 ? 1 : 0;
    return n;
}

bool UiControls::ValidateControl(const Process& proc, size_t i) const {
    const uint64_t addr = Control(i);
    if (addr == 0) return false;
    uint64_t flags = 0;
    char head[8] = {};
    return proc.Read(addr + kFlagsOffset, &flags, 8) &&
           proc.Read(addr + kPathOffset, head, sizeof(head)) && memcmp(head, "Root.", 5) == 0;
}

std::vector<UiControlInfo> UiControls::List(const Process& proc, const std::string& filter) {
    std::unordered_map<std::string, uint64_t> found;  // path -> control
    const size_t kChunk = 16u << 20, kTail = 512;
    std::vector<uint8_t> buf;
    char check[512];
    for (const RegionInfo& r : proc.Regions(MEM_PRIVATE, true)) {
        for (size_t off = 0; off < r.size; off += kChunk) {
            const size_t n = std::min(kChunk + kTail, r.size - off);
            buf.resize(n);
            if (!proc.Read(r.base + off, buf.data(), n)) continue;
            const std::string_view hay(reinterpret_cast<const char*>(buf.data()), n);
            for (size_t p = hay.find("Root."); p != std::string_view::npos && p < kChunk;
                 p = hay.find("Root.", p + 1)) {
                const size_t end = hay.find('\0', p);
                if (end == std::string_view::npos || end - p >= sizeof(check)) continue;
                const std::string_view path = hay.substr(p, end - p);
                if (path.find(filter) == std::string_view::npos) continue;
                // a table entry: path + 512 points at the control, whose own path matches
                uint64_t target = 0;
                if (!proc.Read(r.base + off + p + 512, &target, 8) || target == 0) continue;
                if (!proc.Read(target + kPathOffset, check, path.size() + 1)) continue;
                if (memcmp(check, path.data(), path.size()) != 0 || check[path.size()] != '\0') continue;
                found.emplace(std::string(path), target);
            }
        }
    }
    std::vector<UiControlInfo> out;
    for (const auto& [path, addr] : found) {
        UiControls one({path});
        one.controls_[0] = addr;
        out.push_back({path, addr, one.Read(proc, 0)});
    }
    std::sort(out.begin(), out.end(), [](const auto& l, const auto& r) { return l.path < r.path; });
    return out;
}

UiControlState UiControls::Read(const Process& proc, size_t i) const {
    UiControlState s;
    const uint64_t addr = Control(i);
    if (addr == 0) return s;
    uint64_t flags = 0;
    uint8_t geo[kAnimOffset + 4 - kRectOffset];
    if (!proc.Read(addr + kFlagsOffset, &flags, 8) ||
        !proc.Read(addr + kRectOffset, geo, sizeof(geo)))
        return s;
    s.found = true;
    s.visible = (flags & kVisibleFlag) != 0;
    memcpy(&s.rect, geo, sizeof(UiRect));
    memcpy(&s.animState, geo + (kAnimOffset - kRectOffset), 4);
    return s;
}

bool UiControls::ReadFloats(const Process& proc, size_t i, uint64_t offset, float* out, size_t count) const {
    const uint64_t addr = Control(i);
    return addr != 0 && proc.Read(addr + offset, out, count * sizeof(float));
}

bool UiControls::TooltipItem(const Process& proc, size_t i, uint32_t* acdId) const {
    const uint64_t addr = Control(i);
    if (addr == 0) return false;
    uint64_t flags = 0;
    if (!proc.Read(addr + kFlagsOffset, &flags, 8) || (flags & kVisibleFlag) == 0) return false;
    return proc.Read(addr + kTooltipAcdOffset, acdId, 4) && *acdId != 0 && *acdId != 0xFFFFFFFF;
}

}  // namespace d3hud
