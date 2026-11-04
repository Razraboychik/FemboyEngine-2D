// FemboyEngine2D - transport group editor panel implementation.
#include "Editor/Panels/TransportGroupPanel.hpp"

#include "Engine/Physics/TransportGroups.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace Fby::Editor
{
namespace
{
const char* const kRotationPolicies[] = {"Fixed", "Authored", "Physics"};
const char* const kMemberModes[] = {"Rigid", "Carried dynamic", "Support"};
}

const char* TransportGroupPanel::EntityLabel(const Ecs::Registry& InRegistry, Entity InEntity)
{
    static char ScopedBuffer[96];
    if (const NameComponent* ScopedName = InRegistry.try_get<NameComponent>(InEntity);
        ScopedName != nullptr && ScopedName->Value[0] != '\0')
    {
        std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "%s", ScopedName->Value);
        return ScopedBuffer;
    }
    std::snprintf(ScopedBuffer, sizeof(ScopedBuffer), "Entity %u", entt::to_integral(InEntity));
    return ScopedBuffer;
}

std::uint32_t TransportGroupPanel::NextGroupId(const Ecs::Registry& InRegistry) noexcept
{
    std::uint32_t ScopedNext = 1;
    const auto ScopedView = InRegistry.view<TransportGroupComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        const TransportGroupComponent& ScopedComponent = ScopedView.get<TransportGroupComponent>(ScopedEntity);
        ScopedNext = std::max(ScopedNext, ScopedComponent.GroupId + 1u);
    }
    return ScopedNext;
}

void TransportGroupPanel::DrawGroupEditor(Ecs::Registry& InRegistry, Entity InRoot,
                                          TransportGroupComponent& InComponent)
{
    ImGui::Text("Root: %s", EntityLabel(InRegistry, InRoot));
    ImGui::InputScalar("Group id", ImGuiDataType_U32, &InComponent.GroupId);
    ImGui::InputScalar("Parent id", ImGuiDataType_U32, &InComponent.ParentGroupId);
    int ScopedPolicy = static_cast<int>(InComponent.RotationPolicy);
    if (ImGui::Combo("Rotation policy", &ScopedPolicy, kRotationPolicies, IM_ARRAYSIZE(kRotationPolicies)))
    {
        InComponent.RotationPolicy = static_cast<std::uint32_t>(ScopedPolicy);
    }
    bool ScopedEnabled = (InComponent.Flags & kTransportGroupEnabled) != 0;
    if (ImGui::Checkbox("Enabled", &ScopedEnabled))
    {
        if (ScopedEnabled)
        {
            InComponent.Flags |= kTransportGroupEnabled;
        }
        else
        {
            InComponent.Flags &= ~kTransportGroupEnabled;
        }
    }
    bool ScopedAutoDetach = (InComponent.Flags & kTransportGroupAutoDetachOutside) != 0;
    if (ImGui::Checkbox("Detach outside bounds", &ScopedAutoDetach))
    {
        if (ScopedAutoDetach)
        {
            InComponent.Flags |= kTransportGroupAutoDetachOutside;
        }
        else
        {
            InComponent.Flags &= ~kTransportGroupAutoDetachOutside;
        }
    }
    ImGui::DragFloat2("Bounds min", &InComponent.BoundsMin.X, 1.0f, -1.0e9f, 1.0e9f);
    ImGui::DragFloat2("Bounds max", &InComponent.BoundsMax.X, 1.0f, -1.0e9f, 1.0e9f);
}

void TransportGroupPanel::DrawMemberEditor(Ecs::Registry& InRegistry, Entity InSelected)
{
    static int ScopedGroupId = 1;
    static int ScopedMode = static_cast<int>(kCarrierModeCarriedDynamic);
    ImGui::InputInt("Group id", &ScopedGroupId);
    ImGui::Combo("Mode", &ScopedMode, kMemberModes, IM_ARRAYSIZE(kMemberModes));
    if (ImGui::Button("Attach selected to group"))
    {
        TransportGroupSystem ScopedSystem;
        (void)ScopedSystem.Attach(InRegistry, InSelected, static_cast<std::uint32_t>(std::max(ScopedGroupId, 1)),
                                  static_cast<std::uint32_t>(ScopedMode), kCarrierFlagInheritRotation);
    }
}

