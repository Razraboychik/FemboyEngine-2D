// FemboyEngine2D - .ftex validation and header writing.
#include "Engine/Assets/TextureFormat.hpp"

#include <cstring>
#include <limits>

namespace Fby::Assets
{
namespace
{
void SetError(std::string* InError, const char* InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}

[[nodiscard]] bool ValidFormat(std::uint32_t InValue) noexcept
{
    return InValue <= static_cast<std::uint32_t>(TextureFormat::Astc6x6Srgb);
}

[[nodiscard]] bool ValidDimensions(std::uint32_t InValue) noexcept
{
    return InValue > 0 && InValue <= 16384;
}
}

std::optional<CookedTextureView> ParseCookedTexture(std::span<const std::byte> InBytes, std::string* InError)
{
    if (InBytes.size() < sizeof(TextureHeader))
    {
        SetError(InError, "texture payload is truncated (no complete header)");
        return std::nullopt;
    }

    TextureHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kTextureMagic, sizeof(kTextureMagic)) != 0)
    {
        SetError(InError, "texture payload has a bad magic (not a cooked .ftex)");
        return std::nullopt;
    }
    if (ScopedHeader.version != kTextureVersion && ScopedHeader.version != kTextureVersionLegacy)
    {
        SetError(InError, "texture payload uses an unsupported .ftex version");
        return std::nullopt;
    }
    if (!ValidFormat(ScopedHeader.format))
    {
        SetError(InError, "texture payload uses an unknown TextureFormat");
        return std::nullopt;
    }
    if (!ValidDimensions(ScopedHeader.width) || !ValidDimensions(ScopedHeader.height))
    {
        SetError(InError, "texture payload dimensions are out of range");
        return std::nullopt;
    }
    if (ScopedHeader.layers == 0 || ScopedHeader.layers > 4096)
    {
        SetError(InError, "texture payload layer count is out of range");
        return std::nullopt;
    }

    const auto ScopedFormat = static_cast<TextureFormat>(ScopedHeader.format);
    const std::uint32_t ScopedMaximumMips = MaxMipLevels(ScopedHeader.width, ScopedHeader.height, ScopedFormat);
    const std::uint32_t ScopedMipLevels = ScopedHeader.mipLevels == 0 ? 1u : ScopedHeader.mipLevels;
    if (ScopedMipLevels > ScopedMaximumMips)
    {
        SetError(InError, "texture payload has more mip levels than its dimensions allow");
        return std::nullopt;
    }
    if (ScopedHeader.version == kTextureVersionLegacy &&
        (ScopedFormat != TextureFormat::Rgba8 || ScopedMipLevels != 1 || ScopedHeader.layers != 1))
    {
        SetError(InError, "legacy .ftex payload must be single-layer single-mip RGBA8");
        return std::nullopt;
    }

    const std::uint64_t ScopedExpected =
        TextureByteSize(ScopedFormat, ScopedHeader.width, ScopedHeader.height, ScopedHeader.layers, ScopedMipLevels);
    if (ScopedExpected > std::numeric_limits<std::size_t>::max() ||
        InBytes.size() - sizeof(TextureHeader) != static_cast<std::size_t>(ScopedExpected) ||
        ScopedHeader.dataBytes != ScopedExpected)
    {
        SetError(InError, "texture payload size does not match its header");
        return std::nullopt;
    }

    CookedTextureView ScopedView;
    ScopedView.format = ScopedFormat;
    ScopedView.width = ScopedHeader.width;
    ScopedView.height = ScopedHeader.height;
    ScopedView.layers = ScopedHeader.layers;
    ScopedView.mipLevels = ScopedMipLevels;
    ScopedView.flags = ScopedHeader.flags;
    ScopedView.texels = InBytes.subspan(sizeof(TextureHeader));
    ScopedView.whole = InBytes;
    return ScopedView;
}

bool AppendCookedTexture(std::vector<std::byte>& Out, TextureFormat InFormat, std::uint32_t InWidth,
                         std::uint32_t InHeight, std::uint32_t InLayers, std::uint32_t InMipLevels,
                         std::uint32_t InFlags, std::span<const std::byte> InTexels, std::string* InError)
{
    if (!ValidDimensions(InWidth) || !ValidDimensions(InHeight) || InLayers == 0 || InLayers > 4096)
    {
        SetError(InError, "cooked texture descriptor is out of range");
        return false;
    }
    const std::uint32_t ScopedClampedMips = InMipLevels == 0 ? 1u : InMipLevels;
    if (ScopedClampedMips > MaxMipLevels(InWidth, InHeight, InFormat))
    {
        SetError(InError, "cooked texture has more mip levels than its dimensions allow");
        return false;
    }
    const std::uint64_t ScopedExpected = TextureByteSize(InFormat, InWidth, InHeight, InLayers, ScopedClampedMips);
    if (InTexels.size() != ScopedExpected)
    {
        SetError(InError, "cooked texture payload does not match the descriptor");
        return false;
    }
    if (ScopedExpected > std::numeric_limits<std::uint32_t>::max())
    {
        SetError(InError, "cooked texture payload exceeds the 4 GiB .ftex limit");
        return false;
    }

    TextureHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kTextureMagic, sizeof(kTextureMagic));
    ScopedHeader.version = kTextureVersion;
    ScopedHeader.width = InWidth;
    ScopedHeader.height = InHeight;
    ScopedHeader.layers = InLayers;
    ScopedHeader.format = static_cast<std::uint32_t>(InFormat);
    ScopedHeader.flags = InFlags;
    ScopedHeader.mipLevels = ScopedClampedMips;
    ScopedHeader.dataBytes = static_cast<std::uint32_t>(ScopedExpected);

    Out.resize(sizeof(TextureHeader) + static_cast<std::size_t>(ScopedExpected));
    std::memcpy(Out.data(), &ScopedHeader, sizeof(ScopedHeader));
    if (!InTexels.empty())
    {
        std::memcpy(Out.data() + sizeof(TextureHeader), InTexels.data(), InTexels.size());
    }
    return true;
}
}
