#include "Engine/Assets/TextureArray.hpp"

#include "Engine/Assets/TextureFormat.hpp"
#include "Engine/Assets/PakFormat.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"

#include <toml.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace Fby::Assets
{
namespace
{
void SetError(std::string* InError, std::string InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::move(InMessage);
    }
}

[[nodiscard]] bool ValidDimensions(std::uint32_t InWidth, std::uint32_t InHeight) noexcept
{
    return InWidth > 0 && InHeight > 0 && InWidth <= 16384 && InHeight <= 16384;
}
}

std::optional<AtlasDesc> ParseAtlasManifest(std::span<const std::byte> InTomlBytes, std::string* InError)
{
    const std::string_view ScopedText(reinterpret_cast<const char*>(InTomlBytes.data()), InTomlBytes.size());
    toml::table ScopedRoot;
    try
    {
        ScopedRoot = toml::parse(ScopedText);
    }
    catch (const toml::parse_error& ScopedParseError)
    {
        SetError(InError, std::string("atlas manifest parse error: ") + std::string(ScopedParseError.description()));
        return std::nullopt;
    }

    AtlasDesc ScopedDesc;
    if (const auto* ScopedName = ScopedRoot["name"].as_string())
    {
        ScopedDesc.name = ScopedName->get();
    }
    if (const auto ScopedWidth = ScopedRoot["width"].value<std::int64_t>())
    {
        ScopedDesc.width = static_cast<std::uint32_t>(*ScopedWidth);
    }
    if (const auto ScopedHeight = ScopedRoot["height"].value<std::int64_t>())
    {
        ScopedDesc.height = static_cast<std::uint32_t>(*ScopedHeight);
    }
    if (const auto ScopedSrgb = ScopedRoot["srgb"].value<bool>())
    {
        ScopedDesc.srgb = *ScopedSrgb;
    }

    const toml::array* ScopedLayers = ScopedRoot["layers"].as_array();
    if (ScopedLayers == nullptr || ScopedLayers->empty())
    {
        SetError(InError, "atlas manifest has no 'layers' array");
        return std::nullopt;
    }

    for (const toml::node& ScopedNode : *ScopedLayers)
    {
        const toml::table* ScopedLayer = ScopedNode.as_table();
        if (ScopedLayer == nullptr)
        {
            SetError(InError, "every atlas layer must be a table with a 'source'");
            return std::nullopt;
        }
        const auto* ScopedSource = (*ScopedLayer)["source"].as_string();
        if (ScopedSource == nullptr || ScopedSource->get().empty())
        {
            SetError(InError, "atlas layer is missing 'source'");
            return std::nullopt;
        }
        AtlasLayer ScopedEntry;
        ScopedEntry.source = NormalizePakPath(ScopedSource->get());
        if (const auto* ScopedName = (*ScopedLayer)["name"].as_string())
        {
            ScopedEntry.name = ScopedName->get();
        }
        else
        {
            ScopedEntry.name = ScopedEntry.source;
        }
        ScopedDesc.layers.push_back(std::move(ScopedEntry));
    }

    if (ScopedDesc.width != 0 || ScopedDesc.height != 0)
    {
        if (!ValidDimensions(ScopedDesc.width, ScopedDesc.height))
        {
            SetError(InError, "atlas width/height must both be set and within 1..16384");
            return std::nullopt;
        }
    }
    return ScopedDesc;
}

TextureArrayAsset::TextureArrayAsset(TextureArrayAsset&& InOther) noexcept
    : MHandle(InOther.MHandle), MDesc(std::move(InOther.MDesc))
{
    InOther.MHandle = {};
}

TextureArrayAsset& TextureArrayAsset::operator=(TextureArrayAsset&& InOther) noexcept
{
    if (this == &InOther)
    {
        return *this;
    }
    // The move assignment cannot release the old GPU array: it has no device.
    MHandle = InOther.MHandle;
    MDesc = std::move(InOther.MDesc);
    InOther.MHandle = {};
    return *this;
}

bool TextureArrayAsset::Load(VirtualFileSystem& InVfs, Rhi::RHI& InGpu, std::string_view InManifestPath,
                             std::string* InError)
{
    const auto ScopedBytes = InVfs.Read(InManifestPath);
    if (!ScopedBytes)
    {
        SetError(InError, "atlas manifest not found: " + std::string(InManifestPath));
        return false;
    }
    return LoadFromManifest(InVfs, InGpu, *ScopedBytes, InError);
}

