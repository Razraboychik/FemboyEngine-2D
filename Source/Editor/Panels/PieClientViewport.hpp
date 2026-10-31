// FemboyEngine2D - Play-In-Editor client viewport panel.
#pragma once

#include "Editor/Infrastructure/EditorSystem.hpp"

#include <cstdint>

namespace Fby::Editor
{

// Second viewport of a PIE multiplayer session. It owns the offscreen target
// that presents PlayInEditor's replicated client world: the host composes the
// client frame packet and records it into this target, this panel draws the
// result and owns the client camera.
class PieClientViewport
{
public:
    PieClientViewport() = default;

    // Creates or resizes the offscreen target. A previous target is destroyed
    // only after the GPU is idle, so a descriptor of the sampled image never
    // dangles. Returns false when the backend cannot create the target.
    [[nodiscard]] bool EnsureTarget(Rhi::RHI& InGpu, Rhi::Extent2D InSize);

    // Releases the target. Safe to call with no target.
    void Release(Rhi::RHI& InGpu) noexcept;

    // Called once per editor frame: drops the target when the PIE session is
    // gone or the viewport was hidden, so a stopped session frees its GPU image.
    void SyncSession(Rhi::RHI& InGpu, bool InPieMultiplayer) noexcept;

    [[nodiscard]] bool Active() const noexcept
    {
        return MTarget.IsValid();
    }
    [[nodiscard]] Rhi::RenderTargetHandle Target() const noexcept
    {
        return MTarget;
    }
    [[nodiscard]] Rhi::Extent2D Size() const noexcept
    {
        return MSize;
    }
    [[nodiscard]] EditorCamera& Camera() noexcept
    {
        return MCamera;
    }
    [[nodiscard]] const EditorCamera& Camera() const noexcept
    {
        return MCamera;
    }

    // Docked panel body. `InTexture` is the ImGui descriptor of the target that
    // the host registered after the last target creation; 0 means no image.
    void Draw(std::uint64_t InTexture, bool* InOpen, bool InPieMultiplayer);

private:
    Rhi::RenderTargetHandle MTarget{};
    Rhi::Extent2D MSize{};
    EditorCamera MCamera{};
};
}
