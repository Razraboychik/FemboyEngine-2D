// FemboyEngine2D - scene reference to a cooked .feskl skeleton.
#pragma once

#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Core/AssetId.hpp"
#include "Engine/Core/Types.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/ECS/SkeletonAsset.hpp"

#include <cstdint>
#include <type_traits>

namespace Fby
{

inline constexpr std::uint32_t kSkeletonAssetRefSpawnSkin = 1u << 0;
inline constexpr std::uint32_t kSkeletonAssetRefRebindOnLoad = 1u << 1;

// Migration: the scene now stores a transactional metadata handle instead of
struct SkeletonAssetRef
{
    Assets::AssetHandle Asset{};
    std::uint32_t Flags = kSkeletonAssetRefSpawnSkin | kSkeletonAssetRefRebindOnLoad;
    // Truncated FesklHeader::contentHash
    std::uint32_t ContentHash = 0;

    [[nodiscard]] bool Empty() const noexcept
    {
        return !Asset.IsValid();
    }
};

static_assert(std::is_trivially_copyable_v<SkeletonAssetRef>);
static_assert(std::is_standard_layout_v<SkeletonAssetRef>);
static_assert(sizeof(SkeletonAssetRef) == 16);
}

FBY_REFLECT_COMPONENT(Fby::SkeletonAssetRef, "SkeletonAssetRef", FBY_FIELD(Fby::SkeletonAssetRef, Asset, UInt64),
                      FBY_FIELD(Fby::SkeletonAssetRef, Flags, UInt32),
                      FBY_FIELD(Fby::SkeletonAssetRef, ContentHash, UInt32))
