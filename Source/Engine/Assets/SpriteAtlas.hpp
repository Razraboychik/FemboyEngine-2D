// FemboyEngine2D - deterministic sprite atlas packing and UV lookup.
#pragma once

#include "Engine/Core/Types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fby::Assets
{

inline constexpr std::uint32_t kMaxAtlasSprites = 1024;
inline constexpr std::uint32_t kDefaultAtlasPageSize = 2048;

struct AtlasSpriteSource
{
    std::uint32_t Id = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t Padding = 1;
};

struct AtlasSprite
{
    std::uint32_t Id = 0;
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    // Pivot in normalized sprite space; defaults to the centre.
    float PivotX = 0.5f;
    float PivotY = 0.5f;
    std::uint32_t Flags = 0;
};

static_assert(sizeof(AtlasSprite) == 32);

struct AtlasUv
{
    Vector2D Min{};
    Vector2D Max{};
};

struct SpriteAtlas
{
    std::uint32_t PageWidth = 0;
    std::uint32_t PageHeight = 0;
    std::uint32_t SpriteCount = 0;
    std::array<AtlasSprite, kMaxAtlasSprites> Sprites{};
};

// Result of one pack. The atlas holds sprites in the same order as the source
// span, so a caller can map a stable sprite id to its rectangle by index.
enum class AtlasPackError : std::uint8_t
{
    None = 0,
    TooManySprites,
    InvalidSprite,
    DoesNotFit,
    PageTooLarge,
};

// Deterministic shelf packing: sprites are ordered by (height desc, width desc,
// id asc), then placed left-to-right in rows. The same input always produces the
// same rectangles, which keeps cooked atlases reproducible.
[[nodiscard]] AtlasPackError PackSpriteAtlas(std::span<const AtlasSpriteSource> InSources,
                                             std::uint32_t InPageWidth, std::uint32_t InPageHeight,
                                             SpriteAtlas* OutAtlas) noexcept;

inline constexpr std::size_t kAtlasPagePathBytes = 260;

// Runtime form of a cooked .fbyatlas. The page image is addressed by the host
// through its texture array; the asset only owns the UV table and the path.
struct SpriteAtlasAsset
{
    SpriteAtlas Atlas{};
    std::array<char, kAtlasPagePathBytes> PagePath{};
    std::uint32_t TextureLayer = 0;
    bool Valid = false;
};

// Parses the text emitted by fby_atlas. Page dimensions and every rectangle are
// validated against the page, ids must be unique and non-zero.
[[nodiscard]] bool LoadSpriteAtlasAsset(std::string_view InText, SpriteAtlasAsset* OutAsset,
                                        std::string* InError = nullptr) noexcept;

[[nodiscard]] const AtlasSprite* FindAtlasSprite(const SpriteAtlas& InAtlas, std::uint32_t InId) noexcept;

// UV rectangle for a packed sprite. Returns false when the id is missing or the
// atlas page has no valid size.
[[nodiscard]] bool AtlasSpriteUv(const SpriteAtlas& InAtlas, std::uint32_t InId, AtlasUv* OutUv) noexcept;

}
