// FemboyEngine2D - snapshot/delta size report used by the memory plan.
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Snapshot.hpp"
#include "Engine/Core/DeterministicRng.hpp"

#include <cstddef>
#include <cstdio>
#include <vector>

int main()
{
    using namespace Fby;
    Ecs::Registry ScopedRegistry;
    constexpr std::uint32_t kEntityCount = 10000;
    std::vector<Entity> ScopedEntities;
    ScopedEntities.reserve(kEntityCount);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kEntityCount; ++ScopedIndex)
    {
        const Entity ScopedEntity = ScopedRegistry.create();
        ScopedEntities.push_back(ScopedEntity);
        ScopedRegistry.emplace<Transform2D>(
            ScopedEntity, Transform2D{Vector2D{static_cast<float>(ScopedIndex), 0.0f}, Vector2D{1.0f, 1.0f}, 0.0f});
    }
    const Ecs::SnapshotSchema ScopedSchema = Ecs::SnapshotSchema::BuiltIns();
    std::vector<std::byte> ScopedFull(Ecs::SnapshotSize(ScopedRegistry, ScopedSchema, 0) + 64);
    DeterministicRng ScopedRng = DeterministicRng::FromSeed(1u, 1u);
    std::size_t ScopedFullBytes = 0;
    if (!Ecs::CaptureSnapshot(ScopedRegistry, ScopedSchema, ScopedFull, 1, ScopedRng, &ScopedFullBytes))
    {
        std::printf("snapshot_size_report: capture failed\n");
        return 1;
    }
    ScopedFull.resize(ScopedFullBytes);

    // Five percent of the rows move; the delta should keep only those rows.
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kEntityCount; ScopedIndex += 20)
    {
        ScopedRegistry.get<Transform2D>(ScopedEntities[ScopedIndex]).Position.X += 1.0f;
    }
    std::vector<std::byte> ScopedDelta(Ecs::SnapshotSize(ScopedRegistry, ScopedSchema, 0) + 64);
    std::size_t ScopedDeltaBytes = 0;
    if (!Ecs::CaptureDelta(ScopedRegistry, ScopedSchema, ScopedFull, ScopedDelta, 2, ScopedRng, &ScopedDeltaBytes))
    {
        std::printf("snapshot_size_report: delta failed\n");
        return 1;
    }
    const double ScopedRatio =
        ScopedFullBytes == 0 ? 0.0 : static_cast<double>(ScopedDeltaBytes) / static_cast<double>(ScopedFullBytes);
    std::printf("snapshot_size_report: entities=%u full=%zu delta=%zu ratio=%.3f\n",
                static_cast<unsigned>(kEntityCount), ScopedFullBytes, ScopedDeltaBytes, ScopedRatio);
    // The delta already tracks changed rows; payload compression would only buy
    // a fraction of an already proportional payload and costs rollback latency.
    return ScopedDeltaBytes < ScopedFullBytes ? 0 : 1;
}
