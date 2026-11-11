// FemboyEngine2D - destruction command replay tests.
#include "Engine/ECS/CommandLog.hpp"
#include "Engine/Physics/SdfDestructionCommands.hpp"

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

[[nodiscard]] Fby::SdfDestructible MakeObject()
{
    Fby::SdfDestructibleDesc ScopedDesc;
    ScopedDesc.Origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedDesc.CellSize = Fby::Vector2D{1.0f, 1.0f};
    ScopedDesc.Width = 64;
    ScopedDesc.Height = 64;
    ScopedDesc.BaseShape = Fby::SdfCarveShape::Box;
    ScopedDesc.HalfExtents = Fby::Vector2D{32.0f, 32.0f};
    ScopedDesc.NarrowBand = 8.0f;
    Fby::SdfDestructible ScopedObject;
    (void)ScopedObject.Configure(ScopedDesc);
    return ScopedObject;
}

void TestCarveCommandRoundTrip()
{
    Fby::SdfDestructible ScopedObject = MakeObject();
    Fby::SdfCarveOp ScopedHole;
    ScopedHole.Shape = Fby::SdfCarveShape::Circle;
    ScopedHole.Center = Fby::Vector2D{32.0f, 32.0f};
    ScopedHole.Radius = 8.0f;

    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedEntity = ScopedRegistry.create();
    Fby::Ecs::CommandLog ScopedLog;
    Check(Fby::PushCarveCommand(ScopedLog, ScopedEntity, ScopedHole, 7), "the carve command appends");
    Check(ScopedLog.Count() == 1, "the log holds one command");
    ScopedLog.Seal();

    const Fby::Ecs::CommandLog::View ScopedView =
        Fby::Ecs::CommandLog::View::Open(ScopedLog.Bytes(), nullptr);
    Check(ScopedView.header.version == Fby::Ecs::kCommandLogVersion, "the log is v4");
    Check(ScopedView.header.commandCount == 1, "the view sees the carve");

    // Replay applies the carve through the callback.
    Fby::SdfCarveReplayContext ScopedContext;
    ScopedContext.Object = &ScopedObject;
    Fby::Ecs::CommandWorldCallbacks ScopedCallbacks;
    ScopedCallbacks.user = &ScopedContext;
    ScopedCallbacks.carve = &Fby::ReplayCarveCommand;
    Fby::Ecs::SnapshotSchema ScopedSchema;
    Check(ScopedLog.Replay(ScopedRegistry, ScopedSchema, &ScopedCallbacks), "the carve replays");
    Check(ScopedContext.Applied == 1, "one carve is applied");
    Check(ScopedContext.Object->Ops().size() == 1, "the object records the carve");
    Check(ScopedContext.Object->Sample(Fby::Vector2D{32.0f, 32.0f}) > 0.0f, "the replayed hole is empty");

    // Applying the same log to a second object reproduces the field exactly.
    Fby::SdfDestructible ScopedSecond = MakeObject();
    Fby::SdfCarveReplayContext ScopedSecondContext;
    ScopedSecondContext.Object = &ScopedSecond;
    ScopedCallbacks.user = &ScopedSecondContext;
    Check(ScopedLog.Replay(ScopedRegistry, ScopedSchema, &ScopedCallbacks), "the carve replays again");
    bool ScopedMatches = true;
    for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 64; ++ScopedX)
        {
            const Fby::Vector2D ScopedPoint{static_cast<float>(ScopedX), static_cast<float>(ScopedY)};
            if (ScopedSecond.Sample(ScopedPoint) != ScopedContext.Object->Sample(ScopedPoint))
            {
                ScopedMatches = false;
            }
        }
    }
    Check(ScopedMatches, "the replayed carve is deterministic");

    // The cursor API applies the same command.
    Fby::SdfDestructible ScopedThird = MakeObject();
    Fby::SdfCarveReplayContext ScopedThirdContext;
    ScopedThirdContext.Object = &ScopedThird;
    ScopedCallbacks.user = &ScopedThirdContext;
    Fby::Ecs::CommandLog::Cursor ScopedCursor;
    Check(ScopedLog.ApplyUntil(ScopedRegistry, ScopedSchema, 7, ScopedCursor, &ScopedCallbacks),
          "the cursor applies the carve");
    Check(ScopedThirdContext.Applied == 1, "the cursor applied one carve");
}

void TestDecode()
{
    Fby::SdfCarveOp ScopedOp;
    ScopedOp.Shape = Fby::SdfCarveShape::Box;
    ScopedOp.Center = Fby::Vector2D{1.0f, 2.0f};
    ScopedOp.HalfExtents = Fby::Vector2D{3.0f, 4.0f};
    const auto* ScopedBytes = reinterpret_cast<const std::byte*>(&ScopedOp);
    Fby::SdfCarveOp ScopedDecoded;
    Check(Fby::DecodeCarveCommand(std::span<const std::byte>(ScopedBytes, sizeof(ScopedOp)), &ScopedDecoded),
          "a full payload decodes");
    Check(ScopedDecoded.Center.X == 1.0f && ScopedDecoded.HalfExtents.Y == 4.0f, "the decoded fields match");
    Check(!Fby::DecodeCarveCommand(std::span<const std::byte>(ScopedBytes, sizeof(ScopedOp) - 1), &ScopedDecoded),
          "a short payload is rejected");
    Check(!Fby::DecodeCarveCommand(std::span<const std::byte>(ScopedBytes, sizeof(ScopedOp)), nullptr),
          "a null output is rejected");
    Check(!Fby::ReplayCarveCommand(nullptr, Fby::kNullEntity, {}), "a null context is rejected");
}
}

int main()
{
    TestCarveCommandRoundTrip();
    TestDecode();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d destruction command test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_destruction_commands: OK\n");
    return 0;
}
