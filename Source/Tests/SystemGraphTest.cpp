// FemboyEngine2D - SystemGraph scheduling and failure contract.
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{
int GFailures = 0;
int GOrder = 0;
int GFirst = 0;
int GSecond = 0;
int GConst = 0;
int GUi = 0;
std::atomic<int> GSlow{0};

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

void RunThrowing(Fby::Ecs::SystemContext&, void*)
{
    throw std::runtime_error("system exception");
}

void RunSlow(Fby::Ecs::SystemContext&, void*)
{
    GSlow.fetch_add(1, std::memory_order_relaxed);
}

struct ValidationProbe
{
    int Value = 0;
};

void RunValidated(Fby::Ecs::SystemContext& InContext, void*)
{
    const Fby::Ecs::Registry& ScopedRegistry = InContext.registry;
    for (const auto& [ScopedEntity, ScopedProbe] : ScopedRegistry.view<ValidationProbe>().each())
    {
        (void)ScopedEntity;
        (void)ScopedProbe;
    }
}

void RunConst(const Fby::Ecs::ConstCoreSystemView& InView, void*)
{
    static_assert(std::is_const_v<std::remove_reference_t<decltype(InView.registry)>>,
                  "ConstCoreSystemView exposes a const registry");
    ++GConst;
}

void RunUi(Fby::Ecs::SystemContext&, void*)
{
    ++GUi;
}

static_assert(
    std::is_const_v<std::remove_reference_t<decltype(std::declval<Fby::Ecs::ConstRenderSystemView>().registry)>>,
    "ConstRenderSystemView exposes a const registry");
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
        Graph.SetPaused(true);
        Check(Graph.Paused(), "graph pause flag is set");
        GFirst = 0;
        GSecond = 0;
        Graph.Run(Fby::Ecs::Phase::PreUpdate, Context);
        Check(GFirst == 0 && GSecond == 0, "paused graph skips simulation phases");
        Graph.SetPaused(false);
        Check(!Graph.Paused(), "graph pause flag clears");
    }

    {
        // A paused graph can keep the Ui phase alive for an animated pause HUD.
        Fby::Ecs::SystemGraph Graph;
        Check(Graph.Add(Fby::Ecs::Phase::Ui, "hud", &RunUi), "ui phase system registers");
        Check(Graph.Build(), "ui graph builds");
        Graph.SetPaused(true, true);
        GUi = 0;
        Graph.Run(Fby::Ecs::Phase::Ui, Context);
        Check(GUi == 1, "paused graph runs the Ui phase when requested");
        Graph.SetPaused(true, false);
        GUi = 0;
        Graph.Run(Fby::Ecs::Phase::Ui, Context);
        Check(GUi == 0, "paused graph stops the Ui phase by default");
    }

    {
        // The debug access validator must accept a declared read and must not
        // disturb the normal scheduling path.
        Fby::Ecs::SystemGraph Graph;
        const Fby::Entity ScopedEntity = Registry.create();
        Registry.emplace<ValidationProbe>(ScopedEntity, ValidationProbe{7});
        Fby::Ecs::SystemAccess ScopedAccess;
        ScopedAccess.ComponentReads = Fby::Ecs::MakeComponentAccessMask<ValidationProbe>();
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "validated", &RunValidated, nullptr, 0, 0, ScopedAccess),
              "validated system registers");
        Check(Graph.Build(), "validated graph builds");
        Graph.SetTimingEnabled(true);
        Check(Graph.TimingEnabled(), "timing can be enabled");
        Graph.Run(Fby::Ecs::Phase::PreUpdate, Context);
        Check(Graph.At(0).LastNs > 0, "timing records a non-zero node duration");
    }

    {
        Fby::Ecs::SystemGraph Graph;
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "const_core", Fby::Ecs::BindConstCoreSystem<&RunConst>()),
              "const core system registers");
        Check(Graph.Build(), "const core graph builds");
        GConst = 0;
        Graph.Run(Fby::Ecs::Phase::PreUpdate, Context);
        Check(GConst == 1, "const core system runs");
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
        ScopedWrite.ParallelSafe = true;
        Fby::Ecs::SystemAccess ScopedRead;
        ScopedRead.Reads = Fby::Ecs::kServicePhysics;
        ScopedRead.ParallelSafe = true;
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

    {
        // Component masks extend the same conflict rule to ECS data. The caller
        // still declares the intent; Build() then orders the two systems.
        Fby::Ecs::SystemGraph Graph;
        Fby::Ecs::SystemAccess ScopedComponentWrite;
        ScopedComponentWrite.ComponentWrites = Fby::Ecs::MakeComponentAccessMask<Fby::Transform2D>();
        ScopedComponentWrite.ParallelSafe = true;
        Fby::Ecs::SystemAccess ScopedComponentRead;
        ScopedComponentRead.ComponentReads = Fby::Ecs::MakeComponentAccessMask<Fby::Transform2D>();
        ScopedComponentRead.ParallelSafe = true;
        Check(
            Graph.Add(Fby::Ecs::Phase::FixedUpdate, "component_write", &RunSecond, nullptr, 0, 0, ScopedComponentWrite),
            "add component writer");
        Check(Graph.Add(Fby::Ecs::Phase::FixedUpdate, "component_read", &RunFirst, nullptr, 0, 0, ScopedComponentRead),
              "add component reader");
        Check(Graph.Build(), "component access conflicts build");
        GOrder = 0;
        GFirst = 0;
        GSecond = 0;
        Graph.Run(Fby::Ecs::Phase::FixedUpdate, Context);
        Check(GSecond == 1 && GFirst == 2, "a component write/read conflict keeps registration order");
    }

    {
        // A throwing caller-thread node must not unwind while submitted workers
        // still use the per-wave stack storage.
        Fby::JobSystem ThrowJobs(2);
        Fby::Ecs::SystemContext ThrowContext{Registry, ThrowJobs};
        Fby::Ecs::SystemGraph Graph;
        Fby::Ecs::SystemAccess ScopedParallel;
        ScopedParallel.ParallelSafe = true;
        ScopedParallel.NoEcsAccess = true;
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "throws", &RunThrowing, nullptr, 0, 0, ScopedParallel),
              "add throwing node");
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "slow", &RunSlow, nullptr, 0, 0, ScopedParallel), "add slow node");
        Check(Graph.Build(), "exception graph builds");
        GSlow.store(0);
        Graph.Run(Fby::Ecs::Phase::PreUpdate, ThrowContext);
        Check(GSlow.load() == 1, "a throwing inline node drains the wave");
    }

    {
        // A parallel opt-in without any declared access is rejected: the graph
        // cannot prove that the system is independent. An explicit NoEcsAccess
        // is the escape hatch for a pure node.
        Fby::Ecs::SystemGraph Graph;
        Fby::Ecs::SystemAccess ScopedUndeclared;
        ScopedUndeclared.ParallelSafe = true;
        Check(!Graph.Add(Fby::Ecs::Phase::PreUpdate, "undeclared", &RunFirst, nullptr, 0, 0, ScopedUndeclared),
              "parallel system without access is rejected");
        Fby::Ecs::SystemAccess ScopedExplicit;
        ScopedExplicit.ParallelSafe = true;
        ScopedExplicit.NoEcsAccess = true;
        Check(Graph.Add(Fby::Ecs::Phase::PreUpdate, "explicit", &RunFirst, nullptr, 0, 0, ScopedExplicit),
              "explicit no-access parallel system is accepted");
        Check(Fby::Ecs::IsSystemAccessDeclared(ScopedExplicit), "explicit access is declared");
        Check(!Fby::Ecs::IsSystemAccessDeclared(Fby::Ecs::SystemAccess{}), "default access is undeclared");
    }

    if (GFailures == 0)
    {
        std::printf("system_graph: OK (order, cycle, mask validation, remove, access conflict, exception drain)\n");
        return 0;
    }
    std::fprintf(stderr, "system_graph: %d failure(s)\n", GFailures);
    return 1;
}
