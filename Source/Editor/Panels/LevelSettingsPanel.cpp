// FemboyEngine2D - per-scene level settings panel.
#include "Editor/Panels/LevelSettingsPanel.hpp"

#include "Editor/Infrastructure/EditorTheme.hpp"
#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/Physics/Gravity.hpp"
#include "Engine/Physics/PhysicsJoints.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdint>

namespace Fby::Editor
{
namespace
{
[[nodiscard]] Entity SelectedEntity(Ecs::Registry& InRegistry) noexcept
{
    auto ScopedView = InRegistry.view<EditorSelected>();
    for (const Entity ScopedEntity : ScopedView)
    {
        return ScopedEntity;
    }
    return Fby::kNullEntity;
}
}

void LevelSettingsPanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (!BeginEditorPanel("Level Settings", InOpen))
    {
        EndEditorPanel();
        return;
    }
    ImGui::TextDisabled("These values belong to the open scene and save with it.");
    ImGui::Separator();

    ImGui::SeparatorText("Gravity");
    Entity ScopedGravityEntity = Fby::kNullEntity;
    auto ScopedGravityView = InContext.registry.view<LevelGravityComponent>();
    for (const Entity ScopedEntity : ScopedGravityView)
    {
        ScopedGravityEntity = ScopedEntity;
        break;
    }
    if (ScopedGravityEntity == Fby::kNullEntity)
    {
        if (ImGui::Button("Create level gravity"))
        {
            ScopedGravityEntity = InContext.registry.create();
            (void)InContext.registry.emplace<LevelGravityComponent>(ScopedGravityEntity);
        }
        ImGui::TextDisabled("No LevelGravityComponent. Side falls down; top-down/iso stay weightless.");
    }
    else
    {
        auto& ScopedGravity = ScopedGravityView.get<LevelGravityComponent>(ScopedGravityEntity);
        const char* ScopedModes[] = {"Down", "Up", "Left", "Right", "Custom", "None"};
        int ScopedMode = std::clamp(static_cast<int>(ScopedGravity.Mode), 0, 5);
        if (ImGui::Combo("Mode", &ScopedMode, ScopedModes, 6))
        {
            ScopedGravity.Mode = static_cast<std::uint32_t>(ScopedMode);
        }
        ImGui::DragFloat("Strength", &ScopedGravity.Strength, 0.1f, 0.0f, 1000.0f, "%.2f m/s2");
        if (ScopedGravity.Direction() == GravityDirection::Custom)
        {
            ImGui::DragFloat2("Custom direction", &ScopedGravity.CustomDirection.X, 0.01f, -10.0f, 10.0f, "%.2f");
        }
        ImGui::DragFloat("Max fall speed", &ScopedGravity.MaxFallSpeed, 1.0f, 0.0f, 100000.0f, "%.1f px/s");
        ImGui::Checkbox("Enabled", &ScopedGravity.Enabled);
        ImGui::Checkbox("Apply world gravity", &ScopedGravity.ApplyWorldGravity);
        if (!ScopedGravity.ApplyWorldGravity)
        {
            ImGui::TextDisabled("Local gravity fields still apply.");
        }
        if (ImGui::IsItemHovered())
        {
            EditorTooltip("Area fields are independent of the world gravity vector.");
        }
    }

