// windows.h the way this project wants it: without the rarely used parts and without the
// min/max macros (they break std::min/std::max). Include this instead of <windows.h>.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <utility>

namespace d3hud {

// Owns a kernel HANDLE and closes it on destruction; move-only. nullptr and INVALID_HANDLE_VALUE
// (what CreateFile returns on failure) both mean "empty".
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE h) : h_(h == INVALID_HANDLE_VALUE ? nullptr : h) {}
    UniqueHandle(UniqueHandle&& o) noexcept : h_(std::exchange(o.h_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& o) noexcept {
        if (this != &o) {
            reset();
            h_ = std::exchange(o.h_, nullptr);
        }
        return *this;
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    ~UniqueHandle() { reset(); }

    [[nodiscard]] HANDLE get() const { return h_; }
    explicit operator bool() const { return h_ != nullptr; }
    void reset(HANDLE h = nullptr) {
        if (h_ != nullptr) CloseHandle(h_);
        h_ = h == INVALID_HANDLE_VALUE ? nullptr : h;
    }

private:
    HANDLE h_ = nullptr;
};

}  // namespace d3hud
