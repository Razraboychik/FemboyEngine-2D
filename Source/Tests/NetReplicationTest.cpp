// FemboyEngine2D - replication channel tests with loss, latency and budgets.
#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/Replication.hpp"

#include <cstdint>
#include <cstdio>
#include <span>
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

struct ApplyRecorder
{
    std::vector<std::uint64_t> Ticks;
    std::uint64_t LastChecksum = 0;
    std::uint32_t LastBytes = 0;
};

void OnApply(void* InUser, std::uint64_t InTick, std::span<const std::byte> InState) noexcept
{
    auto* ScopedRecorder = static_cast<ApplyRecorder*>(InUser);
    ScopedRecorder->Ticks.push_back(InTick);
    ScopedRecorder->LastChecksum = Fby::Net::ReplicationChecksum(InState);
    ScopedRecorder->LastBytes = static_cast<std::uint32_t>(InState.size());
}

[[nodiscard]] std::vector<std::byte> BuildState(std::uint64_t InTick, std::uint32_t InBytes)
{
    std::vector<std::byte> ScopedState(InBytes);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InBytes; ++ScopedIndex)
    {
        ScopedState[ScopedIndex] =
            static_cast<std::byte>((InTick * 31u + ScopedIndex * 7u + 11u) & 0xFFu);
    }
    return ScopedState;
}

struct Fixture
{
    Fby::Net::MemoryNetwork Network;
    Fby::Net::MemoryEndpoint SenderEndpoint;
    Fby::Net::MemoryEndpoint ReceiverEndpoint;
    Fby::Net::SnapshotSender Sender;
    Fby::Net::SnapshotReceiver Receiver;
    ApplyRecorder Recorder;
};

[[nodiscard]] bool OpenFixture(Fixture& InFixture, Fby::Net::SnapshotSenderConfig InSenderConfig)
{
    using namespace Fby::Net;

    if (!InFixture.SenderEndpoint.Open(&InFixture.Network, 0) ||
        !InFixture.ReceiverEndpoint.Open(&InFixture.Network, 0))
    {
        return false;
    }
    if (!InFixture.Sender.Start(InFixture.SenderEndpoint.Link(), InFixture.ReceiverEndpoint.Address(),
                                InSenderConfig))
    {
        return false;
    }

    SnapshotReceiverConfig ScopedReceiverConfig{};
    ScopedReceiverConfig.FixedDtMs = 8;
    return InFixture.Receiver.Start(InFixture.ReceiverEndpoint.Link(), InFixture.SenderEndpoint.Address(),
                                    ScopedReceiverConfig, &OnApply, &InFixture.Recorder);
}

void TestLossAndLatency()
{
    using namespace Fby::Net;

    Fixture ScopedFixture;
    SnapshotSenderConfig ScopedSenderConfig{};
    ScopedSenderConfig.RetransmitMs = 20;
    ScopedSenderConfig.MaxAttempts = 32;
    FBY_CHECK(OpenFixture(ScopedFixture, ScopedSenderConfig));

    // 20 percent loss and two ticks of delivery latency.
    ScopedFixture.Network.SetDropEvery(5);
    ScopedFixture.Network.SetLatencyTicks(2);

    const std::vector<std::byte> ScopedBaseline = BuildState(1, 1000);
    FBY_CHECK(ScopedFixture.Sender.PublishBaseline(1, ScopedBaseline, 0));

    std::uint64_t ScopedNextTick = 2;
    std::uint64_t ScopedNow = 0;
    std::vector<std::vector<std::byte>> ScopedExpected;
    ScopedExpected.push_back(ScopedBaseline);

    for (std::uint32_t ScopedStep = 0; ScopedStep < 800 && ScopedFixture.Receiver.AppliedTick() < 10;
         ++ScopedStep)
    {
        ScopedNow = static_cast<std::uint64_t>(ScopedStep) * 8;
        if (ScopedStep > 0 && ScopedStep % 20 == 0 && ScopedNextTick <= 10)
        {
            const std::vector<std::byte> ScopedDelta = BuildState(ScopedNextTick, 300 + ScopedNextTick * 10);
            ScopedExpected.push_back(ScopedDelta);
            FBY_CHECK(ScopedFixture.Sender.PublishDelta(ScopedNextTick, 1, ScopedDelta, ScopedNow));
            ++ScopedNextTick;
        }
        ScopedFixture.Network.AdvanceTick();
        ScopedFixture.Sender.Tick(ScopedNow);
        ScopedFixture.Receiver.Tick(ScopedNow);
    }

    FBY_CHECK(ScopedFixture.Receiver.AppliedTick() == 10);
    FBY_CHECK(ScopedFixture.Receiver.AppliedStates() == 10);
    FBY_CHECK(ScopedFixture.Receiver.ChecksumFailures() == 0);
    FBY_CHECK(ScopedFixture.Recorder.Ticks.size() == 10);

    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedFixture.Recorder.Ticks.size(); ++ScopedIndex)
    {
        FBY_CHECK(ScopedFixture.Recorder.Ticks[ScopedIndex] == ScopedIndex + 1);
    }
    if (!ScopedExpected.empty())
    {
        FBY_CHECK(ScopedFixture.Recorder.LastChecksum == ReplicationChecksum(ScopedExpected.back()));
    }
    FBY_CHECK(ScopedFixture.Sender.Stats().ChunksSent > 0);
    FBY_CHECK(ScopedFixture.Sender.Stats().Acks > 0);
}

