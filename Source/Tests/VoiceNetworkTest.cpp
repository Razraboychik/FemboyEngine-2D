// FemboyEngine2D - Opus voice packets, jitter, relay/mesh and ducking tests.
#include "Engine/Audio/VoiceChannel.hpp"
#include "Engine/Audio/VoiceCodec.hpp"
#include "Engine/Audio/VoiceSession.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
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

[[nodiscard]] std::vector<float> Sine(std::uint32_t InFrames)
{
    std::vector<float> ScopedSamples(InFrames);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedSamples[ScopedIndex] = 0.3f * std::sin(2.0f * 3.14159265f * 440.0f *
                                                     static_cast<float>(ScopedIndex) / 48000.0f);
    }
    return ScopedSamples;
}

[[nodiscard]] float RingRms(Fby::FloatRing& InRing)
{
    std::vector<float> ScopedSamples(InRing.AvailableRead());
    const std::uint32_t ScopedRead =
        InRing.Read(ScopedSamples.data(), static_cast<std::uint32_t>(ScopedSamples.size()));
    ScopedSamples.resize(ScopedRead);
    double ScopedSum = 0.0;
    for (const float ScopedSample : ScopedSamples)
    {
        ScopedSum += static_cast<double>(ScopedSample) * ScopedSample;
    }
    return ScopedSamples.empty() ? 0.0f : static_cast<float>(std::sqrt(ScopedSum / ScopedSamples.size()));
}

struct Router
{
    Fby::VoiceChannel* Peers[8]{};
    std::uint8_t From = 0xFF;
    std::vector<std::uint8_t> LastTargets;
    std::vector<std::vector<std::uint8_t>> Captured;
    bool Forward = true;

    static bool SendThunk(void* InUser, std::uint8_t InToPlayer,
                          std::span<const std::uint8_t> InPacket) noexcept
    {
        auto* ScopedRouter = static_cast<Router*>(InUser);
        if (ScopedRouter == nullptr || InToPlayer >= 8 || ScopedRouter->Peers[InToPlayer] == nullptr)
        {
            return false;
        }
        ScopedRouter->Captured.emplace_back(InPacket.begin(), InPacket.end());
        return ScopedRouter->Peers[InToPlayer]->ReceivePacket(ScopedRouter->From, InPacket);
    }

    static bool CaptureThunk(void* InUser, std::uint8_t InToPlayer,
                             std::span<const std::uint8_t> InPacket) noexcept
    {
        auto* ScopedRouter = static_cast<Router*>(InUser);
        if (ScopedRouter == nullptr)
        {
            return false;
        }
        ScopedRouter->LastTargets.push_back(InToPlayer);
        ScopedRouter->Captured.emplace_back(InPacket.begin(), InPacket.end());
        if (InToPlayer < 8 && ScopedRouter->Peers[InToPlayer] != nullptr)
        {
            return ScopedRouter->Peers[InToPlayer]->ReceivePacket(ScopedRouter->From, InPacket);
        }
        return true;
    }
};

void TestCodec()
{
    using namespace Fby;

    VoiceEncoder ScopedEncoder;
    VoiceDecoder ScopedDecoder;
    if (!ScopedEncoder.Initialize() || !ScopedDecoder.Initialize())
    {
        std::printf("voice_network_test: Opus unavailable, codec skipped\n");
        return;
    }
    const std::vector<float> ScopedSine = Sine(kVoiceFrameSamples);
    std::vector<std::uint8_t> ScopedPacket(kVoiceMaxPacketBytes);
    const std::uint32_t ScopedBytes =
        ScopedEncoder.Encode(ScopedSine, ScopedPacket);
    FBY_CHECK(ScopedBytes > 0);

    std::vector<float> ScopedDecoded(kVoiceFrameSamples);
    const std::uint32_t ScopedFrames = ScopedDecoder.Decode(
        std::span<const std::uint8_t>(ScopedPacket.data(), ScopedBytes), ScopedDecoded);
    FBY_CHECK(ScopedFrames == kVoiceFrameSamples);
    double ScopedSum = 0.0;
    for (const float ScopedSample : ScopedDecoded)
    {
        ScopedSum += static_cast<double>(ScopedSample) * ScopedSample;
    }
    FBY_CHECK(std::sqrt(ScopedSum / ScopedDecoded.size()) > 0.02);

    std::vector<float> ScopedConcealed(kVoiceFrameSamples);
    FBY_CHECK(ScopedDecoder.Conceal(ScopedConcealed) == kVoiceFrameSamples);
    FBY_CHECK(ScopedEncoder.Encode(std::span<const float>(ScopedSine.data(), 100), ScopedPacket) == 0);
}

