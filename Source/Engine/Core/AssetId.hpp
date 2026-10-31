// FemboyEngine2D - stable 64-bit asset identity.
#pragma once

#include "Engine/Assets/PakFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace Fby
{

enum class AssetKind : std::uint16_t
{
    Unknown = 0,
    Skeleton,
    Texture,
    TextureArray,
    AudioClip,
    Mesh,
    SpriteSheet,
    Scene,
    // WebM/VP9 clip
    Video,
    // AnimationTimeline / clip asset
    Animation,
    // MaterialGraph asset
    Material,
    // FemboyScript program asset
    Script,
    // Long-form UTF-8 text asset
    Text,
    // TTF/OTF source, cooked to a .fbyfont style slot
    Font,
    // TOML list of Regular/Bold/Italic/Mono font files
    FontFamily,
};

inline constexpr std::uint32_t kAssetPathCapacity = 128;

struct AssetId
{
    std::uint64_t value = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != 0;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return IsValid();
    }
    [[nodiscard]] friend constexpr bool operator==(AssetId, AssetId) = default;

    // Deterministic: kind is mixed as a 16-bit prefix, the path is normalized to
    [[nodiscard]] static AssetId FromPath(std::string_view InPath, AssetKind InKind) noexcept
    {
        std::uint64_t ScopedHash = 1469598103934665603ull;
        const auto ScopedMix = [&ScopedHash](std::uint8_t InByte)
        {
            ScopedHash ^= InByte;
            ScopedHash *= 1099511628211ull;
        };
        ScopedMix(static_cast<std::uint8_t>(static_cast<std::uint16_t>(InKind) & 0xFFu));
        ScopedMix(static_cast<std::uint8_t>((static_cast<std::uint16_t>(InKind) >> 8) & 0xFFu));
        ScopedMix(static_cast<std::uint8_t>(':'));
        // NormalizePakPath allocates, but this runs at import/registration,
        const std::string ScopedNormalized = Assets::NormalizePakPath(InPath);
        for (const char ScopedCharacter : ScopedNormalized)
        {
            ScopedMix(static_cast<std::uint8_t>(static_cast<unsigned char>(ScopedCharacter)));
        }
        return AssetId{ScopedHash != 0 ? ScopedHash : 1ull};
    }
};

static_assert(sizeof(AssetId) == 8);
static_assert(std::is_trivially_copyable_v<AssetId>);
static_assert(std::is_standard_layout_v<AssetId>);

[[nodiscard]] constexpr AssetId InvalidAssetId() noexcept
{
    return AssetId{};
}
}
