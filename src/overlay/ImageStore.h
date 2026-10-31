// PNG files as Direct3D textures for the overlay (the plugins' d3hud.image). Decoded with the Windows Imaging
// Component, so nothing is vendored. A file is loaded once; the textures live as long as the store.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"

namespace d3hud {

class ImageStore {
public:
    ImageStore();
    ~ImageStore();
    ImageStore(const ImageStore&) = delete;
    ImageStore& operator=(const ImageStore&) = delete;

    void SetDevice(ID3D11Device* device) { device_ = device; }

    struct Loaded {
        int id = 0;  // > 0
        int width = 0, height = 0;
    };
    // Loads the PNG at `file` (or finds it again); the size limits are in PluginAssets.h. On failure `error` says why.
    [[nodiscard]] bool Load(const std::filesystem::path& file, Loaded* out, std::string* error);

    // The texture to hand to ImGui for an id from Load; 0 for an unknown id.
    [[nodiscard]] ImTextureID Texture(int id) const;

private:
    struct Entry {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        int width = 0, height = 0;
    };
    ID3D11Device* device_ = nullptr;
    std::vector<Entry> images_;  // id = index + 1
    std::map<std::filesystem::path, int> byFile_;
    bool comInitialised_ = false;  // we called CoInitializeEx and must balance it
};

}  // namespace d3hud
