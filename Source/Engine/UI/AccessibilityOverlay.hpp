// FemboyEngine2D - caption overlay composition for a frame.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/RHI/RenderItem.hpp"
#include "Engine/UI/Accessibility.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace Fby
{

// Appends a UI overlay to a game frame without allocating on the frame path.
// Overlay items land after the game items and their batches keep the same order:
// the backend draws batches sequentially, so no key merge is required. Returns
// false (and leaves the packet untouched) when the caller's storage is too small.
[[nodiscard]] bool ComposeUiOverlay(Rhi::FramePacket& InPacket, std::span<const Rhi::RenderItem> InOverlayItems,
                                    std::span<const Rhi::RenderKey> InOverlayKeys,
                                    std::span<const Rhi::DrawBatch> InOverlayBatches,
                                    std::uint32_t InOverlayTextureArray, std::span<Rhi::RenderItem> InItems,
                                    std::span<Rhi::RenderKey> InKeys, std::span<Rhi::DrawBatch> InBatches) noexcept;

// Builds the DeafMode caption overlay (subtitle panel + labels) and composes it
// into a frame. The class owns the canvas and the fixed storage the composed
// packet points into, so a game calls Build() and Compose() per frame and the
// engine never allocates.
class AccessibilityOverlay
{
public:
    static constexpr std::uint32_t kMaxOverlayItems = 128;
    static constexpr std::uint32_t kMaxOverlayBatches = 8;

    // Rasterises the captions for this viewport. Returns false when the kernel
    // has nothing to show, so the caller can skip the composition.
    [[nodiscard]] bool Build(Ecs::SystemContext& InContext, const AccessibilityKernel& InKernel,
                             Vector2D InCanvasSize) noexcept;

    // Appends the built captions to `InPacket`. `InItems`/`InKeys`/`InBatches`
    // must hold the game frame plus kMaxOverlayItems/kMaxOverlayBatches.
    [[nodiscard]] bool Compose(Rhi::FramePacket& InPacket, std::span<Rhi::RenderItem> InItems,
                               std::span<Rhi::RenderKey> InKeys, std::span<Rhi::DrawBatch> InBatches) noexcept;

    [[nodiscard]] std::uint32_t ItemCount() const noexcept
    {
        return MItemCount;
    }
    [[nodiscard]] std::uint32_t BatchCount() const noexcept
    {
        return MBatchCount;
    }

private:
    Ui::WidgetCanvas MCanvas{};
    Ui::FrameOutput MOutput{};
    std::array<Rhi::RenderItem, kMaxOverlayItems> MItems{};
    std::array<Rhi::RenderKey, kMaxOverlayItems> MKeys{};
    std::array<Rhi::DrawBatch, kMaxOverlayBatches> MBatches{};
    std::uint32_t MItemCount = 0;
    std::uint32_t MBatchCount = 0;
    std::uint32_t MFontTextureArray = 0xFFFFFFFFu;
};
}
