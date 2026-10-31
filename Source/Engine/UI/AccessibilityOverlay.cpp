// FemboyEngine2D - caption overlay composition for a frame.
#include "Engine/UI/AccessibilityOverlay.hpp"

#include <algorithm>

namespace Fby
{

bool ComposeUiOverlay(Rhi::FramePacket& InPacket, std::span<const Rhi::RenderItem> InOverlayItems,
                      std::span<const Rhi::RenderKey> InOverlayKeys,
                      std::span<const Rhi::DrawBatch> InOverlayBatches, std::uint32_t InOverlayTextureArray,
                      std::span<Rhi::RenderItem> InItems, std::span<Rhi::RenderKey> InKeys,
                      std::span<Rhi::DrawBatch> InBatches) noexcept
{
    if (InOverlayItems.empty())
    {
        return true;
    }
    if (InOverlayItems.size() != InOverlayKeys.size() || InOverlayItems.size() != InOverlayBatches.size())
    {
        // The UI frame writes every span from one pass, so a mismatch is a bug in
        // the overlay producer rather than a capacity problem.
        return false;
    }

    const std::span<const Rhi::RenderItem> ScopedGameItems = InPacket.items;
    const std::span<const Rhi::RenderKey> ScopedGameKeys = InPacket.keys;
    const std::span<const Rhi::DrawBatch> ScopedGameBatches = InPacket.batches;
    const std::size_t ScopedItemTotal = ScopedGameItems.size() + InOverlayItems.size();
    const std::size_t ScopedBatchTotal = ScopedGameBatches.size() + InOverlayBatches.size();
    if (ScopedItemTotal > InItems.size() || ScopedItemTotal > InKeys.size() || ScopedBatchTotal > InBatches.size())
    {
        return false;
    }

    std::copy(ScopedGameItems.begin(), ScopedGameItems.end(), InItems.begin());
    std::copy(InOverlayItems.begin(), InOverlayItems.end(), InItems.begin() + ScopedGameItems.size());
    std::copy(ScopedGameKeys.begin(), ScopedGameKeys.end(), InKeys.begin());
    std::copy(InOverlayKeys.begin(), InOverlayKeys.end(), InKeys.begin() + ScopedGameKeys.size());
    std::copy(ScopedGameBatches.begin(), ScopedGameBatches.end(), InBatches.begin());

    const std::uint32_t ScopedBase = static_cast<std::uint32_t>(ScopedGameItems.size());
    for (std::size_t ScopedIndex = 0; ScopedIndex < InOverlayBatches.size(); ++ScopedIndex)
    {
        Rhi::DrawBatch ScopedBatch = InOverlayBatches[ScopedIndex];
        ScopedBatch.firstItem += ScopedBase;
        if (InOverlayTextureArray != 0xFFFFFFFFu)
        {
            ScopedBatch.textureArray = InOverlayTextureArray;
        }
        InBatches[ScopedGameBatches.size() + ScopedIndex] = ScopedBatch;
    }

    InPacket.items = InItems.first(ScopedItemTotal);
    InPacket.keys = InKeys.first(ScopedItemTotal);
    InPacket.batches = InBatches.first(ScopedBatchTotal);
    // The composed packet is a different item order, so a persistent instance
    // ring must take the full upload rather than a delta.
    InPacket.instanceRanges = {};
    InPacket.instanceRangesAreDeltas = false;
    return true;
}

bool AccessibilityOverlay::Build(Ecs::SystemContext& InContext, const AccessibilityKernel& InKernel,
                                 Vector2D InCanvasSize) noexcept
{
    MItemCount = 0;
    MBatchCount = 0;
    MFontTextureArray = 0xFFFFFFFFu;
    if (InCanvasSize.X <= 0.0f || InCanvasSize.Y <= 0.0f)
    {
        return false;
    }
    MCanvas.SetCanvasSize(InCanvasSize);
    if (!InKernel.BuildSubtitleCanvas(MCanvas))
    {
        return false;
    }
    MOutput = Ui::FrameOutput{};
    MOutput.items = std::span<Rhi::RenderItem>(MItems);
    MOutput.keys = std::span<Rhi::RenderKey>(MKeys);
    MOutput.batches = std::span<Rhi::DrawBatch>(MBatches);
    if (Ui::WidgetCanvasSystem::BuildFrame(InContext, MCanvas, MOutput) != Ui::BuildStatus::Ok)
    {
        return false;
    }
    MItemCount = MOutput.itemsWritten;
    MBatchCount = MOutput.batchesWritten;
    if (MItemCount == 0)
    {
        MBatchCount = 0;
        return false;
    }
    MFontTextureArray = MOutput.fontTextureArray;
    return true;
}

bool AccessibilityOverlay::Compose(Rhi::FramePacket& InPacket, std::span<Rhi::RenderItem> InItems,
                                   std::span<Rhi::RenderKey> InKeys, std::span<Rhi::DrawBatch> InBatches) noexcept
{
    return ComposeUiOverlay(InPacket, std::span<const Rhi::RenderItem>(MItems.data(), MItemCount),
                            std::span<const Rhi::RenderKey>(MKeys.data(), MItemCount),
                            std::span<const Rhi::DrawBatch>(MBatches.data(), MBatchCount), MFontTextureArray, InItems,
                            InKeys, InBatches);
}
}
