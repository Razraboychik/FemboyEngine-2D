// FemboyEngine2D - virtual-texture panel: streaming stats and the debug view.
#include "Editor/Panels/VirtualTexturePanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Engine/RHI/VirtualTextureFormat.hpp"
#include "Engine/RHI/VirtualTextureSystem.hpp"

#include <algorithm>
#include <cstdio>

namespace Fby::Editor
{

void VirtualTexturePanel::Draw(Rhi::VirtualTextureSystem* InSystem, bool* InOpen)
{
    if (!BeginEditorPanel("Virtual Texture", InOpen))
    {
        EndEditorPanel();
        return;
    }

    const char* ScopedDebugModes[] = {"Off", "Tint by page id", "Tint by slot"};
    int ScopedDebug = static_cast<int>(MDebugMode);
    if (ImGui::Combo("Debug view", &ScopedDebug, ScopedDebugModes, 3))
    {
        SetDebugMode(static_cast<std::uint32_t>(ScopedDebug));
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Page id shows which page is bound where; slot shows pool pressure;"
                          " magenta means no resident page.");
    }
    ImGui::TextDisabled("Opt-in: the streaming demo runs with FBY_VT_SELFTEST=1");

    if (InSystem == nullptr)
    {
        ImGui::Separator();
        ImGui::TextWrapped("Virtual texturing is inactive. Bind a streaming system from the host to see "
                           "residency, uploads and evictions here.");
        EndEditorPanel();
        return;
    }

    const Rhi::VirtualTextureStats& ScopedStats = InSystem->Stats();
    ImGui::SeparatorText("Streaming");
    ImGui::Text("frame %u", ScopedStats.frame);
    ImGui::Text("visible %u   resident %u", ScopedStats.visibleTiles, ScopedStats.residentTiles);
    ImGui::Text("uploads %u   evictions %u", ScopedStats.uploads, ScopedStats.evictions);
    ImGui::Text("VFS misses %u   deferred %u", ScopedStats.vfsMisses, ScopedStats.spills);
    ImGui::Text("uploaded %.1f KiB", static_cast<double>(ScopedStats.uploadedBytes) / 1024.0);
    ImGui::Text("VRAM budget %.1f MiB", static_cast<double>(ScopedStats.vramBudgetBytes) / (1024.0 * 1024.0));

    ImGui::SeparatorText("Budget");
    int ScopedMaxLoads = static_cast<int>(InSystem->MaxLoadsPerUpdate());
    if (ImGui::SliderInt("Loads / frame", &ScopedMaxLoads, 1, 32))
    {
        InSystem->SetMaxLoadsPerUpdate(static_cast<std::uint32_t>(ScopedMaxLoads));
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Uploads per frame: 4-8 is steady; lower it when upload bursts"
                          " cause frame-time spikes.");
    }
    ImGui::TextDisabled("deferred = requests that missed the per-frame budget");

    const std::span<const Rhi::VirtualTextureTableUpdate> ScopedPending = InSystem->PendingTableUpdates();
    ImGui::SeparatorText("Page table");
    ImGui::Text("pending updates %u", static_cast<unsigned>(ScopedPending.size()));
    const std::uint32_t ScopedShow = std::min<std::uint32_t>(static_cast<std::uint32_t>(ScopedPending.size()), 8u);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedShow; ++ScopedI)
    {
        const Rhi::VirtualTextureTableUpdate& ScopedUpdate = ScopedPending[ScopedI];
        if (ScopedUpdate.Entry == Rhi::kVirtualTexturePageTableMissing)
        {
            ImGui::Text("tile (%d, %d): evicted", ScopedUpdate.TileX, ScopedUpdate.TileY);
        }
        else
        {
            std::uint32_t ScopedLayer = 0;
            std::uint32_t ScopedSlot = 0;
            (void)Rhi::UnpackVirtualTexturePageTable(ScopedUpdate.Entry, &ScopedLayer, &ScopedSlot);
            ImGui::Text("tile (%d, %d): layer %u slot %u", ScopedUpdate.TileX, ScopedUpdate.TileY, ScopedLayer,
                        ScopedSlot);
        }
    }
    EndEditorPanel();
}
}
