// FemboyEngine2D - editor panel for transport groups.
#pragma once

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <cstdint>

namespace Fby
{
namespace Editor
{

class TransportGroupPanel
{
public:
    TransportGroupPanel() = default;

    void Draw(bool* InOpen, Ecs::Registry& InRegistry, Entity InSelected);

protected:
    static void DrawGroupEditor(Ecs::Registry& InRegistry, Entity InRoot, TransportGroupComponent& InComponent);
    static void DrawMemberEditor(Ecs::Registry& InRegistry, Entity InSelected);
    static void DrawMemberList(Ecs::Registry& InRegistry, std::uint32_t InGroupId);
    static std::uint32_t NextGroupId(const Ecs::Registry& InRegistry) noexcept;
    static const char* EntityLabel(const Ecs::Registry& InRegistry, Entity InEntity);
};
}
}
