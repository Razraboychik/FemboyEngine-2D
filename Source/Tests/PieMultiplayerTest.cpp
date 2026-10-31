// FemboyEngine2D - PIE multiplayer host/client world replication test.
#include "Editor/Infrastructure/PlayInEditor.hpp"

#include "Engine/Core/JobSystem.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/SystemGraph.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

[[nodiscard]] float FirstX(Fby::Ecs::Registry& InRegistry)
{
    for (const auto ScopedEntity : InRegistry.view<Fby::Transform2D>())
    {
        return InRegistry.get<Fby::Transform2D>(ScopedEntity).Position.X;
    }
    return -1.0f;
}
}

int main()
{
    using namespace Fby;

    JobSystem ScopedJobs(1);
    Ecs::SnapshotSchema ScopedSchema = Ecs::SnapshotSchema::BuiltIns();
    (void)ScopedSchema.Add<Transform2D>();
    (void)ScopedSchema.Add<Velocity>();

    std::vector<std::byte> ScopedArena(1u << 20);
    Editor::PlayInEditor ScopedPie;
    FBY_CHECK(ScopedPie.Initialize(ScopedArena, ScopedSchema, ScopedArena.size()));

    Ecs::Registry ScopedEditorWorld;
    const Entity ScopedEntity = ScopedEditorWorld.create();
    ScopedEditorWorld.emplace<Transform2D>(
        ScopedEntity, Transform2D{Vector2D{1.0f, 0.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    ScopedEditorWorld.emplace<Velocity>(ScopedEntity, Velocity{Vector2D{0.0f, 0.0f}});

    Ecs::SystemContext ScopedServices{ScopedEditorWorld, ScopedJobs};
    FBY_CHECK(ScopedPie.StartMultiplayer(ScopedEditorWorld, ScopedSchema, ScopedServices, 1, 1, nullptr) ==
              Editor::PlayInEditor::Result::Ok);
    FBY_CHECK(ScopedPie.Multiplayer());
    FBY_CHECK(ScopedPie.ClientCount() == 1);
    FBY_CHECK(ScopedPie.ClientWorld().view<Transform2D>().size() == 1);
    FBY_CHECK(FirstX(ScopedPie.ClientWorld()) > 0.99f && FirstX(ScopedPie.ClientWorld()) < 1.01f);

    Ecs::SystemGraph ScopedGraph;

    const Editor::PiePlayerInput ScopedInput{1.5f, -0.5f, 3};
    FBY_CHECK(ScopedPie.SendClientInput(ScopedInput));
    FBY_CHECK(ScopedPie.StepMultiplayer(ScopedGraph, 1.0f / 60.0f));
    FBY_CHECK(ScopedPie.HasClientInput());
    FBY_CHECK(std::fabs(ScopedPie.ClientInputOnHost().MoveX - 1.5f) < 1e-5f);
    FBY_CHECK(ScopedPie.ClientInputOnHost().Buttons == 3);

    // Host state changes must reach the client world after a multiplayer step.
    for (const auto ScopedHostEntity : ScopedPie.World().view<Transform2D>())
    {
        ScopedPie.World().get<Transform2D>(ScopedHostEntity).Position = Vector2D{7.0f, 0.0f};
    }
    FBY_CHECK(ScopedPie.StepMultiplayer(ScopedGraph, 1.0f / 60.0f));
    FBY_CHECK(std::fabs(FirstX(ScopedPie.ClientWorld()) - 7.0f) < 1e-4f);

    ScopedPie.Stop();
    FBY_CHECK(!ScopedPie.Active());
    FBY_CHECK(!ScopedPie.Multiplayer());
    FBY_CHECK(!ScopedPie.ExternalClient());

    // A missing sandbox binary is the documented fallback: the session and the
    // local client world still start, only the second process is absent.
    Editor::ExternalClientDesc ScopedMissing;
    ScopedMissing.executable = "fby_sandbox_that_does_not_exist";
    FBY_CHECK(ScopedPie.StartMultiplayer(ScopedEditorWorld, ScopedSchema, ScopedServices, 1, 1, nullptr,
                                         &ScopedMissing) == Editor::PlayInEditor::Result::Ok);
    FBY_CHECK(ScopedPie.Multiplayer());
    FBY_CHECK(!ScopedPie.ExternalClient());
    FBY_CHECK(ScopedPie.ClientWorld().view<Transform2D>().size() == 1);
    // Keepalive without a second process is a no-op, not a crash.
    ScopedPie.KeepExternalClientAlive(1.0f / 60.0f);
    ScopedPie.Stop();

    if (GFailures == 0)
    {
        std::printf("pie_multiplayer_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "pie_multiplayer_test: %u failure(s)\n", GFailures);
    return 1;
}