void TestBaselineResync()
{
    using namespace Fby::Net;

    Fixture ScopedFixture;
    SnapshotSenderConfig ScopedSenderConfig{};
    ScopedSenderConfig.RetransmitMs = 8;
    ScopedSenderConfig.MaxAttempts = 4;
    FBY_CHECK(OpenFixture(ScopedFixture, ScopedSenderConfig));

    const std::vector<std::byte> ScopedBaseline = BuildState(1, 400);
    FBY_CHECK(ScopedFixture.Sender.PublishBaseline(1, ScopedBaseline, 0));

    std::uint64_t ScopedNow = 0;
    for (std::uint32_t ScopedStep = 0; ScopedStep < 40 && ScopedFixture.Receiver.AppliedTick() < 1; ++ScopedStep)
    {
        ScopedNow += 8;
        ScopedFixture.Network.AdvanceTick();
        ScopedFixture.Sender.Tick(ScopedNow);
        ScopedFixture.Receiver.Tick(ScopedNow);
    }
    FBY_CHECK(ScopedFixture.Receiver.AppliedTick() == 1);

    // Complete blackout: every state exhausts its attempts and the sender asks
    // for a fresh baseline.
    ScopedFixture.Network.SetDropEvery(1);
    std::uint64_t ScopedTick = 2;
    for (std::uint32_t ScopedStep = 0; ScopedStep < 120; ++ScopedStep)
    {
        ScopedNow += 8;
        if (ScopedStep % 8 == 0)
        {
            const std::vector<std::byte> ScopedDelta = BuildState(ScopedTick, 200);
            (void)ScopedFixture.Sender.PublishDelta(ScopedTick, 1, ScopedDelta, ScopedNow);
            ++ScopedTick;
        }
        ScopedFixture.Network.AdvanceTick();
        ScopedFixture.Sender.Tick(ScopedNow);
        ScopedFixture.Receiver.Tick(ScopedNow);
    }
    FBY_CHECK(ScopedFixture.Sender.NeedsBaseline());

    // Recovery: a baseline may jump ahead of the expected tick.
    ScopedFixture.Network.SetDropEvery(0);
    const std::vector<std::byte> ScopedRecovery = BuildState(50, 500);
    FBY_CHECK(ScopedFixture.Sender.PublishBaseline(50, ScopedRecovery, ScopedNow));
    for (std::uint32_t ScopedStep = 0; ScopedStep < 200 && ScopedFixture.Receiver.AppliedTick() < 50; ++ScopedStep)
    {
        ScopedNow += 8;
        ScopedFixture.Network.AdvanceTick();
        ScopedFixture.Sender.Tick(ScopedNow);
        ScopedFixture.Receiver.Tick(ScopedNow);
    }
    FBY_CHECK(ScopedFixture.Receiver.AppliedTick() == 50);
    FBY_CHECK(ScopedFixture.Recorder.LastChecksum == ReplicationChecksum(ScopedRecovery));
}

