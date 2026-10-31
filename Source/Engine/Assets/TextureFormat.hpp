// FemboyEngine2D - cooked texture payload (.ftex).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Assets
{

inline constexpr char kTextureMagic[8] = {'F', 'B', 'T', 'E', 'X', '0', '0', '1'};
// Version 1 is the legacy single-mip RGBA8 payload. Version 2 adds a format
inline constexpr std::uint32_t kTextureVersion = 2;
inline constexpr std::uint32_t kTextureVersionLegacy = 1;
inline constexpr std::string_view kTextureExtension = ".ftex";

// The format set is deliberately closed: only these six ship. A game that wants
enum class TextureFormat : std::uint32_t
{
    Rgba8 = 0,
    // Desktop target. BC7 is the only high-quality BC format with a real alpha
    Bc7Unorm = 1,
    Bc7Srgb = 2,
    // Mobile target. ASTC LDR 4x4 (quality) and 6x6 (size).
    Astc4x4Unorm = 3,
    Astc4x4Srgb = 4,
    Astc6x6Unorm = 5,
    Astc6x6Srgb = 6,
};

inline constexpr std::uint32_t kTextureFlagSrgb = 1u << 0;
// The cooked payload came from a source that had an alpha channel. The cooker
inline constexpr std::uint32_t kTextureFlagAlpha = 1u << 1;

[[nodiscard]] constexpr bool IsBlockCompressed(TextureFormat InFormat) noexcept
{
    return InFormat != TextureFormat::Rgba8;
}

[[nodiscard]] constexpr bool IsSrgb(TextureFormat InFormat) noexcept
{
    switch (InFormat)
    {
        case TextureFormat::Bc7Srgb:
        case TextureFormat::Astc4x4Srgb:
        case TextureFormat::Astc6x6Srgb:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr const char* TextureFormatName(TextureFormat InFormat) noexcept
{
    switch (InFormat)
    {
        case TextureFormat::Rgba8:
            return "rgba8";
        case TextureFormat::Bc7Unorm:
            return "bc7";
        case TextureFormat::Bc7Srgb:
            return "bc7-srgb";
        case TextureFormat::Astc4x4Unorm:
            return "astc-4x4";
        case TextureFormat::Astc4x4Srgb:
            return "astc-4x4-srgb";
        case TextureFormat::Astc6x6Unorm:
            return "astc-6x6";
        case TextureFormat::Astc6x6Srgb:
            return "astc-6x6-srgb";
    }
    return "?";
}

struct BlockExtent
{
    std::uint32_t width = 1;
    std::uint32_t height = 1;
};

// Texel block footprint of the format. Rgba8 is "1x1 block, 4 bytes" so the
[[nodiscard]] constexpr BlockExtent FormatBlockExtent(TextureFormat InFormat) noexcept
{
    switch (InFormat)
    {
        case TextureFormat::Rgba8:
            return {1, 1};
        case TextureFormat::Astc6x6Unorm:
        case TextureFormat::Astc6x6Srgb:
            return {6, 6};
        default:
            return {4, 4};
    }
}

[[nodiscard]] constexpr std::uint32_t FormatBlockBytes(TextureFormat InFormat) noexcept
{
    return InFormat == TextureFormat::Rgba8 ? 4u : 16u;
}

// Block-compressed textures do not mip down to 1x1: the chain stops at the
[[nodiscard]] constexpr std::uint32_t MaxMipLevels(std::uint32_t InWidth, std::uint32_t InHeight,
                                                   TextureFormat InFormat) noexcept
{
    std::uint32_t ScopedLevels = 1;
    std::uint32_t ScopedW = InWidth;
    std::uint32_t ScopedH = InHeight;
    const BlockExtent ScopedBlock = FormatBlockExtent(InFormat);
    while (ScopedW > ScopedBlock.width || ScopedH > ScopedBlock.height)
    {
        ScopedW = ScopedW > 1 ? ScopedW / 2 : 1;
        ScopedH = ScopedH > 1 ? ScopedH / 2 : 1;
        ++ScopedLevels;
    }
    return ScopedLevels;
}

[[nodiscard]] constexpr std::uint32_t MipExtent(std::uint32_t InBase, std::uint32_t InLevel) noexcept
{
    const std::uint32_t ScopedShifted = InBase >> InLevel;
    return ScopedShifted == 0 ? 1u : ScopedShifted;
}

// Bytes of one mip level of one layer.
[[nodiscard]] constexpr std::uint64_t MipByteSize(TextureFormat InFormat, std::uint32_t InWidth,
                                                  std::uint32_t InHeight) noexcept
{
    const BlockExtent ScopedBlock = FormatBlockExtent(InFormat);
    const std::uint64_t ScopedBlocksX =
        (static_cast<std::uint64_t>(InWidth) + ScopedBlock.width - 1) / ScopedBlock.width;
    const std::uint64_t ScopedBlocksY =
        (static_cast<std::uint64_t>(InHeight) + ScopedBlock.height - 1) / ScopedBlock.height;
    return ScopedBlocksX * ScopedBlocksY * FormatBlockBytes(InFormat);
}

// Bytes of the full mip chain of one layer.
[[nodiscard]] constexpr std::uint64_t LayerByteSize(TextureFormat InFormat, std::uint32_t InWidth,
                                                    std::uint32_t InHeight, std::uint32_t InMipLevels) noexcept
{
    std::uint64_t ScopedTotal = 0;
    for (std::uint32_t ScopedLevel = 0; ScopedLevel < InMipLevels; ++ScopedLevel)
    {
        ScopedTotal += MipByteSize(InFormat, MipExtent(InWidth, ScopedLevel), MipExtent(InHeight, ScopedLevel));
    }
    return ScopedTotal;
}

// Bytes of a payload that has `layers` independent mip chains.
[[nodiscard]] constexpr std::uint64_t TextureByteSize(TextureFormat InFormat, std::uint32_t InWidth,
                                                      std::uint32_t InHeight, std::uint32_t InLayers,
                                                      std::uint32_t InMipLevels) noexcept
{
    return LayerByteSize(InFormat, InWidth, InHeight, InMipLevels) * InLayers;
}

struct TextureHeader
{
    char magic[8];
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t layers;
    // TextureFormat
    std::uint32_t format;
    // KTextureFlag*
    std::uint32_t flags;
    std::uint32_t mipLevels;
    // Payload after this header; guards truncation
    std::uint32_t dataBytes;
};

static_assert(sizeof(TextureHeader) == 40, ".ftex header is fixed at 40 bytes");

// A validated, zero-copy view of a cooked texture. `texels` points at the layer
struct CookedTextureView
{
    TextureFormat format = TextureFormat::Rgba8;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t layers = 0;
    std::uint32_t mipLevels = 1;
    std::uint32_t flags = 0;
    std::span<const std::byte> texels;
    // Header + payload, for archive re-emit
    std::span<const std::byte> whole;
};

// Validates a .ftex blob: magic, version, format, mip count against dimensions
[[nodiscard]] std::optional<CookedTextureView> ParseCookedTexture(std::span<const std::byte> InBytes,
                                                                  std::string* InError = nullptr);

// Appends header + texels to `out` for the cooker. Returns false when the
[[nodiscard]] bool AppendCookedTexture(std::vector<std::byte>& Out, TextureFormat InFormat, std::uint32_t InWidth,
                                       std::uint32_t InHeight, std::uint32_t InLayers, std::uint32_t InMipLevels,
                                       std::uint32_t InFlags, std::span<const std::byte> InTexels,
                                       std::string* InError = nullptr);

// Convenience for the RHI layer: format the GPU must use, derived from the
[[nodiscard]] constexpr std::uint32_t BlockAlignedExtent(std::uint32_t InExtent, TextureFormat InFormat) noexcept
{
    const BlockExtent ScopedBlock = FormatBlockExtent(InFormat);
    return ((InExtent + ScopedBlock.width - 1) / ScopedBlock.width) * ScopedBlock.width;
}
}
