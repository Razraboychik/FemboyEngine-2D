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

    const float ScopedDeltaMs = ImGui::GetIO().DeltaTime * 1000.0f;
    const float ScopedScale = MResolution.Update(ScopedDeltaMs);
    ImGui::SeparatorText("Dynamic resolution");
    ImGui::Text("frame %.2f ms   scale %.2f", static_cast<double>(ScopedDeltaMs), static_cast<double>(ScopedScale));
    Fby::Rhi::DynamicResolutionSettings ScopedResolution = MResolution.Settings();
    bool ScopedResolutionDirty = false;
    ScopedResolutionDirty |= ImGui::SliderFloat("Target ms", &ScopedResolution.targetMilliseconds, 4.0f, 40.0f, "%.1f");
    ScopedResolutionDirty |= ImGui::SliderFloat("Min scale", &ScopedResolution.minScale, 0.25f, 1.0f, "%.2f");
    ScopedResolutionDirty |= ImGui::SliderFloat("Max scale", &ScopedResolution.maxScale, 0.25f, 1.0f, "%.2f");
    ScopedResolutionDirty |= ImGui::SliderFloat("Step", &ScopedResolution.step, 0.01f, 0.25f, "%.2f");
    if (ScopedResolutionDirty)
    {
        MResolution.Configure(ScopedResolution);
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

    ImGui::SeparatorText("Static layer GPU bake");
    if (InContext.renderer == nullptr)
    {
        ImGui::TextDisabled("No renderer is bound.");
    }
    else if (ImGui::Button("Bake 256x256 static layer"))
    {
        MBakeItems[0] = Fby::Rhi::RenderItem::Quad(Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{256.0f, 256.0f},
                                                   Fby::Color32::FromBytes(40, 60, 90).rgba);
        MBakeKeys[0] = Fby::Rhi::MakeRenderKey(0, 0.0f);
        MBakeBatches[0] = Fby::Rhi::DrawBatch{0, 1, 0, 0};
        Fby::Rhi::FramePacket ScopedPacket;
        ScopedPacket.items = MBakeItems;
        ScopedPacket.keys = MBakeKeys;
        ScopedPacket.batches = MBakeBatches;
        ScopedPacket.camera.viewport = Fby::Vector2D{256.0f, 256.0f};
        ScopedPacket.clear = Fby::Color32::FromBytes(10, 12, 18);
        if (MBakeRenderer.Bake(*InContext.renderer, ScopedPacket, Fby::Rhi::Extent2D{256, 256}) &&
            MBakeRenderer.Finalize(*InContext.renderer))
        {
            MBakeStatus = "ready";
        }
        else
        {
            MBakeStatus = "bake failed";
        }
    }
    ImGui::Text("bake %s   texture %u", MBakeStatus.empty() ? "not run" : MBakeStatus.c_str(),
                MBakeRenderer.Texture().id);
    EndEditorPanel();
}

}