    ImGui::SeparatorText("Gravity fields");
    const Entity ScopedSelected = SelectedEntity(InContext.registry);
    if (ScopedSelected != Fby::kNullEntity && InContext.registry.try_get<GravityBodyComponent>(ScopedSelected) == nullptr)
    {
        if (ImGui::Button("Add field to selected"))
        {
            (void)InContext.registry.emplace<GravityBodyComponent>(ScopedSelected);
        }
        if (ImGui::IsItemHovered())
        {
            EditorTooltip("The field needs a Transform2D and acts on every dynamic body in its zone.");
        }
    }
    std::uint32_t ScopedFieldCount = 0;
    auto ScopedFields = InContext.registry.view<GravityBodyComponent>();
    for (const Entity ScopedEntity : ScopedFields)
    {
        ++ScopedFieldCount;
        auto& ScopedBody = ScopedFields.get<GravityBodyComponent>(ScopedEntity);
        ImGui::PushID(static_cast<int>(ScopedEntity));
        if (ImGui::TreeNodeEx("Field", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char* ScopedModes[] = {"Attract", "Repel", "Directional"};
            const char* ScopedShapes[] = {"Circle", "Box"};
            const char* ScopedFalloffs[] = {"None", "Linear", "Quadratic", "Exponential"};
            int ScopedMode = std::clamp(static_cast<int>(ScopedBody.Mode), 0, 2);
            if (ImGui::Combo("Mode", &ScopedMode, ScopedModes, 3))
            {
                ScopedBody.Mode = static_cast<std::uint32_t>(ScopedMode);
            }
            if (ScopedMode == 2)
            {
                ImGui::DragFloat2("Direction", &ScopedBody.Direction.X, 0.01f, -10.0f, 10.0f, "%.2f");
                if (ImGui::IsItemHovered())
                {
                    EditorTooltip("Local space. Rotates with the entity transform.");
                }
            }
            int ScopedShape = std::clamp(static_cast<int>(ScopedBody.Shape), 0, 1);
            if (ImGui::Combo("Shape", &ScopedShape, ScopedShapes, 2))
            {
                ScopedBody.Shape = static_cast<std::uint32_t>(ScopedShape);
            }
            int ScopedFalloff = std::clamp(static_cast<int>(ScopedBody.Falloff), 0, 3);
            if (ImGui::Combo("Falloff", &ScopedFalloff, ScopedFalloffs, 4))
            {
                ScopedBody.Falloff = static_cast<std::uint32_t>(ScopedFalloff);
            }
            ImGui::DragFloat("Strength", &ScopedBody.Strength, 0.1f, 0.0f, 100000.0f, "%.2f");
            if (ScopedShape == 0)
            {
                ImGui::DragFloat("Radius", &ScopedBody.Radius, 1.0f, 0.0f, 100000.0f, "%.1f");
            }
            else
            {
                ImGui::DragFloat2("Half extents", &ScopedBody.HalfExtents.X, 1.0f, 0.0f, 100000.0f, "%.1f");
            }
            ImGui::DragFloat2("Offset", &ScopedBody.Offset.X, 1.0f, -100000.0f, 100000.0f, "%.1f");
            ImGui::DragFloat("Falloff depth", &ScopedBody.FalloffDepth, 1.0f, 0.0f, 100000.0f, "%.1f");
            ImGui::DragFloat("Falloff power", &ScopedBody.FalloffPower, 0.1f, 1.0f, 16.0f, "%.2f");
            ImGui::DragFloat("Max acceleration", &ScopedBody.MaxAcceleration, 0.1f, 0.0f, 100000.0f, "%.2f");
            ImGui::InputScalar("Category bits", ImGuiDataType_U32, &ScopedBody.AffectsCategoryBits, nullptr, nullptr,
                               "%08x", ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::Checkbox("Enabled", &ScopedBody.Enabled);
            if (ImGui::Button("Remove field"))
            {
                InContext.registry.remove<GravityBodyComponent>(ScopedEntity);
                ImGui::TreePop();
                ImGui::PopID();
                break;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("%u field(s).", ScopedFieldCount);

    ImGui::SeparatorText("Ropes and chains");
    std::uint32_t ScopedChainCount = 0;
    auto ScopedChains = InContext.registry.view<JointChainComponent>();
    for (const Entity ScopedEntity : ScopedChains)
    {
        ++ScopedChainCount;
        auto& ScopedChain = ScopedChains.get<JointChainComponent>(ScopedEntity);
        ImGui::PushID(static_cast<int>(ScopedEntity));
        if (ImGui::TreeNodeEx("Rope / chain", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat("Base tension", &ScopedChain.BaseTension, 0.1f, 0.0f, 1000000.0f, "%.1f N");
            ImGui::DragFloat("Max tension", &ScopedChain.MaxTension, 0.1f, 0.0f, 1000000.0f, "%.1f N");
            ImGui::DragFloat("Tension per px", &ScopedChain.TensionPerDistance, 0.001f, 0.0f, 1000.0f, "%.3f");
            ImGui::DragFloat("Tension distance", &ScopedChain.TensionDistance, 0.1f, 0.0f, 100000.0f, "%.1f px");
            ImGui::Checkbox("Strength enabled (breaks)", &ScopedChain.StrengthEnabled);
            if (!ScopedChain.StrengthEnabled)
            {
                ImGui::TextDisabled("Unbreakable: max tension is telemetry only.");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("%u chain(s).", ScopedChainCount);
    if (ScopedChainCount == 0)
    {
        ImGui::TextDisabled("Add JointChainComponent on an entity, then tune it here.");
    }
    EndEditorPanel();
}
}
