// Where the program lives and how to open files there, for any characters in the path.
// GetModuleFileNameA + fopen would use the ANSI code page: a user name or folder with other
// characters ends up as '?'. Everything exe-relative goes through these instead.
#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

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

// An open FILE* that is closed when it goes out of scope (also on early returns). Converts to FILE*
// so it can be passed to fprintf, fgets, std::print...; compare with nullptr to test for failure.
class File {
public:
    File() = default;
    explicit File(FILE* f) : f_(f) {}
    File(File&& o) noexcept : f_(std::exchange(o.f_, nullptr)) {}
    File& operator=(File&& o) noexcept {
        if (this != &o) {
            close();
            f_ = std::exchange(o.f_, nullptr);
        }
        return *this;
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { close(); }
    operator FILE*() const { return f_; }  // NOLINT: intentionally implicit

private:
    void close() {
        if (f_ != nullptr) fclose(f_);
        f_ = nullptr;
    }
    FILE* f_ = nullptr;
};

// Opens a path with any characters (mode is a wide string: L"rb", L"w", ...); empty File on failure.
[[nodiscard]] inline File OpenFile(const std::filesystem::path& p, const wchar_t* mode) {
    FILE* f = nullptr;
    return _wfopen_s(&f, p.c_str(), mode) == 0 ? File(f) : File();
}

}  // namespace d3hud
