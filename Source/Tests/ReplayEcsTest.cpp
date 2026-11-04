// FemboyEngine2D - ECS replay recorder tests.
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/ReplayRecorder.hpp"
#include "Engine/ECS/Snapshot.hpp"

#include <cstdio>
#include <string>
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

[[nodiscard]] Fby::Entity MakeScene(Fby::Ecs::Registry& InRegistry)
{
    const Fby::Entity ScopedEntity = InRegistry.create();
    Fby::Transform2D ScopedTransform;
    ScopedTransform.Position = Fby::Vector2D{0.0f, 0.0f};
    InRegistry.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
    return ScopedEntity;
}

void TestRecordAndVerify()
{
    Fby::Ecs::SnapshotSchema ScopedSchema;
    Check(ScopedSchema.Add<Fby::Transform2D>(), "the schema accepts Transform2D");

    Fby::Ecs::Registry ScopedRecordRegistry;
    const Fby::Entity ScopedRecorded = MakeScene(ScopedRecordRegistry);
    Fby::Ecs::ReplayRecorder ScopedRecorder;
    Check(ScopedRecorder.Begin(ScopedSchema, 30), "the recorder begins");
    for (std::uint32_t ScopedTick = 0; ScopedTick < 120; ++ScopedTick)
    {
        ScopedRecordRegistry.patch<Fby::Transform2D>(ScopedRecorded,
                                                     [ScopedTick](Fby::Transform2D& InTransform)
                                                     {
                                                         InTransform.Position = Fby::Vector2D{
                                                             static_cast<float>(ScopedTick), 0.0f};
                                                     });
        const std::byte ScopedCommand = static_cast<std::byte>(ScopedTick & 0xFFu);
        Check(ScopedRecorder.RecordTick(ScopedRecordRegistry, ScopedTick,
                                        std::span<const std::byte>(&ScopedCommand, 1)),
              "the tick records");
    }
    ScopedRecorder.End();
    Check(ScopedRecorder.Session().FrameCount() == 120, "every tick is recorded");
    Check(ScopedRecorder.Session().KeyframeCount() == 4, "keyframes land on the interval");
    Check(ScopedRecorder.SnapshotBytes() > 0, "snapshots were captured");

    std::vector<std::byte> ScopedBytes;
    Check(ScopedRecorder.Session().Save(&ScopedBytes), "the replay saves");

    Fby::Ecs::ReplayPlayback ScopedPlayback;
    std::string ScopedError;
    Check(ScopedPlayback.Load(ScopedBytes, ScopedSchema, &ScopedError), "the replay loads");
    Check(ScopedError.empty(), "the load reports no error");
    Check(ScopedPlayback.FrameCount() == 120, "the playback sees every frame");
    Check(ScopedPlayback.CommandsAt(30).size() == 1, "the commands are available per tick");

    // A second registry replays the same mutations and matches the keyframes.
    Fby::Ecs::Registry ScopedReplayRegistry;
    const Fby::Entity ScopedReplayed = MakeScene(ScopedReplayRegistry);
    bool ScopedKeyframesMatch = true;
    for (std::uint32_t ScopedTick = 0; ScopedTick < 120; ++ScopedTick)
    {
        ScopedReplayRegistry.patch<Fby::Transform2D>(ScopedReplayed,
                                                     [ScopedTick](Fby::Transform2D& InTransform)
                                                     {
                                                         InTransform.Position = Fby::Vector2D{
                                                             static_cast<float>(ScopedTick), 0.0f};
                                                     });
        if (ScopedTick == 30 || ScopedTick == 90)
        {
            if (!ScopedPlayback.VerifyTick(ScopedReplayRegistry, ScopedTick))
            {
                ScopedKeyframesMatch = false;
            }
        }
    }
    Check(ScopedKeyframesMatch, "the replay reproduces the keyframe states");

    // A divergent registry fails the same keyframe.
    ScopedReplayRegistry.patch<Fby::Transform2D>(ScopedReplayed,
                                                 [](Fby::Transform2D& InTransform)
                                                 {
                                                     InTransform.Position = Fby::Vector2D{999.0f, 999.0f};
                                                 });
    Check(!ScopedPlayback.VerifyTick(ScopedReplayRegistry, 90), "a diverged state fails the keyframe");
}

void TestEmptySchema()
{
    Fby::Ecs::SnapshotSchema ScopedSchema;
    Fby::Ecs::ReplayRecorder ScopedRecorder;
    Check(!ScopedRecorder.Begin(ScopedSchema, 30), "an empty schema is rejected");
    Fby::Ecs::ReplayPlayback ScopedPlayback;
    std::string ScopedError;
    Check(!ScopedPlayback.Load({}, ScopedSchema, &ScopedError), "an empty schema cannot load a replay");
}
}

int main()
{
    TestRecordAndVerify();
    TestEmptySchema();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d replay ecs test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("replay_ecs: OK\n");
    return 0;
}
