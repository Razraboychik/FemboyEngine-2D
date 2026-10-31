// FemboyEngine2D - plugin load/unload hot reload while jobs run.
#include "Engine/Core/JobSystem.hpp"
#include "Engine/Core/PluginHost.hpp"
#include "Engine/ECS/Components.hpp"

#include <atomic>
#include <cstdio>
#include <string>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

std::atomic<std::uint32_t> GJobHits{0};
}

int main()
{
    using namespace Fby;

    Plugin::ComponentRegistry ScopedComponents;
    ScopedComponents.Register<Transform2D>("Transform2D");
    ScopedComponents.Register<Velocity>("Velocity");
    entt::registry ScopedRegistry;
    Plugin::PluginHost ScopedHost(ScopedComponents, &ScopedRegistry);

    std::string ScopedError;
    Check(ScopedHost.Load(FBY_TEST_PLUGIN_PATH, &ScopedError), "the fixture plugin loads");
    Check(ScopedHost.Plugins().size() == 1, "one plugin is registered");
    ScopedHost.UnloadAll();
    Check(ScopedHost.Plugins().empty(), "unload clears the plugin list");

    // Reload the shared object ten times while four workers run.
    JobSystem ScopedJobs(4);
    GJobHits = 0;
    ScopedJobs.ParallelFor(2048, 64,
                           [](std::uint32_t InBegin, std::uint32_t InEnd)
                           {
                               for (std::uint32_t ScopedI = InBegin; ScopedI < InEnd; ++ScopedI)
                               {
                                   (void)ScopedI;
                                   GJobHits.fetch_add(1, std::memory_order_relaxed);
                               }
                           });

    for (std::uint32_t ScopedIteration = 0; ScopedIteration < 10; ++ScopedIteration)
    {
        const bool ScopedLoaded = ScopedHost.Load(FBY_TEST_PLUGIN_PATH, &ScopedError);
        if (!ScopedLoaded)
        {
            std::fprintf(stderr, "reload failed: %s\n", ScopedError.c_str());
        }
        Check(ScopedLoaded, "the plugin reloads");
        Check(ScopedHost.Plugins().size() == 1, "one plugin after reload");
        ScopedHost.UnloadAll();
        Check(ScopedHost.Plugins().empty(), "unload after reload");
    }

    ScopedJobs.WaitIdle();
    Check(GJobHits.load() == 2048, "the background jobs completed during the reloads");

    // The bad-layout fixture must be rejected, not crash the host.
    if (!ScopedHost.Load(FBY_TEST_BAD_PLUGIN_PATH, &ScopedError))
    {
        Check(!ScopedError.empty(), "the bad-layout plugin reports an error");
    }
    else
    {
        Check(false, "the bad-layout plugin must not load");
        ScopedHost.UnloadAll();
    }

    if (GFailures == 0)
    {
        std::printf("plugin_reload: OK\n");
        return 0;
    }
    std::fprintf(stderr, "plugin_reload: %d failure(s)\n", GFailures);
    return 1;
}
