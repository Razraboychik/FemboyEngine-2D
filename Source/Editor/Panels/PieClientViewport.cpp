// FemboyEngine2D - Play-In-Editor client viewport implementation.
#include "Editor/Panels/PieClientViewport.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Fby::Editor
{

bool PieClientViewport::EnsureTarget(Rhi::RHI& InGpu, Rhi::Extent2D InSize)
{
    const std::uint32_t ScopedWidth = std::clamp(InSize.width, 1u, 4096u);
    const std::uint32_t ScopedHeight = std::clamp(InSize.height, 1u, 4096u);
    if (MTarget.IsValid() && MSize.width == ScopedWidth && MSize.height == ScopedHeight)
    {
        return true;
    }

    Release(InGpu);

    Rhi::RenderTargetDesc ScopedDesc;
    ScopedDesc.size = {ScopedWidth, ScopedHeight};
    ScopedDesc.srgb = false;
    MTarget = InGpu.CreateRenderTarget(ScopedDesc);
    if (!MTarget.IsValid())
    {
        MSize = {};
        return false;
    }
    MSize = ScopedDesc.size;
    return true;
}

void PieClientViewport::Release(Rhi::RHI& InGpu) noexcept
{
    if (MTarget.IsValid())
    {
        // The image may still be referenced by the previous frame's draw data.
        InGpu.WaitIdle();
        InGpu.DestroyRenderTarget(MTarget);
    }
    MTarget = {};
    MSize = {};
}

void PieClientViewport::SyncSession(Rhi::RHI& InGpu, bool InPieMultiplayer) noexcept
{
    if (!InPieMultiplayer)
    {
        Release(InGpu);
    }
}

void PieClientViewport::Draw(std::uint64_t InTexture, bool* InOpen, bool InPieMultiplayer)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (!BeginEditorPanel("PIE Client", InOpen, ImGuiWindowFlags_NoScrollbar))
    {
        EndEditorPanel();
        ImGui::PopStyleVar();
        return;
    }
    ImGui::PopStyleVar();

    const ImVec2 ScopedAvailable = ImGui::GetContentRegionAvail();
    if (!InPieMultiplayer)
    {
        ImGui::SetCursorPos(ImVec2(12.0f, 12.0f));
        ImGui::TextDisabled("PIE client viewport: start Play (Multiplayer) to present the client world.");
        EndEditorPanel();
        return;
    }

    if (InTexture != 0)
    {
        // The target keeps a fixed pixel size while the panel is resizable, so
        // the image is letterboxed instead of stretched.
        const float ScopedScale =
            std::min(ScopedAvailable.x / static_cast<float>(MSize.width),
                     ScopedAvailable.y / static_cast<float>(MSize.height));
        const ImVec2 ScopedImageSize{static_cast<float>(MSize.width) * ScopedScale,
                                     static_cast<float>(MSize.height) * ScopedScale};
        const ImVec2 ScopedOffset{(ScopedAvailable.x - ScopedImageSize.x) * 0.5f,
                                  (ScopedAvailable.y - ScopedImageSize.y) * 0.5f};
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + ScopedOffset.x,
                                   ImGui::GetCursorPosY() + ScopedOffset.y));
        ImGui::Image(static_cast<ImTextureRef>(static_cast<ImTextureID>(InTexture)), ScopedImageSize);
    }
    else
    {
        ImGui::Dummy(ScopedAvailable);
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                  IM_COL32(18, 20, 28, 255));
        const ImVec2 ScopedMin = ImGui::GetItemRectMin();
        ImGui::SetCursorScreenPos(ImVec2(ScopedMin.x + 12.0f, ScopedMin.y + 12.0f));
        ImGui::TextDisabled("PIE client target is not available yet.");
    }

    // Camera input mirrors the main viewport: middle-drag pans, wheel zooms.
    if (ImGui::IsItemHovered())
    {
        const ImGuiIO& ScopedIo = ImGui::GetIO();
        const float ScopedZoom = std::max(MCamera.zoom, 0.01f);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
        {
            MCamera.center.X -= ScopedIo.MouseDelta.x / ScopedZoom;
            MCamera.center.Y -= ScopedIo.MouseDelta.y / ScopedZoom;
        }
        if (ScopedIo.MouseWheel != 0.0f)
        {
            MCamera.zoom = std::clamp(MCamera.zoom * (1.0f + ScopedIo.MouseWheel * 0.1f), 0.05f, 8.0f);
        }
    }

    ImGui::SetCursorPos(ImVec2(8.0f, 30.0f));
    ImGui::TextDisabled("PIE client | zoom %.2f | MMB pan, wheel zoom", MCamera.zoom);
    EndEditorPanel();
}
}
