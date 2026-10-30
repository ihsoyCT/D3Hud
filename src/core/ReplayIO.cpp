#include "ReplayIO.h"

namespace d3hud {
namespace {
constexpr uint32_t kReplayMagic = 0x52483344;  // 'D3HR'
constexpr uint32_t kReplayVersion = 7;  // follows kSnapshotVersion (frame layout)
}  // namespace

bool ReplayWriter::Open(const char* path, float frameIntervalMs) {
    Close();
    fopen_s(&f_, path, "wb");
    if (f_ == nullptr) return false;
    hdr_ = {kReplayMagic, kReplayVersion, 0, frameIntervalMs};
    fwrite(&hdr_, sizeof(hdr_), 1, f_);
    count_ = 0;
    return true;
}

void ReplayWriter::Append(const WorldSnapshot& snap) {
    if (f_ == nullptr) return;
    fwrite(&snap, sizeof(snap), 1, f_);
    count_++;
}

void ReplayWriter::Close() {
    if (f_ == nullptr) return;
    fseek(f_, 0, SEEK_SET);
    hdr_.frameCount = count_;
    fwrite(&hdr_, sizeof(hdr_), 1, f_);
    fclose(f_);
    f_ = nullptr;
}

bool ReplayReader::Open(const char* path) {
    Close();
    fopen_s(&f_, path, "rb");
    if (f_ == nullptr) return false;
    if (fread(&hdr_, sizeof(hdr_), 1, f_) != 1 || hdr_.magic != kReplayMagic ||
        hdr_.version != kReplayVersion || hdr_.frameCount == 0) {
        fclose(f_);
        f_ = nullptr;
        return false;
    }
    return true;
}

bool ReplayReader::Read(uint32_t index, WorldSnapshot* out) const {
    if (f_ == nullptr || index >= hdr_.frameCount) return false;
    const long long off = sizeof(ReplayHeader) + static_cast<long long>(index) * sizeof(WorldSnapshot);
    if (_fseeki64(f_, off, SEEK_SET) != 0) return false;
    return fread(out, sizeof(WorldSnapshot), 1, f_) == 1;
}

void ReplayReader::Close() {
    if (f_ != nullptr) {
        fclose(f_);
        f_ = nullptr;
    }
}

}  // namespace d3hud
