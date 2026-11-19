// FemboyEngine2D - one topology plan shared by hosted, dedicated and clients.
#pragma once

#include "Engine/Network/Session.hpp"

#include <cstdint>

namespace Fby::Net
{

enum class NetTopology : std::uint8_t
{
    Offline = 0,
    Hosted,
    Dedicated,
    PeerToPeer,
    Hybrid,
};

[[nodiscard]] const char* NetTopologyName(NetTopology InTopology) noexcept;

// Gameplay code reads this plan, never the raw mode. Hosted is Dedicated plus a
// local client; Dedicated has no local client; a plain client only connects.
struct NetTopologyPlan
{
    NetTopology Topology = NetTopology::Offline;
    NetRole Role = NetRole::Offline;
    bool RunsServer = false;
    bool HasLocalClient = false;
    bool Headless = false;
};

[[nodiscard]] NetTopologyPlan PlanNetTopology(const NetSessionConfig& InConfig) noexcept;

[[nodiscard]] constexpr bool IsAuthorityRole(NetRole InRole) noexcept
{
    return InRole == NetRole::Authority;
}

// The plan is the only authority for this question: a role alone cannot know
// whether the process is headless or dedicated.
[[nodiscard]] constexpr bool HasLocalClient(const NetTopologyPlan& InPlan) noexcept
{
    return InPlan.HasLocalClient;
}

}