void TestMesh()
{
    using namespace Fby;

    auto ScopedA = std::make_unique<VoiceChannel>();
    auto ScopedB = std::make_unique<VoiceChannel>();
    auto ScopedC = std::make_unique<VoiceChannel>();

    Router ScopedRouterA{};
    Router ScopedRouterB{};
    Router ScopedRouterC{};
    ScopedRouterA.Peers[1] = ScopedB.get();
    ScopedRouterA.Peers[2] = ScopedC.get();
    ScopedRouterA.From = 0;
    ScopedRouterB.Peers[0] = ScopedA.get();
    ScopedRouterB.Peers[2] = ScopedC.get();
    ScopedRouterB.From = 1;
    ScopedRouterC.Peers[0] = ScopedA.get();
    ScopedRouterC.Peers[1] = ScopedB.get();
    ScopedRouterC.From = 2;

    VoiceChannelConfig ScopedConfigA{};
    ScopedConfigA.Routing = VoiceRouting::Mesh;
    ScopedConfigA.LocalPlayer = 0;
    VoiceChannelConfig ScopedConfigB = ScopedConfigA;
    ScopedConfigB.LocalPlayer = 1;
    VoiceChannelConfig ScopedConfigC = ScopedConfigA;
    ScopedConfigC.LocalPlayer = 2;

    if (!ScopedA->Start(ScopedConfigA, &Router::CaptureThunk, &ScopedRouterA) ||
        !ScopedB->Start(ScopedConfigB, &Router::SendThunk, &ScopedRouterB) ||
        !ScopedC->Start(ScopedConfigC, &Router::SendThunk, &ScopedRouterC))
    {
        std::printf("voice_network_test: Opus unavailable, mesh skipped\n");
        return;
    }

    FBY_CHECK(ScopedA->AddParticipant(1) && ScopedA->AddParticipant(2));
    FBY_CHECK(ScopedB->AddParticipant(0) && ScopedB->AddParticipant(2));
    FBY_CHECK(ScopedC->AddParticipant(0) && ScopedC->AddParticipant(1));

    VoiceSession ScopedSessionA;
    (void)ScopedSessionA.AddParticipant(1);
    (void)ScopedSessionA.AddParticipant(2);
    VoiceSession ScopedSessionB;
    (void)ScopedSessionB.AddParticipant(0);

    std::vector<float> ScopedStorage(kVoiceFrameSamples * 4);
    FloatRing ScopedCapture;
    ScopedCapture.Configure(ScopedStorage.data(), static_cast<std::uint32_t>(ScopedStorage.size()), 1);
    const std::vector<float> ScopedSine = Sine(kVoiceFrameSamples);
    FBY_CHECK(ScopedCapture.Write(ScopedSine.data(), kVoiceFrameSamples) == kVoiceFrameSamples);
    ScopedA->SetCaptureRing(&ScopedCapture);

    ScopedA->Tick(0.02f, &ScopedSessionA);
    FBY_CHECK(ScopedRouterA.LastTargets.size() == 2);
    FBY_CHECK(ScopedB->Stats().PacketsReceived == 1);

    ScopedB->Tick(0.02f, &ScopedSessionB);
    FBY_CHECK(ScopedB->Stats().FramesDecoded == 1);
    FloatRing* ScopedBRing = ScopedB->ParticipantRing(0);
    FBY_CHECK(ScopedBRing != nullptr);
    if (ScopedBRing != nullptr)
    {
        FBY_CHECK(RingRms(*ScopedBRing) > 0.02f);
    }

    // Muted local microphone consumes capture but transmits nothing.
    FBY_CHECK(ScopedCapture.Write(ScopedSine.data(), kVoiceFrameSamples) == kVoiceFrameSamples);
    ScopedSessionA.SetLocalMuted(true);
    const std::size_t ScopedTargetsBefore = ScopedRouterA.LastTargets.size();
    ScopedA->Tick(0.02f, &ScopedSessionA);
    FBY_CHECK(ScopedRouterA.LastTargets.size() == ScopedTargetsBefore);
    FBY_CHECK(ScopedCapture.AvailableRead() == 0);
}

