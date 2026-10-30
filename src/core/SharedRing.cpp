#include "SharedRing.h"

#include <cstring>

namespace d3hud {
namespace {
constexpr uint32_t kRingMagic = 0x524E4752;  // 'RNGR'
}

bool SharedRing::Create() {
    const SIZE_T size = sizeof(Header) + sizeof(WorldSnapshot) * kCapacity;
    map_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, kName);
    if (map_ == nullptr) return false;
    header_ = static_cast<Header*>(MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, size));
    if (header_ == nullptr) {
        CloseHandle(map_);
        map_ = nullptr;
        return false;
    }
    header_->magic = kRingMagic;
    header_->capacity = kCapacity;
    header_->writeIndex = 0;
    slots_ = reinterpret_cast<WorldSnapshot*>(header_ + 1);
    return true;
}

bool SharedRing::Open() {
    const SIZE_T size = sizeof(Header) + sizeof(WorldSnapshot) * kCapacity;
    map_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, kName);
    if (map_ == nullptr) return false;
    header_ = static_cast<Header*>(MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, size));
    if (header_ == nullptr || header_->magic != kRingMagic) {
        Close();
        return false;
    }
    slots_ = reinterpret_cast<WorldSnapshot*>(header_ + 1);
    return true;
}

void SharedRing::Close() {
    if (header_ != nullptr) {
        UnmapViewOfFile(header_);
        header_ = nullptr;
        slots_ = nullptr;
    }
    if (map_ != nullptr) {
        CloseHandle(map_);
        map_ = nullptr;
    }
}

void SharedRing::Publish(const WorldSnapshot& snap) {
    if (header_ == nullptr) return;
    const uint64_t idx = header_->writeIndex;
    const uint32_t slot = static_cast<uint32_t>(idx % kCapacity);
    slots_[slot] = snap;  // single writer, reader may briefly see torn data - acceptable for HUD
    MemoryBarrier();
    header_->writeIndex = idx + 1;
}

bool SharedRing::Poll(WorldSnapshot* out) {
    if (header_ == nullptr) return false;
    const uint64_t w = header_->writeIndex;
    MemoryBarrier();
    if (w < nextRead_) nextRead_ = 0;  // writer restarted (Create() resets the index)
    if (w == 0 || w <= nextRead_) {
        if (w > 0 && nextRead_ + kCapacity < w) nextRead_ = w - kCapacity;  // catch up
        return false;
    }
    if (nextRead_ + kCapacity < w) nextRead_ = w - kCapacity;
    const uint32_t slot = static_cast<uint32_t>(nextRead_ % kCapacity);
    *out = slots_[slot];
    MemoryBarrier();
    nextRead_++;
    return true;
}

}  // namespace d3hud
