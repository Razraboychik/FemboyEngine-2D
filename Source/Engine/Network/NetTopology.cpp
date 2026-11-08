// FemboyEngine2D - topology plan derivation.
#include "Engine/Network/NetTopology.hpp"

namespace Fby::Net
{

const char* NetTopologyName(NetTopology InTopology) noexcept
{
    switch (InTopology)
    {
        case NetTopology::Offline:
            return "Offline";
        case NetTopology::Hosted:
            return "Hosted";
        case NetTopology::Dedicated:
            return "Dedicated";
        case NetTopology::PeerToPeer:
            return "PeerToPeer";
        case NetTopology::Hybrid:
            return "Hybrid";
    }
    return "?";
}

NetTopologyPlan PlanNetTopology(const NetSessionConfig& InConfig) noexcept
{
    NetTopologyPlan ScopedPlan{};
    ScopedPlan.Role = ResolveSessionRole(InConfig);
    ScopedPlan.Headless = InConfig.Headless;

    switch (InConfig.Mode)
    {
        case NetSessionMode::Offline:
            ScopedPlan.Topology = NetTopology::Offline;
            break;
        case NetSessionMode::Dedicated:
            ScopedPlan.Topology = NetTopology::Dedicated;
            break;
        case NetSessionMode::Hosted:
            ScopedPlan.Topology = NetTopology::Hosted;
            break;
        case NetSessionMode::PeerToPeer:
            ScopedPlan.Topology = NetTopology::PeerToPeer;
            break;
        case NetSessionMode::Hybrid:
            ScopedPlan.Topology = NetTopology::Hybrid;
            break;
        case NetSessionMode::Auto:
            if (ScopedPlan.Role == NetRole::Authority)
            {
                ScopedPlan.Topology = InConfig.Headless ? NetTopology::Dedicated : NetTopology::Hosted;
            }
            else if (ScopedPlan.Role == NetRole::Peer)
            {
                ScopedPlan.Topology = NetTopology::PeerToPeer;
            }
            else if (ScopedPlan.Role == NetRole::Client)
            {
                ScopedPlan.Topology = NetTopology::Hybrid;
            }
            else
            {
                ScopedPlan.Topology = NetTopology::Offline;
            }
            break;
    }

    ScopedPlan.RunsServer = ScopedPlan.Role == NetRole::Authority;
    ScopedPlan.HasLocalClient =
        ScopedPlan.Role == NetRole::Client || ScopedPlan.Role == NetRole::Peer ||
        (ScopedPlan.Role == NetRole::Authority && !InConfig.Headless && ScopedPlan.Topology != NetTopology::Dedicated);
    return ScopedPlan;
}

}