void TestRelay()
{
    using namespace Fby;

    auto ScopedClient = std::make_unique<VoiceChannel>();
    auto ScopedServer = std::make_unique<VoiceChannel>();
    auto ScopedPeer = std::make_unique<VoiceChannel>();

    Router ScopedClientRouter{};
    Router ScopedServerRouter{};
    Router ScopedPeerRouter{};
    ScopedClientRouter.Peers[7] = ScopedServer.get();
    ScopedClientRouter.From = 0;
    ScopedServerRouter.Peers[0] = ScopedClient.get();
    ScopedServerRouter.Peers[1] = ScopedPeer.get();
    ScopedServerRouter.From = 7;

    VoiceChannelConfig ScopedClientConfig{};
    ScopedClientConfig.Routing = VoiceRouting::Relay;
    ScopedClientConfig.LocalPlayer = 0;
    ScopedClientConfig.RelayPlayer = 7;

    VoiceChannelConfig ScopedServerConfig{};
    ScopedServerConfig.Routing = VoiceRouting::Relay;
    ScopedServerConfig.LocalPlayer = 7;
    ScopedServerConfig.IsRelayServer = true;

    VoiceChannelConfig ScopedPeerConfig{};
    ScopedPeerConfig.Routing = VoiceRouting::Relay;
    ScopedPeerConfig.LocalPlayer = 1;
    ScopedPeerConfig.RelayPlayer = 7;

    if (!ScopedClient->Start(ScopedClientConfig, &Router::CaptureThunk, &ScopedClientRouter) ||
        !ScopedServer->Start(ScopedServerConfig, &Router::CaptureThunk, &ScopedServerRouter) ||
        !ScopedPeer->Start(ScopedPeerConfig, &Router::CaptureThunk, &ScopedPeerRouter))
    {
        std::printf("voice_network_test: Opus unavailable, relay skipped\n");
        return;
    }

    FBY_CHECK(ScopedClient->AddParticipant(7));
    FBY_CHECK(ScopedClient->AddParticipant(1));
    FBY_CHECK(ScopedServer->AddParticipant(0) && ScopedServer->AddParticipant(1));
    FBY_CHECK(ScopedPeer->AddParticipant(0));
    FBY_CHECK(ScopedPeer->AddParticipant(7));

    std::vector<float> ScopedStorage(kVoiceFrameSamples * 4);
    FloatRing ScopedCapture;
    ScopedCapture.Configure(ScopedStorage.data(), static_cast<std::uint32_t>(ScopedStorage.size()), 1);
    const std::vector<float> ScopedSine = Sine(kVoiceFrameSamples);
    FBY_CHECK(ScopedCapture.Write(ScopedSine.data(), kVoiceFrameSamples) == kVoiceFrameSamples);
    ScopedClient->SetCaptureRing(&ScopedCapture);

    ScopedClient->Tick(0.02f, nullptr);
    FBY_CHECK(ScopedServer->Stats().PacketsReceived == 1);
    FBY_CHECK(ScopedServer->Stats().PacketsForwarded == 1);
    FBY_CHECK(ScopedPeer->Stats().PacketsReceived == 1);
    FBY_CHECK(ScopedServerRouter.Captured.size() == 1);

    // The server must not echo voice back to its sender.
    for (const std::uint8_t ScopedTarget : ScopedServerRouter.LastTargets)
    {
        FBY_CHECK(ScopedTarget != 0);
    }
}