void TestInterpolationAlpha()
{
    using namespace Fby::Net;

    Fixture ScopedFixture;
    SnapshotSenderConfig ScopedSenderConfig{};
    ScopedSenderConfig.RetransmitMs = 8;
    FBY_CHECK(OpenFixture(ScopedFixture, ScopedSenderConfig));

    const std::vector<std::byte> ScopedBaseline = BuildState(1, 200);
    FBY_CHECK(ScopedFixture.Sender.PublishBaseline(1, ScopedBaseline, 0));

    std::uint64_t ScopedNow = 0;
    for (std::uint32_t ScopedStep = 0; ScopedStep < 40 && ScopedFixture.Receiver.AppliedTick() < 1; ++ScopedStep)
    {
        ScopedNow += 8;
        ScopedFixture.Network.AdvanceTick();
        ScopedFixture.Sender.Tick(ScopedNow);
        ScopedFixture.Receiver.Tick(ScopedNow);
    }
    FBY_CHECK(ScopedFixture.Receiver.HasState());
    const std::uint64_t ScopedApplyMs = ScopedFixture.Receiver.LastApplyMs();
    FBY_CHECK(ScopedFixture.Receiver.InterpolationAlpha(ScopedApplyMs) == 0.0f);
    const float ScopedHalf = ScopedFixture.Receiver.InterpolationAlpha(ScopedApplyMs + 4);
    FBY_CHECK(ScopedHalf > 0.49f && ScopedHalf < 0.51f);
    FBY_CHECK(ScopedFixture.Receiver.InterpolationAlpha(ScopedApplyMs + 100) == 1.0f);

    SnapshotReceiver ScopedEmpty;
    FBY_CHECK(ScopedEmpty.InterpolationAlpha(1000) == 0.0f);
}

void TestBudget()
{
    using namespace Fby::Net;

    Fixture ScopedFixture;
    SnapshotSenderConfig ScopedSenderConfig{};
    ScopedSenderConfig.RetransmitMs = 8;
    ScopedSenderConfig.BytesPerSecond = 3000;
    FBY_CHECK(OpenFixture(ScopedFixture, ScopedSenderConfig));

    const std::vector<std::byte> ScopedBaseline = BuildState(1, 100);
    FBY_CHECK(ScopedFixture.Sender.PublishBaseline(1, ScopedBaseline, 0));

    std::uint64_t ScopedNow = 0;
    for (std::uint32_t ScopedStep = 0; ScopedStep < 40 && ScopedFixture.Receiver.AppliedTick() < 1; ++ScopedStep)
    {
        ScopedNow += 8;
        ScopedFixture.Network.AdvanceTick();
        ScopedFixture.Sender.Tick(ScopedNow);
        ScopedFixture.Receiver.Tick(ScopedNow);
    }
    FBY_CHECK(ScopedFixture.Receiver.AppliedTick() == 1);

    // A large delta does not fit the current budget and waits for tokens.
    const std::vector<std::byte> ScopedDelta = BuildState(2, 2000);
    FBY_CHECK(ScopedFixture.Sender.PublishDelta(2, 1, ScopedDelta, ScopedNow));
    FBY_CHECK(ScopedFixture.Sender.Stats().Deferred >= 1);

    for (std::uint32_t ScopedStep = 0; ScopedStep < 400 && ScopedFixture.Receiver.AppliedTick() < 2; ++ScopedStep)
    {
        ScopedNow += 8;
        ScopedFixture.Sender.Tick(ScopedNow);
        ScopedFixture.Receiver.Tick(ScopedNow);
        ScopedFixture.Network.AdvanceTick();
    }
    FBY_CHECK(ScopedFixture.Receiver.AppliedTick() == 2);
    FBY_CHECK(ScopedFixture.Recorder.LastChecksum == ReplicationChecksum(ScopedDelta));
}
}

void TestLargeBaseline()
{
    using namespace Fby::Net;

    Fixture ScopedFixture;
    SnapshotSenderConfig ScopedSenderConfig{};
    ScopedSenderConfig.RetransmitMs = 8;
    FBY_CHECK(OpenFixture(ScopedFixture, ScopedSenderConfig));

    // The PIE budget is 16 KiB: a baseline well above the old 4 KiB cap must
    // still round-trip in one published state.
    const std::vector<std::byte> ScopedBaseline = BuildState(1, 12000);
    FBY_CHECK(ScopedFixture.Sender.PublishBaseline(1, ScopedBaseline, 0));

    std::uint64_t ScopedNow = 0;
    for (std::uint32_t ScopedStep = 0; ScopedStep < 400 && ScopedFixture.Receiver.AppliedTick() < 1; ++ScopedStep)
    {
        ScopedNow += 8;
        ScopedFixture.Network.AdvanceTick();
        ScopedFixture.Sender.Tick(ScopedNow);
        ScopedFixture.Receiver.Tick(ScopedNow);
    }
    FBY_CHECK(ScopedFixture.Receiver.AppliedTick() == 1);
    FBY_CHECK(ScopedFixture.Recorder.LastBytes == 12000);}

int main()
{
    TestLossAndLatency();
    TestBaselineResync();
    TestInterpolationAlpha();
    TestBudget();
    TestLargeBaseline();

    if (GFailures == 0)
    {
        std::printf("net_replication_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "net_replication_test: %u failure(s)\n", GFailures);
    return 1;
}
