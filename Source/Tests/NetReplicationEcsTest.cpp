// FemboyEngine2D - ECS baseline/delta replication and relevance filter tests.
#include "Engine/ECS/Components.hpp"
#include "Engine/Network/RelevanceGrid.hpp"
#include "Engine/Network/ReplicationEcs.hpp"

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
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

[[nodiscard]] float FirstTransformX(const Fby::Ecs::Registry& InRegistry)
{
    const auto ScopedView = InRegistry.view<Fby::Transform2D>();
    for (const auto ScopedEntity : ScopedView)
    {
        return InRegistry.get<Fby::Transform2D>(ScopedEntity).Position.X;
    }
    return 0.0f;
}

void TestBaselineAndDelta()
{
    using namespace Fby;
    using Fby::Net::NetEntityId;
    using Fby::Net::NetPlayerId;
    using Fby::Net::NetRollbackSession;
    using Fby::Net::RelevanceConfig;
    using Fby::Net::RelevancePlan;

    Ecs::Registry ScopedRegistry;
    const Entity ScopedEntity = ScopedRegistry.create();
    Transform2D ScopedTransform{};
    ScopedTransform.Position = Vector2D{1.0f, 2.0f};
    ScopedRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
    Velocity ScopedVelocity{};
    ScopedVelocity.Value = Vector2D{0.5f, 0.0f};
    ScopedRegistry.emplace<Velocity>(ScopedEntity, ScopedVelocity);

    const Ecs::SnapshotSchema ScopedSchema = Ecs::SnapshotSchema::BuiltIns();
    DeterministicRng ScopedRng = Pcg32::FromSeed(0x1234u, 7u);

    std::vector<std::byte> ScopedBaseline(65536);
    std::size_t ScopedBaselineBytes = 0;
    FBY_CHECK(Net::CaptureReplicationBaseline(ScopedRegistry, ScopedSchema, 1, ScopedRng, ScopedBaseline,
                                              &ScopedBaselineBytes));
    FBY_CHECK(ScopedBaselineBytes > 0);

    ScopedRegistry.get<Transform2D>(ScopedEntity).Position = Vector2D{10.0f, 20.0f};
    std::vector<std::byte> ScopedDelta(65536);
    std::size_t ScopedDeltaBytes = 0;
    FBY_CHECK(Net::CaptureReplicationDelta(ScopedRegistry, ScopedSchema,
                                           std::span<const std::byte>(ScopedBaseline.data(), ScopedBaselineBytes), 2,
                                           ScopedRng, ScopedDelta, &ScopedDeltaBytes));
    FBY_CHECK(ScopedDeltaBytes > 0);

    Ecs::Registry ScopedClient;
    FBY_CHECK(Net::ApplyReplicationBaseline(ScopedClient, ScopedSchema,
                                            std::span<const std::byte>(ScopedBaseline.data(), ScopedBaselineBytes)));
    FBY_CHECK(Net::ApplyReplicationDelta(
        ScopedClient, ScopedSchema, std::span<const std::byte>(ScopedBaseline.data(), ScopedBaselineBytes),
        std::span<const std::byte>(ScopedDelta.data(), ScopedDeltaBytes)));
    FBY_CHECK(std::fabs(FirstTransformX(ScopedClient) - 10.0f) < 1e-4f);

    // In-place delta: the client keeps the same entity handle across the update.
    std::size_t ScopedBase2Bytes = 0;
    FBY_CHECK(Net::CaptureReplicationBaseline(ScopedClient, ScopedSchema, 3, ScopedRng, ScopedBaseline,
                                              &ScopedBase2Bytes));
    FBY_CHECK(Net::ApplyReplicationBaseline(
        ScopedClient, ScopedSchema, std::span<const std::byte>(ScopedBaseline.data(), ScopedBase2Bytes)));
    Entity ScopedClientEntity = entt::null;
    for (const auto ScopedHandle : ScopedClient.view<Transform2D>())
    {
        ScopedClientEntity = ScopedHandle;
        break;
    }
    FBY_CHECK(ScopedClientEntity != entt::null);

    ScopedRegistry.get<Transform2D>(ScopedEntity).Position = Vector2D{30.0f, 40.0f};
    std::size_t ScopedDelta2Bytes = 0;
    FBY_CHECK(Net::CaptureReplicationDelta(ScopedRegistry, ScopedSchema,
                                           std::span<const std::byte>(ScopedBaseline.data(), ScopedBase2Bytes), 4,
                                           ScopedRng, ScopedDelta, &ScopedDelta2Bytes));
    FBY_CHECK(Net::ApplyReplicationDeltaInPlace(
        ScopedClient, ScopedSchema, std::span<const std::byte>(ScopedDelta.data(), ScopedDelta2Bytes)));
    FBY_CHECK(ScopedClient.valid(ScopedClientEntity));
    FBY_CHECK(std::fabs(ScopedClient.get<Transform2D>(ScopedClientEntity).Position.X - 30.0f) < 1e-4f);

    // Relevance filtering keeps the plan-compatible records and stays callable
    // for a mapped entity.
    NetRollbackSession ScopedSession;
    NetRollbackSession::Config ScopedConfig{};
    ScopedConfig.playerCount = 2;
    ScopedConfig.localPlayer = 0;
    ScopedConfig.rollbackTicks = 8;
    std::vector<std::byte> ScopedArena(1u << 20);
    FBY_CHECK(ScopedSession.Initialize(ScopedRegistry, ScopedSchema, ScopedConfig, ScopedArena) ==
              NetRollbackSession::Result::Ok);
    (void)ScopedSession.RegisterEntity(ScopedEntity, NetEntityId{1});

    RelevanceConfig ScopedRelevance{};
    FBY_CHECK(ScopedSession.ConfigureRelevanceGrid(ScopedRelevance) == NetRollbackSession::Result::Ok);
    FBY_CHECK(ScopedSession.SetRelevancePlayerPosition(NetPlayerId{0}, Vector2D{0.0f, 0.0f}) ==
              NetRollbackSession::Result::Ok);
    (void)ScopedSession.RefreshRelevanceGrid();
    RelevancePlan ScopedPlan{};
    if (ScopedSession.BuildRelevancePlan(NetPlayerId{0}, 2, ScopedPlan) == NetRollbackSession::Result::Ok)
    {
        std::vector<std::byte> ScopedFiltered(65536);
        std::size_t ScopedFilteredBytes = 0;
        FBY_CHECK(Net::FilterReplicationDelta(ScopedSession, ScopedPlan,
                                              std::span<const std::byte>(ScopedDelta.data(), ScopedDeltaBytes),
                                              ScopedFiltered, &ScopedFilteredBytes));
    }
    ScopedSession.Shutdown();
}
}

int main()
{
    TestBaselineAndDelta();

    if (GFailures == 0)
    {
        std::printf("net_replication_ecs_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "net_replication_ecs_test: %u failure(s)\n", GFailures);
    return 1;
}
