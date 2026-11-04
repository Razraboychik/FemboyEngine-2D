// FemboyEngine2D - crowd render extract: pool agents to render items.
#pragma once

#include "Engine/Navigation/CrowdPool.hpp"
#include "Engine/RHI/RenderItem.hpp"

#include <cmath>
#include <cstdint>
#include <span>

namespace Fby::Navigation
{

struct CrowdRenderSettings
{
    Vector2D Size{6.0f, 6.0f};
    std::uint32_t Layer = 0;
    // Colors cycle by group, so a crowd reads as squads without a per-agent color.
    std::uint32_t GroupColors[8] = {0xFF3C6EFFu, 0xFFFF6E3Cu, 0xFF6EFF3Cu, 0xFF3CFFFFu,
                                    0xFFFF3CFFu, 0xFFFFC83Cu, 0xFFC83CFFu, 0xFF3CFF8Fu};
    // Rotate the quad to face the velocity; agents at rest stay unrotated.
    bool FaceVelocity = true;
};

// Writes one item per agent and returns how many were written. The span caps the
// output, so a caller can stream a crowd into a fixed staging buffer.
[[nodiscard]] inline std::uint32_t BuildCrowdRenderItems(const CrowdPool& InPool,
                                                         const CrowdRenderSettings& InSettings,
                                                         std::span<Rhi::RenderItem> OutItems) noexcept
{
    const std::uint32_t ScopedCount = std::min(InPool.Count(), static_cast<std::uint32_t>(OutItems.size()));
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        const CrowdAgent* ScopedAgent = InPool.At(ScopedIndex);
        if (ScopedAgent == nullptr)
        {
            continue;
        }
        Rhi::RenderItem& ScopedItem = OutItems[ScopedIndex];
        ScopedItem = Rhi::RenderItem::Quad(ScopedAgent->Position, InSettings.Size,
                                           InSettings.GroupColors[ScopedAgent->Group % 8u]);
        ScopedItem.layer = InSettings.Layer;
        if (InSettings.FaceVelocity && ScopedAgent->Velocity.LengthSquared() > 1.0e-6f)
        {
            ScopedItem.rotation = std::atan2(ScopedAgent->Velocity.Y, ScopedAgent->Velocity.X);
        }
        else
        {
            ScopedItem.rotation = 0.0f;
        }
    }
    return ScopedCount;
}

}
