// Where the program lives and how to open files there, for any characters in the path.
// GetModuleFileNameA + fopen would use the ANSI code page: a user name or folder with other
// characters ends up as '?'. Everything exe-relative goes through these instead.
#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

#include "Win32.h"

namespace d3hud {

// Full path of the running exe.
inline std::filesystem::path ExePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return std::filesystem::path(buf);
        }
        buf.resize(buf.size() * 2);  // truncated: the path is longer than the buffer
    }
}

// Folder of the running exe: where data files, d3hud.ini and reader.log live.
inline std::filesystem::path ExeDir() { return ExePath().parent_path(); }

// UTF-8 text of a path for ImGui and logs (path::string() would use the ANSI code page).
inline std::string Utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// FILE* for a path with any characters (mode is a wide string: L"rb", L"w", ...); nullptr on failure.
[[nodiscard]] inline FILE* OpenFile(const std::filesystem::path& p, const wchar_t* mode) {
    FILE* f = nullptr;
    return _wfopen_s(&f, p.c_str(), mode) == 0 ? f : nullptr;
}

}  // namespace d3hud
