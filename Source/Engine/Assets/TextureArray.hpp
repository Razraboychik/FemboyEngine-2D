// FemboyEngine2D - runtime texture-array asset.
#pragma once

#include "Engine/RHI/RHI.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby
{
class VirtualFileSystem;

namespace Assets
{

struct AtlasLayer
{
    std::string name;
    // Path inside the VFS, e.g. "atlas/tiles.ftex"
    std::string source;
    // Unknown layers start as Blend; the cooked header refines this to Opaque
    // or Cutout when the source alpha is binary.
    TextureAlphaMode alphaMode = TextureAlphaMode::Blend;
};

struct AtlasDesc
{
    std::string name = "main";
    // 0 = take from the first layer header
    std::uint32_t width = 0;
    // 0 = take from the first layer header
    std::uint32_t height = 0;
    bool srgb = false;
    // Filled by Load/LoadFromManifest from the first cooked layer. The editor
    TextureFormat format = TextureFormat::Rgba8;
    std::vector<AtlasLayer> layers;
    // Per-layer classification resolved from the cooked alpha flags.
    std::vector<TextureAlphaMode> layerAlphaModes;
};

// Parses atlas.toml. Accepted forms for a layer:
[[nodiscard]] std::optional<AtlasDesc> ParseAtlasManifest(std::span<const std::byte> InTomlBytes,
                                                          std::string* InError = nullptr);

// Owns the GPU texture array. Move-only, no singleton: the caller decides how
class TextureArrayAsset
{
public:
    TextureArrayAsset() = default;
    ~TextureArrayAsset() = default;

    TextureArrayAsset(const TextureArrayAsset&) = delete;
    TextureArrayAsset& operator=(const TextureArrayAsset&) = delete;
    TextureArrayAsset(TextureArrayAsset&& InOther) noexcept;
    TextureArrayAsset& operator=(TextureArrayAsset&& InOther) noexcept;

    // Reads `manifestPath` from `vfs`, loads every layer, validates equal
    [[nodiscard]] bool Load(VirtualFileSystem& InVfs, Rhi::RHI& InGpu, std::string_view InManifestPath,
                            std::string* InError = nullptr);

    // Uploads a manifest that is already in memory.
    [[nodiscard]] bool LoadFromManifest(VirtualFileSystem& InVfs, Rhi::RHI& InGpu,
                                        std::span<const std::byte> InManifest, std::string* InError = nullptr);

    void Release(Rhi::RHI& InGpu);

    [[nodiscard]] bool IsValid() const noexcept
    {
        return MHandle.IsValid();
    }
    [[nodiscard]] Rhi::TextureArrayHandle Handle() const noexcept
    {
        return MHandle;
    }
    [[nodiscard]] const AtlasDesc& Description() const noexcept
    {
        return MDesc;
    }
    [[nodiscard]] std::span<const TextureAlphaMode> LayerAlphaModes() const noexcept
    {
        return MDesc.layerAlphaModes;
    }
    [[nodiscard]] std::uint32_t LayerIndex(std::string_view InName) const noexcept;

private:
    Rhi::TextureArrayHandle MHandle{};
    AtlasDesc MDesc;
};
}
}