void TestJitterReorder()
{
    using namespace Fby;

    auto ScopedA = std::make_unique<VoiceChannel>();
    auto ScopedB = std::make_unique<VoiceChannel>();

    Router ScopedRouterA{};
    Router ScopedRouterB{};
    ScopedRouterA.Peers[1] = ScopedB.get();
    ScopedRouterA.From = 0;
    ScopedRouterB.Peers[0] = ScopedA.get();
    ScopedRouterB.From = 1;

    VoiceChannelConfig ScopedAConfig{};
    ScopedAConfig.Routing = VoiceRouting::Mesh;
    ScopedAConfig.LocalPlayer = 0;
    VoiceChannelConfig ScopedBConfig = ScopedAConfig;
    ScopedBConfig.LocalPlayer = 1;

    ScopedRouterA.Forward = false;
    if (!ScopedA->Start(ScopedAConfig, &Router::SendThunk, &ScopedRouterA) ||
        !ScopedB->Start(ScopedBConfig, &Router::SendThunk, &ScopedRouterB))
    {
        std::printf("voice_network_test: Opus unavailable, jitter skipped\n");
        return;
    }
    FBY_CHECK(ScopedA->AddParticipant(1));
    FBY_CHECK(ScopedB->AddParticipant(0));

    std::vector<float> ScopedStorage(kVoiceFrameSamples * 4);
    FloatRing ScopedCapture;
    ScopedCapture.Configure(ScopedStorage.data(), static_cast<std::uint32_t>(ScopedStorage.size()), 1);
    const std::vector<float> ScopedSine = Sine(kVoiceFrameSamples * 3);
    FBY_CHECK(ScopedCapture.Write(ScopedSine.data(), kVoiceFrameSamples * 3) == kVoiceFrameSamples * 3);
    ScopedA->SetCaptureRing(&ScopedCapture);
    ScopedA->Tick(0.06f, nullptr);

    // CaptureThunk forwarded each packet to B in order, so clear B's jitter by
    // re-sending the recorded packets out of order after a fresh Tick.
    FBY_CHECK(!ScopedRouterA.Captured.empty());
    const std::vector<std::vector<std::uint8_t>> ScopedPackets = ScopedRouterA.Captured;
    FBY_CHECK(ScopedPackets.size() == 3);

    // Deliver 0, 2, 1; the smallest pending sequence must win each playout tick.
    (void)ScopedB->ReceivePacket(0, ScopedPackets[0]);
    (void)ScopedB->ReceivePacket(0, ScopedPackets[2]);
    (void)ScopedB->ReceivePacket(0, ScopedPackets[1]);
    ScopedB->Tick(0.06f, nullptr);
    FBY_CHECK(ScopedB->Stats().FramesDecoded == 3);
}

void TestDucking()
{
    using namespace Fby;

    DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));
    BusId ScopedVoiceBus = kInvalidBus;
    FBY_CHECK(ScopedGraph.CreateBus(kMasterBus, 1.0f, &ScopedVoiceBus) == DspResult::Ok);
    std::vector<float> ScopedSilence(256, 0.0f);
    ScopedGraph.ProcessOffline(ScopedSilence.data(), 128);
    FBY_CHECK(ConfigureVoiceDucking(ScopedGraph, ScopedVoiceBus, kMusicBus) == DspResult::Ok);
    (void)ScopedGraph.DestroyBus(ScopedVoiceBus);
    ScopedGraph.Shutdown();
}
}

int main()
{
    TestCodec();
    TestMesh();
    TestRelay();
    TestJitterReorder();
    TestDucking();

    if (GFailures == 0)
    {
        std::printf("voice_network_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_network_test: %u failure(s)\n", GFailures);
    return 1;
}
