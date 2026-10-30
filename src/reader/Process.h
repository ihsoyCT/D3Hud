// Remote process access: open by pid/name, enumerate modules and memory
// regions, buffered ReadProcessMemory.
#pragma once

#include "Win32.h"
#include <tlhelp32.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace d3hud {

struct ProcInfo {
    DWORD pid = 0;
    std::string name;
};

struct ModuleInfo {
    uint64_t base = 0;
    uint64_t size = 0;
    std::string name;
};

struct RegionInfo {
    uint64_t base = 0;
    size_t size = 0;
    DWORD protect = 0;
    DWORD state = 0;
    DWORD type = 0;  // MEM_IMAGE / MEM_MAPPED / MEM_PRIVATE
};

class Process {
public:
    static std::vector<ProcInfo> List(const char* nameFilter = nullptr);
    static std::optional<Process> OpenByPid(DWORD pid);
    static std::optional<Process> OpenByName(const std::string& namePart);

    ~Process();
    Process(Process&& o) noexcept;
    Process& operator=(Process&&) = delete;
    Process(const Process&) = delete;

    bool Read(uint64_t addr, void* buf, size_t size) const;
    template <typename T>
    bool Read(uint64_t addr, T* out) const {
        return Read(addr, out, sizeof(T));
    }
    std::vector<uint8_t> ReadVec(uint64_t addr, size_t size) const;

    std::vector<ModuleInfo> Modules() const;
    std::optional<ModuleInfo> FindModule(const std::string& namePart) const;

    // Walk VirtualQueryEx; optionally filter by type (MEM_PRIVATE etc, 0 = all committed readable).
    std::vector<RegionInfo> Regions(DWORD typeFilter = 0) const;  // committed, readable, not guarded

    HANDLE handle() const { return handle_; }
    DWORD pid() const { return pid_; }
    const std::string& name() const { return name_; }
    // Process creation time (FILETIME, 0 on failure): with the pid it keys the
    // discovery-base cache — the same game process has the same heap layout.
    uint64_t creationTime() const;

private:
    Process() = default;
    HANDLE handle_ = nullptr;
    DWORD pid_ = 0;
    std::string name_;
};

}  // namespace d3hud
