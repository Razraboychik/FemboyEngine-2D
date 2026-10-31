// FemboyEngine2D - texture cooking (PNG/TGA -> BC7 desktop / ASTC mobile).
#pragma once

#include "Engine/Assets/TextureFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Cook
{

enum class CookTarget : std::uint8_t
{
    // BC7 (sRGB or UNORM)
    Desktop = 0,
    // ASTC 4x4 / 6x6
    Mobile,
};

enum class AstcBlock : std::uint8_t
{
    Block4x4 = 0,
    Block6x6,
};

struct TextureCookOptions
{
    CookTarget target = CookTarget::Desktop;
    AstcBlock astcBlock = AstcBlock::Block4x4;
    // Colour textures are sRGB by convention; a mask/data texture can be asked
    bool srgb = true;
    // 0 = full chain for the format's block size (Assets::MaxMipLevels).
    std::uint32_t mipLevels = 0;
};

struct CookedTexture
{
    // Complete .ftex v2 image
    std::vector<std::byte> payload;
    Assets::TextureFormat format = Assets::TextureFormat::Bc7Srgb;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipLevels = 1;
    bool hasAlpha = false;
};

// Reads a file and cooks it. `source` must end in .png or .tga.
[[nodiscard]] bool CookTextureFile(const std::filesystem::path& InSource, const TextureCookOptions& InOptions,
                                   CookedTexture* Out, std::string* InError = nullptr);

// Same over an in-memory source (tests, embedded assets). `sourceName` carries
[[nodiscard]] bool CookTextureBytes(std::span<const std::byte> InSource, std::string_view InSourceName,
                                    const TextureCookOptions& InOptions, CookedTexture* Out,
                                    std::string* InError = nullptr);

// Codec building blocks (also used directly by tests)
void EncodeBc7Mode6Block(const std::uint8_t* InRgba8Block, std::span<std::byte, 16> Out);
// Decodes one BC7 block. Returns false when the block is not mode 6 (the
[[nodiscard]] bool DecodeBc7Mode6Block(std::span<const std::byte, 16> InBlock, std::uint8_t* InRgba8Block);

// Emits one ASTC LDR void-extent constant-colour block. `rgba8` holds
void EncodeAstcConstantBlock(std::span<const std::uint8_t> InRgba8, std::span<std::byte, 16> Out);
// Reads back an ASTC constant-colour block (test oracle for structure).
[[nodiscard]] bool DecodeAstcConstantBlock(std::span<const std::byte, 16> InBlock, std::uint8_t* InRgba8);

// Builds a full mip chain into `outRgba8` (concatenated, level-major), starting
[[nodiscard]] bool BuildMipChain(const std::uint8_t* InRgba8, std::uint32_t InWidth, std::uint32_t InHeight,
                                 std::uint32_t InMipLevels, bool InSrgb, std::vector<std::uint8_t>& OutRgba8);

// Highest-quality ASTC encoder seam. Returns false to fall back to the
using AstcEncoder = bool (*)(const std::uint8_t* InRgba8, std::uint32_t InWidth, std::uint32_t InHeight,
                             const TextureCookOptions& InOptions, std::vector<std::byte>& OutBlocks);
void SetAstcEncoder(AstcEncoder InEncoder) noexcept;
}
