// What a plugin may ask the host to load from its own folder (pure, unit-tested). The host reads the file, not the
// plugin: a plugin names an image by a plain file name and never sees a path, so it cannot reach anything outside its
// folder, cannot write, and cannot read any other kind of file.
#pragma once

#include <cstddef>
#include <string_view>

namespace d3hud {

constexpr size_t kMaxImageFileBytes = 2u << 20;  // a PNG bigger than this is refused
constexpr int kMaxImageSide = 1024;              // pixels
constexpr int kMaxImagesPerPlugin = 64;          // GPU memory is not unlimited

// "fire.png": letters, digits, '_' and '-', then ".png". No directories, no dots in the name, no other extensions.
inline bool IsPlainImageName(std::string_view name) {
    constexpr std::string_view ext = ".png";
    if (name.size() <= ext.size() || name.size() > 64 || !name.ends_with(ext)) return false;
    for (const char c : name.substr(0, name.size() - ext.size())) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

}  // namespace d3hud
