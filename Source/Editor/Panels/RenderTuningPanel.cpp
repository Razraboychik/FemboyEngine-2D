// FemboyEngine2D - live controls for the render-scale and static-tile caches.
#include "Editor/Panels/RenderTuningPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/ECS/SystemContext.hpp"

#include <imgui.h>

#include <cstdint>

namespace Fby::Editor
{

RenderTuningPanel::~RenderTuningPanel()
{
    if (MGpu != nullptr)
    {
        MBakeRenderer.Release(*MGpu);
    }
}

void RenderTuningPanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Render Tuning", InOpen))
    {
        EndEditorPanel();
        return;
    }
    MGpu = InContext.renderer;
    if (!MConfigured)
    {
        Fby::Rhi::DynamicResolutionSettings ScopedResolution;
        ScopedResolution.minScale = 0.5f;
        ScopedResolution.maxScale = 1.0f;
        ScopedResolution.targetMilliseconds = 16.6f;
        ScopedResolution.step = 0.05f;
        ScopedResolution.settleFrames = 3;
        MResolution.Configure(ScopedResolution);
        Fby::Rhi::StaticBakeSettings ScopedBake;
        ScopedBake.tileSize = 256;
        ScopedBake.budgetBytes = 64u << 20;
        ScopedBake.maxBakesPerFrame = 4;
        MBake.Configure(ScopedBake);
        MBake.SetLightVersion(MLightVersion);
        MConfigured = true;
    }

    ImGui::SeparatorText("Static bake tiles");
    ImGui::SliderFloat("Zoom", &MZoom, 0.1f, 4.0f, "%.2f");
    int ScopedLightVersion = static_cast<int>(MLightVersion);
    if (ImGui::InputInt("Light version", &ScopedLightVersion))
    {
        MLightVersion = static_cast<std::uint32_t>(ScopedLightVersion);
        MBake.SetLightVersion(MLightVersion);
    }
    MBake.BeginFrame();
    MBake.SetZoom(MZoom);
    constexpr std::int32_t kTileColumns = 8;
    constexpr std::int32_t kTileRows = 6;
    constexpr std::uint32_t kTileBytes = 256u * 256u * 4u;
    for (std::int32_t ScopedY = 0; ScopedY < kTileRows; ++ScopedY)
    {
        for (std::int32_t ScopedX = 0; ScopedX < kTileColumns; ++ScopedX)
        {
            Fby::Rhi::StaticBakeTileKey ScopedKey;
            if (!MBake.Acquire(0u, ScopedX, ScopedY, &ScopedKey) && MBake.HasBudget())
            {
                MBake.Commit(ScopedKey, kTileBytes);
            }
        }
    }
    const Fby::Rhi::StaticBakeStats& ScopedStats = MBake.Stats();
    ImGui::Text("tiles %u   ready %u   bakes/frame %u   memory %.1f MB   evictions %llu",
                ScopedStats.tiles, ScopedStats.readyTiles, ScopedStats.bakesThisFrame,
                static_cast<double>(ScopedStats.memoryBytes) / (1024.0 * 1024.0),
                static_cast<unsigned long long>(ScopedStats.evictions));
    ImGui::Text("zoom bucket %u   budget %s", static_cast<unsigned>(MBake.ZoomBucket()),
                MBake.HasBudget() ? "available" : "spent");

    EndEditorPanel();
}

}
