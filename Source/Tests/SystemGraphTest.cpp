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

    if (GFailures == 0)
    {
        std::printf("system_graph: OK (order, cycle, mask validation, remove)\n");
        return 0;
    }
    std::fprintf(stderr, "system_graph: %d failure(s)\n", GFailures);
    return 1;
}
