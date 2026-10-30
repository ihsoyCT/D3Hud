// Discovery results cached per game process (pid + creation time): a reader
// restart against the same running game validates the bases in milliseconds
// instead of re-sweeping the whole heap.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace d3hud {

struct BasesCache {
    uint32_t pid = 0;
    uint64_t created = 0;  // process creation FILETIME
    uint64_t actorBase = 0;
    std::vector<uint64_t> acd, attrib, scene;
    std::vector<uint64_t> ui;  // control addresses, same order as GameUiPaths()

    [[nodiscard]] bool Save(const std::string& path) const;
    // True only when the file matches this exact process instance.
    [[nodiscard]] bool Load(const std::string& path);
};

}  // namespace d3hud
