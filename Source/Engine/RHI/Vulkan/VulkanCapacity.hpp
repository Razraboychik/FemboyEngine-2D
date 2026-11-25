// FemboyEngine2D - Vulkan instance-ring capacity policy.
#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Fby::Rhi::VulkanCapacity
{
inline constexpr std::uint32_t kMaxInstanceItemsPerFrame = 1u << 20;

// Converts a requested item budget into the capacity that fits one storage
// range and the two ring copies per frame-in-flight. It never returns zero.
[[nodiscard]] constexpr std::uint32_t ClampInstanceCapacity(std::uint32_t InRequested,
                                                           std::uint64_t InMaxStorageRange,
                                                           std::uint32_t InFrames,
                                                           std::uint32_t InItemBytes) noexcept
{
    const std::uint32_t ScopedRequested = InRequested == 0 ? 1u : InRequested;
    if (InFrames == 0 || InMaxStorageRange == 0 || InItemBytes == 0)
    {
        return 1u;
    }
    const std::uint64_t ScopedRingBytesPerItem =
        static_cast<std::uint64_t>(InItemBytes) * static_cast<std::uint64_t>(InFrames) * 2u;
    const std::uint64_t ScopedByRange = InMaxStorageRange / ScopedRingBytesPerItem;
    const std::uint32_t ScopedRangeCap =
        ScopedByRange > std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
            : static_cast<std::uint32_t>(ScopedByRange);
    return std::max(1u, std::min({ScopedRequested, kMaxInstanceItemsPerFrame, ScopedRangeCap}));
}
}
