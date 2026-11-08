// FemboyEngine2D - topology plan keeps hosted, dedicated and clients on one path.
#include "Engine/Network/NetTopology.hpp"

#include <cstdio>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)
}

int main()
{
    Fby::Net::NetSessionConfig ScopedConfig;
    ScopedConfig.Mode = Fby::Net::NetSessionMode::Offline;
    Fby::Net::NetTopologyPlan ScopedPlan = Fby::Net::PlanNetTopology(ScopedConfig);
    FBY_CHECK(ScopedPlan.Topology == Fby::Net::NetTopology::Offline);
    FBY_CHECK(!ScopedPlan.RunsServer && !ScopedPlan.HasLocalClient);

    ScopedConfig = Fby::Net::NetSessionConfig{};
    ScopedConfig.Mode = Fby::Net::NetSessionMode::Hosted;
    ScopedPlan = Fby::Net::PlanNetTopology(ScopedConfig);
    FBY_CHECK(ScopedPlan.Topology == Fby::Net::NetTopology::Hosted);
    FBY_CHECK(ScopedPlan.Role == Fby::Net::NetRole::Authority);
    FBY_CHECK(ScopedPlan.RunsServer && ScopedPlan.HasLocalClient && !ScopedPlan.Headless);

    ScopedConfig = Fby::Net::NetSessionConfig{};
    ScopedConfig.Mode = Fby::Net::NetSessionMode::Dedicated;
    ScopedPlan = Fby::Net::PlanNetTopology(ScopedConfig);
    FBY_CHECK(ScopedPlan.Topology == Fby::Net::NetTopology::Dedicated);
    FBY_CHECK(ScopedPlan.Role == Fby::Net::NetRole::Authority);
    FBY_CHECK(ScopedPlan.RunsServer && !ScopedPlan.HasLocalClient);

    ScopedConfig = Fby::Net::NetSessionConfig{};
    ScopedConfig.Mode = Fby::Net::NetSessionMode::Auto;
    ScopedConfig.Headless = true;
    ScopedConfig.HostRequested = true;
    ScopedPlan = Fby::Net::PlanNetTopology(ScopedConfig);
    FBY_CHECK(ScopedPlan.Topology == Fby::Net::NetTopology::Dedicated);
    FBY_CHECK(ScopedPlan.Role == Fby::Net::NetRole::Authority);
    FBY_CHECK(ScopedPlan.RunsServer && !ScopedPlan.HasLocalClient);

    ScopedConfig = Fby::Net::NetSessionConfig{};
    ScopedConfig.Mode = Fby::Net::NetSessionMode::Auto;
    ScopedConfig.Headless = false;
    ScopedConfig.HostRequested = true;
    ScopedPlan = Fby::Net::PlanNetTopology(ScopedConfig);
    FBY_CHECK(ScopedPlan.Topology == Fby::Net::NetTopology::Hosted);
    FBY_CHECK(ScopedPlan.RunsServer && ScopedPlan.HasLocalClient);

    ScopedConfig = Fby::Net::NetSessionConfig{};
    ScopedConfig.Mode = Fby::Net::NetSessionMode::Auto;
    ScopedConfig.Remote = Fby::Net::NetAddress{0x0100007Fu, 7777};
    ScopedPlan = Fby::Net::PlanNetTopology(ScopedConfig);
    FBY_CHECK(ScopedPlan.Topology == Fby::Net::NetTopology::Hybrid);
    FBY_CHECK(ScopedPlan.Role == Fby::Net::NetRole::Client);
    FBY_CHECK(!ScopedPlan.RunsServer && ScopedPlan.HasLocalClient);

    ScopedConfig = Fby::Net::NetSessionConfig{};
    ScopedConfig.Mode = Fby::Net::NetSessionMode::PeerToPeer;
    ScopedPlan = Fby::Net::PlanNetTopology(ScopedConfig);
    FBY_CHECK(ScopedPlan.Topology == Fby::Net::NetTopology::PeerToPeer);
    FBY_CHECK(ScopedPlan.Role == Fby::Net::NetRole::Peer);
    FBY_CHECK(!ScopedPlan.RunsServer && ScopedPlan.HasLocalClient);

    FBY_CHECK(Fby::Net::IsAuthorityRole(Fby::Net::NetRole::Authority));
    FBY_CHECK(Fby::Net::IsLocalClientRole(Fby::Net::NetRole::Client));
    FBY_CHECK(Fby::Net::IsLocalClientRole(Fby::Net::NetRole::Authority));
    FBY_CHECK(!Fby::Net::IsLocalClientRole(Fby::Net::NetRole::Peer));

    if (GFailures == 0)
    {
        std::printf("net_topology: OK (offline, hosted, dedicated, client, p2p)\n");
        return 0;
    }
    std::fprintf(stderr, "net_topology: %u failure(s)\n", GFailures);
    return 1;
}
