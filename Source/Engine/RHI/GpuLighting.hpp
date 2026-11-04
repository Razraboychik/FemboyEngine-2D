// FemboyEngine2D - GPU light and cluster records.
#pragma once

#include "Engine/Core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Fby::Rhi
{

inline constexpr std::uint32_t ClusterTileSize = 16u;
inline constexpr std::uint32_t ClusterDepthSlices = 8u;
inline constexpr std::uint32_t MaxLightsPerCluster = 64u;

// One light record consumed by the cluster compute passes.
struct alignas(32) GpuLight
{
    // x, y position; z radius; w type.
    Vector4D PositionAndRadius;
    // rgb colour; a intensity.
    Vector4D ColorAndIntensity;
    // x, y direction; z cos outer cone; w cos inner cone.
    Vector4D DirectionAndCone;
    std::uint32_t Type = 0;
    std::uint32_t Flags = 0;
    std::uint32_t ShadowIndex = 0;
    std::uint32_t Reserved = 0;
};

static_assert(sizeof(GpuLight) == 64);
static_assert(offsetof(GpuLight, Type) == 48);

// One cluster header. Offset is in light-index units, not bytes.
struct alignas(16) ClusterHeader
{
    std::uint32_t Offset = 0;
    std::uint32_t Count = 0;
    std::uint32_t OverflowCount = 0;
    std::uint32_t Flags = 0;
};

static_assert(sizeof(ClusterHeader) == 16);

struct alignas(16) ClusterGridConstants
{
    std::uint32_t TilesX = 0;
    std::uint32_t TilesY = 0;
    std::uint32_t LightCount = 0;
    std::uint32_t MaxPerCluster = MaxLightsPerCluster;
};

static_assert(sizeof(ClusterGridConstants) == 16);

// SDF GI probe grid. One header per volume, probes follow contiguously.
struct alignas(16) GpuGiVolumeHeader
{
    // origin.xy, cellSize.xy
    Vector4D OriginAndCell;
    // width, height, maxDistance, strength
    Vector4D Extent;
};

static_assert(sizeof(GpuGiVolumeHeader) == 32);

struct alignas(16) GpuGiProbe
{
    // centre.xy, radius, padding
    Vector4D PositionAndRadius;
    // four lobe radiance values
    Vector4D Radiance;
    // occlusion, openness, reserved, reserved
    Vector4D Occlusion;
};

static_assert(sizeof(GpuGiProbe) == 48);
}
