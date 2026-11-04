// FemboyEngine2D - replay session tests.
#include "Engine/Core/Replay.hpp"

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

// Toy simulation: the state is a running FNV hash of the payload bytes, so the
// replay can independently rebuild the hash the recorder stored.
[[nodiscard]] std::uint64_t StepState(std::uint64_t InState, const std::vector<std::byte>& InPayload)
{
    std::uint64_t ScopedHash = InState;
    for (const std::byte ScopedByte : InPayload)
    {
        ScopedHash ^= static_cast<std::uint64_t>(ScopedByte);
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

void TestRecordReplay()
{
    Fby::Core::ReplaySession ScopedRecorder;
    ScopedRecorder.BeginRecord(30);
    Check(ScopedRecorder.IsRecording(), "recording starts");
    std::uint64_t ScopedState = 1469598103934665603ull;
    std::vector<std::vector<std::byte>> ScopedPayloads;
    for (std::uint32_t ScopedTick = 0; ScopedTick < 120; ++ScopedTick)
    {
        std::vector<std::byte> ScopedPayload = {static_cast<std::byte>(ScopedTick & 0xFFu),
                                                static_cast<std::byte>(ScopedTick >> 8)};
        ScopedState = StepState(ScopedState, ScopedPayload);
        ScopedPayloads.push_back(ScopedPayload);
        Check(ScopedRecorder.RecordFrame(ScopedTick, ScopedPayload, ScopedState), "the frame records");
    }
    ScopedRecorder.EndRecord();
    Check(!ScopedRecorder.IsRecording(), "recording stops");
    Check(ScopedRecorder.FrameCount() == 120, "every tick is recorded");
    Check(ScopedRecorder.KeyframeCount() == 4, "keyframes land on the interval");
    Check(ScopedRecorder.Keyframes()[1].Tick == 30, "the second keyframe is at tick 30");

    // The replay rebuilds the same hash and matches the stored keyframes.
    std::uint64_t ScopedReplay = 1469598103934665603ull;
    for (std::uint32_t ScopedTick = 0; ScopedTick < 120; ++ScopedTick)
    {
        const std::span<const std::byte> ScopedPayload = ScopedRecorder.PayloadAtTick(ScopedTick);
        Check(!ScopedPayload.empty(), "the payload is available for the tick");
        ScopedReplay = StepState(ScopedReplay, ScopedPayloads[ScopedTick]);
        if (ScopedTick == 30 || ScopedTick == 90)
        {
            Check(ScopedRecorder.VerifyKeyframe(ScopedTick, ScopedReplay), "the replay matches the keyframe");
        }
    }
    Check(!ScopedRecorder.VerifyKeyframe(30, ScopedReplay ^ 1ull), "a different hash fails the keyframe");
    Check(!ScopedRecorder.VerifyKeyframe(45, ScopedReplay), "a non-keyframe tick has nothing to verify");
    Check(ScopedRecorder.PayloadAtTick(500).empty(), "an unrecorded tick has no payload");
}

void TestPersistence()
{
    Fby::Core::ReplaySession ScopedRecorder;
    ScopedRecorder.BeginRecord(10);
    std::vector<std::byte> ScopedEmpty;
    Check(ScopedRecorder.RecordFrame(0, ScopedEmpty, 1), "an empty payload records");
    std::vector<std::byte> ScopedPayload = {std::byte{7}, std::byte{9}};
    Check(ScopedRecorder.RecordFrame(5, ScopedPayload, 2), "the second payload records");
    {
        std::vector<std::byte> ScopedPayload2 = {std::byte{1}};
        Check(ScopedRecorder.RecordFrame(11, ScopedPayload2, 3), "the third payload records");
    }
    ScopedRecorder.EndRecord();

    std::vector<std::byte> ScopedBytes;
    Check(ScopedRecorder.Save(&ScopedBytes), "the session saves");
    Fby::Core::ReplaySession ScopedLoaded;
    std::string ScopedError;
    Check(ScopedLoaded.Load(ScopedBytes, &ScopedError), "the session loads");
    Check(ScopedError.empty(), "the load reports no error");
    Check(ScopedLoaded.FrameCount() == 3, "the frame count is restored");
    Check(ScopedLoaded.KeyframeCount() == 2, "keyframes are restored");
    Check(ScopedLoaded.RollupHash() == ScopedRecorder.RollupHash(), "the rollup hash is restored");
    Check(ScopedLoaded.PayloadAtTick(5).size() == 2 && ScopedLoaded.PayloadAtTick(5)[1] == std::byte{9},
          "the payload bytes are restored");
    Check(!ScopedLoaded.VerifyKeyframe(99, 0), "an unknown keyframe does not verify");

    // Corruption is rejected.
    std::vector<std::byte> ScopedTruncated(ScopedBytes.begin(), ScopedBytes.end() - 1);
    Check(!ScopedLoaded.Load(ScopedTruncated, &ScopedError), "a truncated replay is rejected");
    std::vector<std::byte> ScopedMagic = ScopedBytes;
    ScopedMagic[0] = std::byte{0x00};
    Check(!ScopedLoaded.Load(ScopedMagic, &ScopedError), "a wrong magic is rejected");
    std::vector<std::byte> ScopedTrailing = ScopedBytes;
    ScopedTrailing.push_back(std::byte{0x00});
    Check(!ScopedLoaded.Load(ScopedTrailing, &ScopedError), "trailing bytes are rejected");

    // Recording after load must be re-enabled explicitly.
    std::vector<std::byte> ScopedLate = {std::byte{1}};
    Check(!ScopedLoaded.RecordFrame(50, ScopedLate, 0), "a loaded session does not record");
}
}

int main()
{
    TestRecordReplay();
    TestPersistence();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d replay test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("replay: OK\n");
    return 0;
}
