// FemboyEngine2D - collision layer matrix.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace Fby
{

inline constexpr std::uint32_t kCollisionLayerCount = 64;
inline constexpr std::uint32_t kNamedCollisionLayers = 6;

enum class CollisionLayer : std::uint8_t
{
    Player = 0,
    Enemy = 1,
    Bullet = 2,
    Water = 3,
    SdfTerrain = 4,
    Default = 5,
    Count = kNamedCollisionLayers,
};

[[nodiscard]] constexpr const char* CollisionLayerName(CollisionLayer InLayer) noexcept
{
    switch (InLayer)
    {
        case CollisionLayer::Player:
            return "Player";
        case CollisionLayer::Enemy:
            return "Enemy";
        case CollisionLayer::Bullet:
            return "Bullet";
        case CollisionLayer::Water:
            return "Water";
        case CollisionLayer::SdfTerrain:
            return "SDF Terrain";
        case CollisionLayer::Default:
            return "Default";
        case CollisionLayer::Count:
            break;
    }
    return "?";
}

struct CollisionFilterMatrix
{
    // Explicit field-wise equality: masks are the entire state, so two matrices
    [[nodiscard]] friend constexpr bool operator==(const CollisionFilterMatrix&,
                                                   const CollisionFilterMatrix&) noexcept = default;

    // Row-per-layer mask. A zero bit pair disables the pair in both directions.
    std::array<std::uint64_t, kCollisionLayerCount> masks{};

    [[nodiscard]] constexpr bool Get(std::uint32_t InLayerA, std::uint32_t InLayerB) const noexcept
    {
        if (InLayerA >= kCollisionLayerCount || InLayerB >= kCollisionLayerCount)
        {
            return false;
        }
        return (masks[InLayerA] & (1ull << InLayerB)) != 0;
    }
    [[nodiscard]] constexpr bool Get(CollisionLayer InLayerA, CollisionLayer InLayerB) const noexcept
    {
        return Get(static_cast<std::uint32_t>(InLayerA), static_cast<std::uint32_t>(InLayerB));
    }
    constexpr void Set(std::uint32_t InLayerA, std::uint32_t InLayerB, bool InEnabled) noexcept
    {
        if (InLayerA >= kCollisionLayerCount || InLayerB >= kCollisionLayerCount)
        {
            return;
        }
        const std::uint64_t ScopedBitA = 1ull << InLayerA;
        const std::uint64_t ScopedBitB = 1ull << InLayerB;
        if (InEnabled)
        {
            masks[InLayerA] |= ScopedBitB;
            masks[InLayerB] |= ScopedBitA;
        }
        else
        {
            masks[InLayerA] &= ~ScopedBitB;
            masks[InLayerB] &= ~ScopedBitA;
        }
    }
    constexpr void Set(CollisionLayer InLayerA, CollisionLayer InLayerB, bool InEnabled) noexcept
    {
        Set(static_cast<std::uint32_t>(InLayerA), static_cast<std::uint32_t>(InLayerB), InEnabled);
    }

    // Box2D uses a 32-bit category/mask. The first 32 matrix layers map directly.
    [[nodiscard]] constexpr std::uint32_t CategoryBits(std::uint32_t InLayer) const noexcept
    {
        return InLayer < 32 ? (1u << InLayer) : 0u;
    }
    [[nodiscard]] constexpr std::uint32_t MaskBits(std::uint32_t InLayer) const noexcept
    {
        if (InLayer >= 32)
        {
            return 0u;
        }
        return static_cast<std::uint32_t>(masks[InLayer] & 0xFFFFFFFFull);
    }
    [[nodiscard]] static constexpr std::uint32_t LayerFromCategory(std::uint32_t InCategoryBits) noexcept
    {
        for (std::uint32_t ScopedI = 0; ScopedI < 32; ++ScopedI)
        {
            if ((InCategoryBits & (1u << ScopedI)) != 0)
            {
                return ScopedI;
            }
        }
        return static_cast<std::uint32_t>(CollisionLayer::Default);
    }

    static constexpr CollisionFilterMatrix Defaults() noexcept
    {
        CollisionFilterMatrix ScopedMatrix{};
        for (std::uint32_t ScopedA = 0; ScopedA < kNamedCollisionLayers; ++ScopedA)
        {
            for (std::uint32_t ScopedB = 0; ScopedB < kNamedCollisionLayers; ++ScopedB)
            {
                ScopedMatrix.Set(ScopedA, ScopedB, true);
            }
        }
        // Player and enemy do not collide with the SDF terrain as a rigid body:
        ScopedMatrix.Set(CollisionLayer::Player, CollisionLayer::SdfTerrain, false);
        ScopedMatrix.Set(CollisionLayer::Enemy, CollisionLayer::SdfTerrain, false);
        ScopedMatrix.Set(CollisionLayer::Bullet, CollisionLayer::Bullet, false);
        ScopedMatrix.Set(CollisionLayer::Bullet, CollisionLayer::Water, false);
        return ScopedMatrix;
    }
};

static_assert(std::is_trivially_copyable_v<CollisionFilterMatrix>);
static_assert(std::is_standard_layout_v<CollisionFilterMatrix>);
}
