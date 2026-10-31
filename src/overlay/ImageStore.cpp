#include "ImageStore.h"

#include <wincodec.h>

#include "PluginAssets.h"
#include "Win32.h"

namespace d3hud {

using Microsoft::WRL::ComPtr;

ImageStore::ImageStore() {
    // WIC is a COM service. S_OK / S_FALSE: initialised here (balance it); RPC_E_CHANGED_MODE: the thread already has COM
    // in another mode, which is fine for WIC.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    comInitialised_ = SUCCEEDED(hr);
}

ImageStore::~ImageStore() {
    images_.clear();  // release the textures before COM goes away
    if (comInitialised_) CoUninitialize();
}

bool ImageStore::Load(const std::filesystem::path& file, Loaded* out, std::string* error) {
    const auto fail = [&](const char* why) {
        if (error != nullptr) *error = why;
        return false;
    };
    if (const auto known = byFile_.find(file); known != byFile_.end()) {
        const Entry& e = images_[static_cast<size_t>(known->second) - 1];
        *out = {known->second, e.width, e.height};
        return true;
    }
    if (device_ == nullptr) return fail("no graphics device");
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(file, ec);
    if (ec) return fail("file not found");
    if (bytes > kMaxImageFileBytes) return fail("file too large");

    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return fail("image decoder not available");
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
        return fail("not a readable image");
    GUID container{};
    if (FAILED(decoder->GetContainerFormat(&container)) || container != GUID_ContainerFormatPng) return fail("not a PNG");
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return fail("not a readable image");
    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0 || w > static_cast<UINT>(kMaxImageSide) ||
        h > static_cast<UINT>(kMaxImageSide))
        return fail("image too large");

    ComPtr<IWICFormatConverter> convert;  // straight (not premultiplied) RGBA: what ImGui's blending expects
    if (FAILED(factory->CreateFormatConverter(&convert)) ||
        FAILED(convert->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom)))
        return fail("cannot convert the image");
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
    if (FAILED(convert->CopyPixels(nullptr, w * 4, static_cast<UINT>(pixels.size()), pixels.data())))
        return fail("cannot read the image");

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{pixels.data(), w * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device_->CreateTexture2D(&desc, &init, &texture))) return fail("cannot create the texture");
    Entry entry;
    entry.width = static_cast<int>(w);
    entry.height = static_cast<int>(h);
    if (FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &entry.view))) return fail("cannot create the texture");

    images_.push_back(std::move(entry));
    const int id = static_cast<int>(images_.size());
    byFile_[file] = id;
    *out = {id, static_cast<int>(w), static_cast<int>(h)};
    return true;
}

ImTextureID ImageStore::Texture(int id) const {
    if (id < 1 || static_cast<size_t>(id) > images_.size()) return 0;
    return reinterpret_cast<ImTextureID>(images_[static_cast<size_t>(id) - 1].view.Get());
}

}  // namespace d3hud