bool TextureArrayAsset::LoadFromManifest(VirtualFileSystem& InVfs, Rhi::RHI& InGpu,
                                         std::span<const std::byte> InManifest, std::string* InError)
{
    std::optional<AtlasDesc> ScopedParsed = ParseAtlasManifest(InManifest, InError);
    if (!ScopedParsed)
    {
        return false;
    }
    AtlasDesc ScopedDesc = std::move(*ScopedParsed);

    std::vector<std::byte> ScopedPixels;
    std::uint32_t ScopedWidth = ScopedDesc.width;
    std::uint32_t ScopedHeight = ScopedDesc.height;
    bool ScopedSrgb = ScopedDesc.srgb;
    TextureFormat ScopedFormat = TextureFormat::Rgba8;
    std::uint32_t ScopedMipLevels = 1;
    bool ScopedFormatKnown = false;

    for (const AtlasLayer& ScopedLayer : ScopedDesc.layers)
    {
        const auto ScopedBytes = InVfs.Read(ScopedLayer.source);
        if (!ScopedBytes)
        {
            SetError(InError, "atlas layer not found: " + ScopedLayer.source);
            return false;
        }

        // One shared validator for both generations: v1 single-mip RGBA8 and v2
        std::string ScopedParseError;
        const std::optional<CookedTextureView> ScopedCooked = ParseCookedTexture(*ScopedBytes, &ScopedParseError);
        if (!ScopedCooked)
        {
            SetError(InError, "atlas layer '" + ScopedLayer.source + "': " + ScopedParseError);
            return false;
        }
        if (ScopedCooked->layers != 1)
        {
            SetError(InError, "atlas layer must contain exactly one texture layer: " + ScopedLayer.source);
            return false;
        }
        if (!ScopedFormatKnown)
        {
            // The first layer defines the array: every layer must match its
            ScopedFormat = ScopedCooked->format;
            ScopedMipLevels = ScopedCooked->mipLevels;
            ScopedFormatKnown = true;
            if (ScopedWidth == 0)
            {
                ScopedWidth = ScopedCooked->width;
                ScopedHeight = ScopedCooked->height;
            }
            // A block-compressed format carries its colour space in the format
            if (ScopedFormat != TextureFormat::Rgba8)
            {
                ScopedSrgb = IsSrgb(ScopedFormat);
            }
        }
        if (ScopedCooked->format != ScopedFormat)
        {
            SetError(InError, "atlas layer " + ScopedLayer.source + " is " + TextureFormatName(ScopedCooked->format) +
                                  ", expected " + TextureFormatName(ScopedFormat) +
                                  " (all layers share one GPU format)");
            return false;
        }
        if (ScopedCooked->mipLevels != ScopedMipLevels)
        {
            SetError(InError, "atlas layer " + ScopedLayer.source + " has " + std::to_string(ScopedCooked->mipLevels) +
                                  " mip levels, expected " + std::to_string(ScopedMipLevels));
            return false;
        }
        if (ScopedCooked->width != ScopedWidth || ScopedCooked->height != ScopedHeight)
        {
            SetError(InError, "atlas layer " + ScopedLayer.source + " is " + std::to_string(ScopedCooked->width) + "x" +
                                  std::to_string(ScopedCooked->height) + ", expected " + std::to_string(ScopedWidth) +
                                  "x" + std::to_string(ScopedHeight));
            return false;
        }
        if (ScopedPixels.size() > std::numeric_limits<std::size_t>::max() - ScopedCooked->texels.size())
        {
            SetError(InError, "atlas is too large");
            return false;
        }
        ScopedPixels.insert(ScopedPixels.end(), ScopedCooked->texels.begin(), ScopedCooked->texels.end());
    }

    if (ScopedDesc.layers.size() > std::numeric_limits<std::uint32_t>::max() ||
        ScopedDesc.layers.size() > InGpu.Info().maxTextureLayers)
    {
        SetError(InError, "atlas has " + std::to_string(ScopedDesc.layers.size()) +
                              " layers, the device accepts at most " + std::to_string(InGpu.Info().maxTextureLayers));
        return false;
    }

    Rhi::TextureArrayDesc ScopedTextureDesc;
    ScopedTextureDesc.size = {ScopedWidth, ScopedHeight};
    ScopedTextureDesc.layers = static_cast<std::uint32_t>(ScopedDesc.layers.size());
    ScopedTextureDesc.mipLevels = ScopedMipLevels;
    ScopedTextureDesc.srgb = ScopedSrgb;
    // The cooked payload uploads verbatim: the RHI copies BC7/ASTC blocks into
    ScopedTextureDesc.format = ScopedFormat;

    const Rhi::TextureArrayHandle ScopedHandle = InGpu.CreateTextureArray(ScopedTextureDesc, ScopedPixels);
    if (!ScopedHandle.IsValid())
    {
        SetError(InError, "RHI refused the texture array upload");
        return false;
    }

    if (MHandle.IsValid())
    {
        InGpu.DestroyTextureArray(MHandle);
    }
    MHandle = ScopedHandle;
    ScopedDesc.format = ScopedFormat;
    MDesc = std::move(ScopedDesc);

    LogInfo("atlas", "loaded '{}' - {}x{} x {} layers, {} format, {} mip levels ({} bytes, {})", MDesc.name,
            ScopedWidth, ScopedHeight, MDesc.layers.size(), TextureFormatName(ScopedFormat), ScopedMipLevels,
            ScopedPixels.size(), ScopedSrgb ? "sRGB" : "linear");
    return true;
}

void TextureArrayAsset::Release(Rhi::RHI& InGpu)
{
    if (MHandle.IsValid())
    {
        InGpu.DestroyTextureArray(MHandle);
        MHandle = {};
    }
    MDesc = {};
}

std::uint32_t TextureArrayAsset::LayerIndex(std::string_view InName) const noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MDesc.layers.size(); ++ScopedI)
    {
        if (MDesc.layers[ScopedI].name == InName)
        {
            return ScopedI;
        }
    }
    return 0;
}
}
