// FemboyEngine2D - virtual-texture streaming and debug panel.
#pragma once

#include "imgui.h"

#include <cstdint>

namespace Fby::Rhi
{
class VirtualTextureSystem;
}

namespace Fby::Editor
{

// Read-only view of the host's streaming system plus the debug-view selector.
// VT stays opt-in: the panel only shows live data when the host bound a system.
class VirtualTexturePanel
{
public:
    VirtualTexturePanel() = default;

    void Draw(Rhi::VirtualTextureSystem* InSystem, bool* InOpen);

    // 0 = off, 1 = tint by page id, 2 = tint by slot. The host forwards this to
    // the frame packet so the sprite shader shades the VT instead of sampling.
    [[nodiscard]] std::uint32_t DebugMode() const noexcept
    {
        return MDebugMode;
    }
    void SetDebugMode(std::uint32_t InMode) noexcept
    {
        MDebugMode = InMode > 2u ? 2u : InMode;
    }

private:
    std::uint32_t MDebugMode = 0;
};
}
