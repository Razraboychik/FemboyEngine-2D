// FemboyEngine2D - snapshot round-trip properties.
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Snapshot.hpp"

#include <algorithm>
#include <cstdio>
#include <span>
#include <vector>

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
}

int main()
{
    using namespace Fby;

    Ecs::SnapshotSchema ScopedSchema;
    Check(ScopedSchema.Add<Transform2D>(), "the schema accepts Transform2D");
    Check(ScopedSchema.Add<Velocity>(), "the schema accepts Velocity");

    for (std::uint32_t ScopedRound = 0; ScopedRound < 64; ++ScopedRound)
    {
        Ecs::Registry ScopedRegistry;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 24; ++ScopedIndex)
        {
            const Entity ScopedEntity = ScopedRegistry.create();
            Transform2D ScopedTransform;
            ScopedTransform.Position = Vector2D{static_cast<float>(ScopedRound * 24u + ScopedIndex),
                                                static_cast<float>(ScopedIndex * 3u)};
            ScopedTransform.Scale = Vector2D{1.0f + static_cast<float>(ScopedRound) * 0.01f, 1.0f};
            ScopedTransform.Rotation = static_cast<float>(ScopedIndex) * 0.1f;
            ScopedRegistry.emplace<Transform2D>(ScopedEntity, ScopedTransform);
            if ((ScopedIndex & 1u) == 0)
            {
                Velocity ScopedVelocity;
                ScopedVelocity.Value = Vector2D{static_cast<float>(ScopedIndex), -static_cast<float>(ScopedRound)};
                ScopedRegistry.emplace<Velocity>(ScopedEntity, ScopedVelocity);
            }
        }

        const std::size_t ScopedSize = Ecs::SnapshotSize(ScopedRegistry, ScopedSchema);
        Check(ScopedSize > 0, "the snapshot has a size");
        std::vector<std::byte> ScopedFirst(ScopedSize);
        const DeterministicRng ScopedRng = DeterministicRng::FromSeed(ScopedRound + 1u);
        std::size_t ScopedFirstBytes = 0;
        Check(Ecs::CaptureSnapshot(ScopedRegistry, ScopedSchema, ScopedFirst, ScopedRound, ScopedRng, &ScopedFirstBytes),
              "the first snapshot captures");

        for (const Entity ScopedEntity : ScopedRegistry.view<Transform2D>())
        {
            ScopedRegistry.patch<Transform2D>(ScopedEntity,
                                              [](Transform2D& InTransform)
                                              {
                                                  InTransform.Position = Vector2D{-999.0f, 999.0f};
                                              });
        }

        DeterministicRng ScopedRestoredRng = DeterministicRng::FromSeed(999u);
        Check(Ecs::RestoreSnapshot(ScopedRegistry, ScopedSchema,
                                   std::span<const std::byte>(ScopedFirst.data(), ScopedFirstBytes),
                                   &ScopedRestoredRng),
              "the snapshot restores");

        std::vector<std::byte> ScopedSecond(ScopedSize);
        std::size_t ScopedSecondBytes = 0;
        Check(Ecs::CaptureSnapshot(ScopedRegistry, ScopedSchema, ScopedSecond, ScopedRound, ScopedRestoredRng,
                                   &ScopedSecondBytes),
              "the second snapshot captures");
        Check(ScopedSecondBytes == ScopedFirstBytes, "the snapshot size is stable");
        Check(ScopedSecondBytes == ScopedFirstBytes &&
                  std::equal(ScopedFirst.begin(), ScopedFirst.begin() + static_cast<std::ptrdiff_t>(ScopedFirstBytes),
                             ScopedSecond.begin()),
              "the snapshot bytes are stable");
    }

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d snapshot property failure(s)\n", GFailures);
        return 1;
    }
    std::printf("snapshot_property: OK\n");
    return 0;
}
