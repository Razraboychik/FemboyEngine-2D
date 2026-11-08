// FemboyEngine2D - SystemGraph scheduling and failure contract.
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <cstdio>

namespace
{
int GFailures = 0;
int GOrder = 0;
int GFirst = 0;
int GSecond = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void RunFirst(Fby::Ecs::SystemContext&, void*)
{
    GFirst = ++GOrder;
}

void RunSecond(Fby::Ecs::SystemContext&, void*)
{
    GSecond = ++GOrder;
}
}

int main()
{
    Fby::Ecs::Registry Registry;
    Fby::JobSystem Jobs(1);
    Fby::Ecs::SystemContext Context{Registry, Jobs};
    constexpr std::uint64_t FirstBit = 1ull << 0;

    {
        Fby::Ecs::SystemGraph Graph;
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "first", &RunFirst), "add first");
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "second", &RunSecond, nullptr, FirstBit), "add second");
        Check(Graph.Build(), "acyclic graph builds");
        GOrder = 0;
        Graph.Run(Fby::Ecs::Phase::PreUpdate, Context);
        Check(GFirst == 1 && GSecond == 2, "dependency order is respected");
    }

    {
        Fby::Ecs::SystemGraph Graph;
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "first", &RunFirst, nullptr, 0, FirstBit + (1ull << 1)),
              "add cycle first");
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "second", &RunSecond, nullptr, 0, FirstBit), "add cycle second");
        Check(!Graph.Build(), "cycle is rejected");
        Check(!Graph.IsBuilt(), "failed build leaves the graph unbuilt");
        GFirst = 0;
        GSecond = 0;
        GOrder = 0;
        Graph.Run(Fby::Ecs::Phase::PreUpdate, Context);
        Check(GOrder == 0, "an unbuilt graph runs nothing");
    }

    {
        Fby::Ecs::SystemGraph Graph;
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "first", &RunFirst), "add invalid-mask first");
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "second", &RunSecond, nullptr, 1ull << 40),
              "add invalid-mask second");
        Check(!Graph.Build(), "a mask naming a missing node is rejected");
    }

    {
        int User = 0;
        Fby::Ecs::SystemGraph Graph;
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "first", &RunFirst, &User), "add removable");
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "second", &RunSecond, nullptr, FirstBit), "add dependent");
        Check(Graph.RemoveUser(&User), "remove the first system");
        Check(Graph.Count() == 1, "one system remains");
        Check(Graph.Build(), "graph rebuilds after removal");
        GOrder = 0;
        GSecond = 0;
        Graph.Run(Fby::Ecs::Phase::PreUpdate, Context);
        Check(GSecond == 1, "surviving system runs without a ghost dependency");
    }

    {
        // A declared write/read conflict on the same service orders two systems
        // even though neither declares an explicit dependency edge.
        Fby::Ecs::SystemGraph Graph;
        Fby::Ecs::SystemAccess ScopedWrite;
        ScopedWrite.Writes = Fby::Ecs::kServicePhysics;
        Fby::Ecs::SystemAccess ScopedRead;
        ScopedRead.Reads = Fby::Ecs::kServicePhysics;
        Check(Graph.Add(Fby::Ecs::Phase::FixedUpdate, "physics_write", &RunSecond, nullptr, 0, 0, ScopedWrite),
              "add conflicting writer");
        Check(Graph.Add(Fby::Ecs::Phase::FixedUpdate, "physics_read", &RunFirst, nullptr, 0, 0, ScopedRead),
              "add conflicting reader");
        Check(Graph.Build(), "access conflicts build");
        GOrder = 0;
        GFirst = 0;
        GSecond = 0;
        Graph.Run(Fby::Ecs::Phase::FixedUpdate, Context);
        Check(GSecond == 1 && GFirst == 2, "a write/read conflict keeps registration order");
    }

    {
        Fby::Ecs::SystemGraph Graph;
        Check(!Graph.DeterministicSimulation(), "deterministic simulation is off by default");
        Graph.SetDeterministicSimulation(true);
        Check(Graph.DeterministicSimulation(), "deterministic simulation toggles");
        Check(Graph.Add(Fby::Ecs::Phase::FixedUpdate, "first", &RunFirst), "add deterministic first");
        Check(Graph.Add(Fby::Ecs::Phase::FixedUpdate, "second", &RunSecond), "add deterministic second");
        Check(Graph.Build(), "deterministic graph builds");
        GOrder = 0;
        GFirst = 0;
        GSecond = 0;
        Graph.Run(Fby::Ecs::Phase::FixedUpdate, Context);
        Check(GFirst == 1 && GSecond == 2, "deterministic graph keeps registration order");
    }

    if (GFailures == 0)
    {
        std::printf("system_graph: OK (order, cycle, mask validation, remove, access conflict)\n");
        return 0;
    }
    std::fprintf(stderr, "system_graph: %d failure(s)\n", GFailures);
    return 1;
}
