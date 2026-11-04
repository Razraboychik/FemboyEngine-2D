// FemboyEngine2D - GPU visibility contracts.
#pragma once

#include "Engine/Core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Fby::Rhi
{

// std430-compatible indirect command. Vulkan's VkDrawIndexedIndirectCommand has
// exactly these five uint32 fields in this order.
struct DrawIndexedIndirectCommand
{
    std::uint32_t IndexCount = 6;
    std::uint32_t InstanceCount = 0;
    std::uint32_t FirstIndex = 0;
    std::int32_t VertexOffset = 0;
    std::uint32_t FirstInstance = 0;
};

static_assert(sizeof(DrawIndexedIndirectCommand) == 20);
static_assert(std::is_trivially_copyable_v<DrawIndexedIndirectCommand>);

// Push constants for one opaque culling dispatch.
struct GpuCullingConstants
{
    // minX, minY, maxX, maxY in world space.
    Vector4D ViewBounds;
    // width, height, 0, lodScale - used to choose the Hi-Z mip.
    Vector4D Viewport;
    std::uint32_t ItemCount = 0;
    std::uint32_t MaxVisible = 0;
    std::uint32_t SourceOffset = 0;
    // Destination offset in the compacted visible list.
    std::uint32_t OutputOffset = 0;
    // Per-batch counter/command slots.
    std::uint32_t CounterOffset = 0;
    std::uint32_t CommandOffset = 0;
    std::uint32_t Reserved0 = 0;
    std::uint32_t Reserved1 = 0;
    // 0 = frustum only; 1 = sample the Hi-Z pyramid.
    std::uint32_t UseDepth = 0;
    std::uint32_t Reserved2 = 0;
};

static_assert(sizeof(GpuCullingConstants) == 72);
static_assert(offsetof(GpuCullingConstants, ItemCount) == 32);
static_assert(offsetof(GpuCullingConstants, OutputOffset) == 44);
static_assert(offsetof(GpuCullingConstants, CounterOffset) == 48);
static_assert(offsetof(GpuCullingConstants, CommandOffset) == 52);
static_assert(offsetof(GpuCullingConstants, UseDepth) == 64);
}
