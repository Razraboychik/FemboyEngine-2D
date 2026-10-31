// FemboyEngine2D - tilemap component and render bridge.
#pragma once

#include "Engine/Core/Grid.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/RHI/RenderItem.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace Fby
{

class VirtualFileSystem;

// A 64 x 64 map is 16 KiB as a component, two layers already being a large
// but plausible tilemap. Anything bigger belongs in a streamed chunk system.
inline constexpr std::uint32_t kTilemapMaxWidth = 64;
inline constexpr std::uint32_t kTilemapMaxHeight = 64;
inline constexpr std::uint32_t kTilemapMaxTiles = kTilemapMaxWidth * kTilemapMaxHeight;

// Tile word layout. Index 0 means "no tile", so a freshly cleared map draws
// nothing without a second validity array.
inline constexpr std::uint32_t kTileIndexMask = 0xFFFFu;
inline constexpr std::uint32_t kTileFlagSolid = 1u << 16;
inline constexpr std::uint32_t kTileFlagTrigger = 1u << 17;
inline constexpr std::uint32_t kTileFlagOccluder = 1u << 18;
inline constexpr std::uint32_t kTileHeightShift = 24;

[[nodiscard]] constexpr std::uint32_t MakeTile(std::uint16_t InIndex, std::uint32_t InFlags = 0,
                                               std::uint8_t InHeight = 0) noexcept
{
    return (static_cast<std::uint32_t>(InIndex) & kTileIndexMask) | (InFlags & 0x00FF0000u) |
           (static_cast<std::uint32_t>(InHeight) << kTileHeightShift);
}

[[nodiscard]] constexpr std::uint16_t TileIndexOf(std::uint32_t InTile) noexcept
{
    return static_cast<std::uint16_t>(InTile & kTileIndexMask);
}

[[nodiscard]] constexpr std::uint32_t TileFlagsOf(std::uint32_t InTile) noexcept
{
    return InTile & 0x00FF0000u;
}

[[nodiscard]] constexpr std::uint8_t TileHeightOf(std::uint32_t InTile) noexcept
{
    return static_cast<std::uint8_t>((InTile >> kTileHeightShift) & 0xFFu);
}

[[nodiscard]] constexpr bool TileEmpty(std::uint32_t InTile) noexcept
{
    return TileIndexOf(InTile) == 0;
}

struct TilemapComponent
{
    Grid::GridDesc Grid{};
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    // Row-major, `Width * Height` words.
    std::array<std::uint32_t, kTilemapMaxTiles> Tiles{};
    // Texture-array slice and atlas grid the tile indices address.
    std::uint32_t TextureLayer = 0;
    std::uint32_t AtlasColumns = 8;
    std::uint32_t AtlasRows = 8;
    // Coarse bucket for the render key.
    std::uint16_t SortLayer = 0;
    std::uint32_t Tint = 0xFFFFFFFFu;
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<TilemapComponent>);
static_assert(std::is_standard_layout_v<TilemapComponent>);

namespace Tilemap
{

inline constexpr std::uint32_t kFlagVisibleInEditor = 1u << 0;
inline constexpr std::uint32_t kFlagNoCollision = 1u << 1;

[[nodiscard]] bool Contains(const TilemapComponent& InMap, Grid::Cell InCell) noexcept;

[[nodiscard]] std::uint32_t At(const TilemapComponent& InMap, Grid::Cell InCell) noexcept;

[[nodiscard]] bool Set(TilemapComponent& InMap, Grid::Cell InCell, std::uint32_t InTile) noexcept;

void Clear(TilemapComponent& InMap) noexcept;

void Resize(TilemapComponent& InMap, std::uint32_t InWidth, std::uint32_t InHeight) noexcept;

[[nodiscard]] bool IsSolid(const TilemapComponent& InMap, Grid::Cell InCell) noexcept;

[[nodiscard]] Vector2D CellToWorld(const TilemapComponent& InMap, Grid::Cell InCell) noexcept;

[[nodiscard]] Grid::Cell WorldToCell(const TilemapComponent& InMap, Vector2D InWorld) noexcept;

// World-space AABB of one cell quad, lifted by the tile height.
[[nodiscard]] bool CellBounds(const TilemapComponent& InMap, Grid::Cell InCell, Vector2D* OutMin,
                              Vector2D* OutMax) noexcept;

// Atlas UV rectangle of a 1-based tile index.
[[nodiscard]] bool TileUv(const TilemapComponent& InMap, std::uint16_t InIndex, Vector2D* OutMin,
                          Vector2D* OutMax) noexcept;

// Text form used by the editor's tilemap document (`.tilemap`). The header is
// line-based and the tile payload is one hex word per cell, so a diff is
// readable and a corrupted word is detected instead of silently rendered.
[[nodiscard]] bool SaveText(const TilemapComponent& InMap, std::string* OutText);
[[nodiscard]] bool LoadText(std::string_view InText, TilemapComponent* OutMap, std::string* InError = nullptr);

// Reads a `.tilemap` through the VFS so a cooked archive and a loose asset work
// the same way. The caller owns the resulting runtime component.
[[nodiscard]] bool LoadFromVfs(const VirtualFileSystem& InVfs, std::string_view InPath, TilemapComponent* OutMap,
                               std::string* InError = nullptr);
}

// Authored reference to a `.tilemap` asset. The scene serializes this small POD;
// the runtime TilemapComponent (16 KiB) is rebuilt from it on load, so a grid
// never lands in a snapshot, a PIE clone or a scene diff.
inline constexpr std::uint32_t kTilemapRefPathBytes = 96;

struct TilemapAssetRefComponent
{
    char Path[kTilemapRefPathBytes]{};
    std::uint32_t SortLayer = 0;
    std::uint32_t Flags = 0;
};

static_assert(std::is_trivially_copyable_v<TilemapAssetRefComponent>);
static_assert(std::is_standard_layout_v<TilemapAssetRefComponent>);
static_assert(sizeof(TilemapAssetRefComponent) == kTilemapRefPathBytes + 8);

// One sorted-ready tile quad: the render item and the key it must be sorted by.
struct TileQuad
{
    Rhi::RenderItem Item{};
    Rhi::RenderKey Key = 0;
};

// Turns tilemaps into render quads for the frame extract. Culled by the view
// rectangle, so a large map costs only the visible cells.
class TilemapRenderSystem
{
public:
    static constexpr std::uint32_t kMaxQuads = 4096;

    void Collect(Ecs::Registry& InRegistry, Vector2D InViewMin, Vector2D InViewMax) noexcept;

    [[nodiscard]] std::span<const TileQuad> Quads() const noexcept
    {
        return {MQuads.data(), MCount};
    }
    [[nodiscard]] std::uint32_t QuadCount() const noexcept
    {
        return MCount;
    }
    [[nodiscard]] bool Truncated() const noexcept
    {
        return MTruncated;
    }
    [[nodiscard]] std::uint32_t CellCount() const noexcept
    {
        return MCellCount;
    }

private:
    std::array<TileQuad, kMaxQuads> MQuads{};
    std::uint32_t MCount = 0;
    std::uint32_t MCellCount = 0;
    bool MTruncated = false;
};
}

FBY_REFLECT_COMPONENT(Fby::TilemapAssetRefComponent, "TilemapAssetRefComponent",
                      FBY_FIELD(Fby::TilemapAssetRefComponent, Path, String),
                      FBY_FIELD(Fby::TilemapAssetRefComponent, SortLayer, UInt32),
                      FBY_FIELD(Fby::TilemapAssetRefComponent, Flags, UInt32))