void TransportGroupPanel::DrawMemberList(Ecs::Registry& InRegistry, std::uint32_t InGroupId)
{
    std::array<Entity, kTransportMaxMembers> ScopedToDetach{};
    std::uint32_t ScopedCount = 0;
    const auto ScopedView = InRegistry.view<CarrierMemberComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        const CarrierMemberComponent& ScopedMember = ScopedView.get<CarrierMemberComponent>(ScopedEntity);
        if (ScopedMember.GroupId != InGroupId)
        {
            continue;
        }
        ImGui::PushID(entt::to_integral(ScopedEntity));
        ImGui::Text("%s  mode=%s", EntityLabel(InRegistry, ScopedEntity),
                    ScopedMember.Mode < static_cast<std::uint32_t>(IM_ARRAYSIZE(kMemberModes))
                        ? kMemberModes[ScopedMember.Mode]
                        : "?");
        ImGui::SameLine();
        if (ImGui::SmallButton("Detach") && ScopedCount < ScopedToDetach.size())
        {
            ScopedToDetach[ScopedCount++] = ScopedEntity;
        }
        ImGui::PopID();
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
    {
        TransportGroupSystem ScopedSystem;
        (void)ScopedSystem.Detach(InRegistry, ScopedToDetach[ScopedIndex], true);
    }
}

void TransportGroupPanel::Draw(bool* InOpen, Ecs::Registry& InRegistry, Entity InSelected)
{
    if (InOpen == nullptr || !*InOpen)
    {
        return;
    }
    if (!ImGui::Begin("Transport Groups", InOpen))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Kinematic carrier graph; Parent2D is not used.");
    ImGui::Separator();

    std::uint32_t ScopedRemoveGroupId = 0;
    if (InRegistry.valid(InSelected))
    {
        TransportGroupComponent* ScopedGroup = InRegistry.try_get<TransportGroupComponent>(InSelected);
        if (ScopedGroup != nullptr)
        {
            DrawGroupEditor(InRegistry, InSelected, *ScopedGroup);
            if (ImGui::Button("Remove selected group"))
            {
                ScopedRemoveGroupId = ScopedGroup->GroupId;
            }
            ImGui::Separator();
            DrawMemberList(InRegistry, ScopedGroup->GroupId);
        }
        else if (CarrierMemberComponent* ScopedMember = InRegistry.try_get<CarrierMemberComponent>(InSelected);
                 ScopedMember != nullptr)
        {
            ImGui::Text("Member: %s", EntityLabel(InRegistry, InSelected));
            ImGui::Text("Group: %u", ScopedMember->GroupId);
            ImGui::Text("Mode: %s", ScopedMember->Mode < static_cast<std::uint32_t>(IM_ARRAYSIZE(kMemberModes))
                                        ? kMemberModes[ScopedMember->Mode]
                                        : "?");
            if (ImGui::Button("Detach selected"))
            {
                TransportGroupSystem ScopedSystem;
                (void)ScopedSystem.Detach(InRegistry, InSelected, true);
            }
        }
        else
        {
            if (ImGui::Button("Create group on selected"))
            {
                TransportGroupSystem ScopedSystem;
                (void)ScopedSystem.CreateGroup(InRegistry, InSelected, NextGroupId(InRegistry));
            }
            ImGui::Separator();
            DrawMemberEditor(InRegistry, InSelected);
        }
    }
    else
    {
        ImGui::TextDisabled("Select an entity to create or edit a group.");
    }

    if (ScopedRemoveGroupId != 0)
    {
        TransportGroupSystem ScopedSystem;
        (void)ScopedSystem.DestroyGroup(InRegistry, ScopedRemoveGroupId);
    }

    ImGui::Separator();
    ImGui::TextDisabled("Groups");
    const auto ScopedGroups = InRegistry.view<TransportGroupComponent>();
    for (const Entity ScopedEntity : ScopedGroups)
    {
        const TransportGroupComponent& ScopedGroup = ScopedGroups.get<TransportGroupComponent>(ScopedEntity);
        ImGui::Text("Group %u  root=%s", ScopedGroup.GroupId, EntityLabel(InRegistry, ScopedEntity));
    }
    ImGui::End();
}
}
