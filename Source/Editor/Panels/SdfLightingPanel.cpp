// FemboyEngine2D - SDF lighting authoring panel implementation.
#include "Editor/Panels/SdfLightingPanel.hpp"

#include "Engine/ECS/Lighting.hpp"
#include "Engine/ECS/FCS/Registry.hpp"
#include "Engine/RHI/RenderItem.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace Fby::Editor
{

void SdfLightingPanel::AddSun(Fcs::Registry& InRegistry)
{
    const Entity ScopedEntity = InRegistry.create();
    if (!InRegistry.valid(ScopedEntity))
    {
        return;
    }
    InRegistry.emplace<SdfSunComponent>(ScopedEntity, SdfSunComponent{});
}

void SdfLightingPanel::AddOccluder(Fcs::Registry& InRegistry, Vector2D InSpawnPosition, bool InBox)
{
    const Entity ScopedEntity = InRegistry.create();
    if (!InRegistry.valid(ScopedEntity))
    {
        return;
    }
    Transform2D ScopedTransform;
    ScopedTransform.Position = InSpawnPosition;
    InRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
    Light2DOccluderComponent ScopedOccluder;
    ScopedOccluder.Flags = InBox ? kLight2DOccluderFlagBox : 0u;
    InRegistry.emplace<Light2DOccluderComponent>(ScopedEntity, ScopedOccluder);
    MSelectedOccluder = static_cast<std::uint32_t>(ScopedEntity);
}

void SdfLightingPanel::Draw(Fcs::Registry& InRegistry, Vector2D InSpawnPosition, bool* InOpen)
{
    if (!ImGui::Begin("SDF Lighting", InOpen))
    {
        ImGui::End();
        return;
    }
    SdfSunComponent* ScopedSun = nullptr;
    Entity ScopedSunOwner = kNullEntity;
    auto ScopedSunView = InRegistry.view<SdfSunComponent>();
    for (auto [ScopedEntity, ScopedComponent] : ScopedSunView.each())
    {
        if (ScopedSun == nullptr || ScopedEntity < ScopedSunOwner)
        {
            ScopedSunOwner = ScopedEntity;
            ScopedSun = &ScopedComponent;
        }
    }
    ImGui::SeparatorText("SDF sun");
    if (ScopedSun == nullptr)
    {
        ImGui::TextDisabled("The scene has no SdfSunComponent; the SDF mask stays disabled.");
        if (ImGui::Button("Create sun"))
        {
            AddSun(InRegistry);
        }
        ImGui::End();
        return;
    }
    bool ScopedEnabled = (ScopedSun->Flags & kSdfSunFlagDisabled) == 0;
    if (ImGui::Checkbox("Enabled", &ScopedEnabled))
    {
        ScopedSun->Flags = ScopedEnabled ? (ScopedSun->Flags & ~kSdfSunFlagDisabled)
                                         : (ScopedSun->Flags | kSdfSunFlagDisabled);
    }
    ImGui::DragFloat2("Direction", &ScopedSun->Direction.X, 0.01f);
    ImGui::SliderFloat("Softness", &ScopedSun->Softness, 0.1f, 128.0f, "%.1f px");
    ImGui::SliderFloat("Ambient", &ScopedSun->Ambient, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Occlusion radius", &ScopedSun->OcclusionRadius, 1.0f, 256.0f, "%.1f px");
    ImGui::SliderFloat("Occlusion strength", &ScopedSun->OcclusionStrength, 0.0f, 1.0f, "%.2f");
    int ScopedSteps = static_cast<int>(ScopedSun->Steps);
    if (ImGui::SliderInt("Steps", &ScopedSteps, 1, 64))
    {
        ScopedSun->Steps = static_cast<std::uint32_t>(std::clamp(ScopedSteps, 1, 64));
    }
    ImGui::TextDisabled("Steps bound the per-ray cost; lower values are cheaper and harder.");

    ImGui::SeparatorText("Dynamic occluders");
    std::uint32_t ScopedCount = 0;
    auto ScopedCountView = InRegistry.view<Light2DOccluderComponent>();
    for (auto [ScopedEntity, ScopedOccluder] : ScopedCountView.each())
    {
        (void)ScopedEntity;
        (void)ScopedOccluder;
        ++ScopedCount;
    }
    ImGui::Text("%u occluders (GPU collects %u)", ScopedCount, Rhi::kMaxSdfGpuOccluders);
    if (ScopedCount > Rhi::kMaxSdfGpuOccluders)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Budget exceeded: extra occluders are ignored.");
    }
    if (ImGui::Button("Add box occluder"))
    {
        AddOccluder(InRegistry, InSpawnPosition, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add circle occluder"))
    {
        AddOccluder(InRegistry, InSpawnPosition, false);
    }
    Entity ScopedPendingDelete = kNullEntity;
    auto ScopedOccluderView = InRegistry.view<Transform2D, Light2DOccluderComponent>();
    for (auto [ScopedEntity, ScopedTransform, ScopedOccluder] : ScopedOccluderView.each())
    {
        ImGui::PushID(static_cast<int>(ScopedEntity));
        const bool ScopedBox = (ScopedOccluder.Flags & kLight2DOccluderFlagBox) != 0;
        const bool ScopedSelected = MSelectedOccluder == static_cast<std::uint32_t>(ScopedEntity);
        if (ImGui::Selectable(ScopedBox ? "box occluder" : "circle occluder", ScopedSelected))
        {
            MSelectedOccluder = static_cast<std::uint32_t>(ScopedEntity);
        }
        if (ScopedSelected)
        {
            bool ScopedOccluderEnabled = (ScopedOccluder.Flags & kLight2DOccluderFlagDisabled) == 0;
            if (ImGui::Checkbox("Enabled", &ScopedOccluderEnabled))
            {
                ScopedOccluder.Flags = ScopedOccluderEnabled ? (ScopedOccluder.Flags & ~kLight2DOccluderFlagDisabled)
                                                             : (ScopedOccluder.Flags | kLight2DOccluderFlagDisabled);
            }
            bool ScopedIsBox = ScopedBox;
            if (ImGui::Checkbox("Box", &ScopedIsBox))
            {
                ScopedOccluder.Flags = ScopedIsBox ? (ScopedOccluder.Flags | kLight2DOccluderFlagBox)
                                                   : (ScopedOccluder.Flags & ~kLight2DOccluderFlagBox);
            }
            ImGui::DragFloat2("Position", &ScopedTransform.Position.X, 0.5f);
            if (ScopedIsBox)
            {
                ImGui::DragFloat2("Half extents", &ScopedOccluder.HalfExtents.X, 0.5f, 0.5f, 512.0f);
            }
            else
            {
                ImGui::DragFloat("Radius", &ScopedOccluder.Radius, 0.5f, 0.5f, 512.0f);
            }
            if (ImGui::Button("Delete"))
            {
                ScopedPendingDelete = ScopedEntity;
                MSelectedOccluder = 0xFFFFFFFFu;
                ImGui::PopID();
                break;
            }
        }
        ImGui::PopID();
    }
    if (ScopedPendingDelete != kNullEntity)
    {
        InRegistry.destroy(ScopedPendingDelete);
    }
    ImGui::End();
}
}
