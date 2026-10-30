#include "Process.h"

#include <cstdio>
#include <print>

namespace d3hud {

std::vector<ProcInfo> Process::List(const char* nameFilter) {
    std::vector<ProcInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            char name[64];
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, name, sizeof(name), nullptr, nullptr);
            if (nameFilter == nullptr || strstr(name, nameFilter) != nullptr) {
                out.push_back({pe.th32ProcessID, name});
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

std::optional<Process> Process::OpenByPid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (h == nullptr) return std::nullopt;
    Process p;
    p.handle_ = h;
    p.pid_ = pid;
    char name[64] = {};
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (pe.th32ProcessID == pid) {
                    WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, name, sizeof(name), nullptr,
                                        nullptr);
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }
    p.name_ = name;
    return p;
}

std::optional<Process> Process::OpenByName(const std::string& namePart) {
    auto procs = List(namePart.c_str());
    if (procs.empty()) return std::nullopt;
    if (procs.size() > 1) {
        std::print(stderr, "warning: {} processes match '{}' — using pid {} ({})\n", procs.size(),
                namePart.c_str(), static_cast<unsigned long>(procs[0].pid), procs[0].name.c_str());
    }
    return OpenByPid(procs[0].pid);
}

Process::~Process() {
    if (handle_) CloseHandle(handle_);
}
Process::Process(Process&& o) noexcept : handle_(o.handle_), pid_(o.pid_), name_(std::move(o.name_)) {
    o.handle_ = nullptr;
}

bool Process::Read(uint64_t addr, void* buf, size_t size) const {
    SIZE_T got = 0;
    if (!ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(addr), buf, size, &got)) return false;
    return got == size;
}

std::vector<uint8_t> Process::ReadVec(uint64_t addr, size_t size) const {
    std::vector<uint8_t> v(size);
    if (!Read(addr, v.data(), size)) v.clear();
    return v;
}

std::vector<ModuleInfo> Process::Modules() const {
    std::vector<ModuleInfo> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
    if (snap == INVALID_HANDLE_VALUE) return out;
    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            char name[260];
            WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, name, sizeof(name), nullptr, nullptr);
            out.push_back({reinterpret_cast<uint64_t>(me.modBaseAddr), me.modBaseSize, name});
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return out;
}

std::optional<ModuleInfo> Process::FindModule(const std::string& namePart) const {
    for (const auto& m : Modules()) {
        if (m.name.find(namePart) != std::string::npos) return m;
    }
    return std::nullopt;
}

uint64_t Process::creationTime() const {
    FILETIME creation = {}, exit = {}, kernel = {}, user = {};
    if (!GetProcessTimes(handle_, &creation, &exit, &kernel, &user)) return 0;
    return (static_cast<uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
}

std::vector<RegionInfo> Process::Regions(DWORD typeFilter, bool readableOnly) const {
    std::vector<RegionInfo> out;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint64_t addr = reinterpret_cast<uint64_t>(si.lpMinimumApplicationAddress);
    const uint64_t maxAddr = reinterpret_cast<uint64_t>(si.lpMaximumApplicationAddress);
    MEMORY_BASIC_INFORMATION mbi;
    while (addr < maxAddr) {
        if (VirtualQueryEx(handle_, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0) break;
        const bool committed = mbi.State == MEM_COMMIT;
        const bool readable = (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ |
                                              PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY |
                                              PAGE_EXECUTE_WRITECOPY)) != 0;
        const bool guard = (mbi.Protect & PAGE_GUARD) != 0;
        const bool typeOk = typeFilter == 0 || (mbi.Type & typeFilter) != 0;
        if (committed && readable && !guard && typeOk) {
            out.push_back({reinterpret_cast<uint64_t>(mbi.BaseAddress), mbi.RegionSize, mbi.Protect,
                           mbi.State, mbi.Type});
        }
        addr = reinterpret_cast<uint64_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return out;
}

}  // namespace d3hud
