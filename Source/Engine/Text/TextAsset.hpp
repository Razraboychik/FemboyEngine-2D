// FemboyEngine2D - long-form text asset references.
#pragma once

#include "Engine/Core/AssetId.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby::Text
{

// A stable reference to one block of a cooked text asset. The generation
// invalidates references after a hot reload without changing the 16-byte size.
struct TextAssetRef
{
    AssetId Asset{};
    std::uint32_t Block = 0;
    std::uint32_t Generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Asset.IsValid() && Generation != 0;
    }

    [[nodiscard]] friend constexpr bool operator==(TextAssetRef InLeft, TextAssetRef InRight) noexcept
    {
        return InLeft.Asset == InRight.Asset && InLeft.Block == InRight.Block &&
               InLeft.Generation == InRight.Generation;
    }
};

static_assert(sizeof(TextAssetRef) == 16);
static_assert(std::is_trivially_copyable_v<TextAssetRef>);
static_assert(std::is_standard_layout_v<TextAssetRef>);

// One block in the store. The offset is into the store's text arena.
struct TextBlock
{
    std::uint32_t Offset = 0;
    std::uint32_t Length = 0;
};

static_assert(sizeof(TextBlock) == 8);
}
