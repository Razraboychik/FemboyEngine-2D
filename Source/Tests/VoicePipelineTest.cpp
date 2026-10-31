// FemboyEngine2D - stream ring, live stream voices and voice-session policy tests.
#include "Engine/Audio/AudioDevices.hpp"
#include "Engine/Audio/AudioStream.hpp"
#include "Engine/Audio/DspGraph.hpp"
#include "Engine/Audio/VoiceSession.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>

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

void TestFloatRing()
{
    float ScopedStorage[64]{};
    Fby::FloatRing ScopedRing;
    ScopedRing.Configure(ScopedStorage, 64, 1);
    FBY_CHECK(ScopedRing.CapacityFrames() == 64);
    FBY_CHECK(ScopedRing.AvailableRead() == 0);
    FBY_CHECK(ScopedRing.AvailableWrite() == 63);

    std::array<float, 40> ScopedInput{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedInput.size(); ++ScopedIndex)
    {
        ScopedInput[ScopedIndex] = static_cast<float>(ScopedIndex);
    }
    FBY_CHECK(ScopedRing.Write(ScopedInput.data(), static_cast<std::uint32_t>(ScopedInput.size())) == 40);
    FBY_CHECK(ScopedRing.AvailableRead() == 40);

    std::array<float, 100> ScopedMore{};
    FBY_CHECK(ScopedRing.Write(ScopedMore.data(), 100) == 23);
    FBY_CHECK(ScopedRing.DroppedFrames() == 77);
    FBY_CHECK(ScopedRing.AvailableRead() == 63);

    std::array<float, 10> ScopedRead{};
    FBY_CHECK(ScopedRing.Read(ScopedRead.data(), 10) == 10);
    FBY_CHECK(std::fabs(ScopedRead[0] - 0.0f) < 1e-6f);
    FBY_CHECK(std::fabs(ScopedRead[9] - 9.0f) < 1e-6f);
    FBY_CHECK(ScopedRing.AvailableRead() == 53);

    std::array<float, 100> ScopedUnderrun{};
    FBY_CHECK(ScopedRing.Read(ScopedUnderrun.data(), 100) == 53);
    FBY_CHECK(ScopedRing.UnderrunFrames() == 47);
    FBY_CHECK(ScopedRing.AvailableRead() == 0);
}

void TestStreamVoice()
{
    using namespace Fby;

    DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));

    float ScopedStorage[1024]{};
    FloatRing ScopedRing;
    ScopedRing.Configure(ScopedStorage, 1024, 1);

    std::array<float, 256> ScopedFrames{};
    ScopedFrames.fill(0.25f);
    FBY_CHECK(ScopedRing.Write(ScopedFrames.data(), 256) == 256);

    std::uint32_t ScopedVoice = kInvalidVoice;
    FBY_CHECK(ScopedGraph.PlayStream(&ScopedRing, kSfxBus, 1.0f, 0.0f, &ScopedVoice) == DspResult::Ok);

    std::array<float, 512> ScopedOutput{};
    ScopedGraph.ProcessOffline(ScopedOutput.data(), 256);
    FBY_CHECK(std::fabs(ScopedOutput[0]) > 0.1f);
    FBY_CHECK(std::fabs(ScopedOutput[0] - ScopedOutput[1]) < 1e-4f);

    FBY_CHECK(ScopedGraph.StopVoice(ScopedVoice) == DspResult::Ok);
    std::array<float, 512> ScopedSilence{};
    ScopedGraph.ProcessOffline(ScopedSilence.data(), 256);
    FBY_CHECK(std::fabs(ScopedSilence[0]) < 1e-4f);

    // Pan: a hard-left voice leaves the right channel quiet.
    std::array<float, 256> ScopedPanFrames{};
    ScopedPanFrames.fill(0.25f);
    (void)ScopedRing.Write(ScopedPanFrames.data(), 256);
    FBY_CHECK(ScopedGraph.PlayStream(&ScopedRing, kSfxBus, 1.0f, -1.0f, &ScopedVoice) == DspResult::Ok);
    std::array<float, 512> ScopedPanOutput{};
    ScopedGraph.ProcessOffline(ScopedPanOutput.data(), 256);
    FBY_CHECK(std::fabs(ScopedPanOutput[0]) > 0.1f);
    FBY_CHECK(std::fabs(ScopedPanOutput[1]) < 1e-3f);
    (void)ScopedGraph.StopVoice(ScopedVoice);

    // An empty ring is silence plus an underrun, not a crash.
    ScopedRing.ResetCounters();
    FBY_CHECK(ScopedGraph.PlayStream(&ScopedRing, kSfxBus, 1.0f, 0.0f, &ScopedVoice) == DspResult::Ok);
    std::array<float, 512> ScopedEmpty{};
    ScopedGraph.ProcessOffline(ScopedEmpty.data(), 256);
    FBY_CHECK(std::fabs(ScopedEmpty[0]) < 1e-5f);
    FBY_CHECK(ScopedRing.UnderrunFrames() >= 256);
    (void)ScopedGraph.StopVoice(ScopedVoice);
    FBY_CHECK(ScopedGraph.PlayStream(nullptr, kSfxBus, 1.0f, 0.0f, nullptr) == DspResult::InvalidArgument);

    ScopedGraph.Shutdown();
}

void TestVoiceSession()
{
    using namespace Fby;

    VoiceSession ScopedSession;
    FBY_CHECK(ScopedSession.AddParticipant(0));
    FBY_CHECK(ScopedSession.AddParticipant(1));
    FBY_CHECK(!ScopedSession.AddParticipant(0));
    FBY_CHECK(ScopedSession.Count() == 2);
    FBY_CHECK(ScopedSession.CanTransmit());

    ScopedSession.SetLocalMuted(true);
    FBY_CHECK(!ScopedSession.CanTransmit());
    ScopedSession.SetLocalMuted(false);

    ScopedSession.SetPushToTalkEnabled(true);
    FBY_CHECK(!ScopedSession.CanTransmit());
    ScopedSession.SetPushToTalkHeld(true);
    FBY_CHECK(ScopedSession.CanTransmit());
    ScopedSession.SetPushToTalkHeld(false);
    ScopedSession.SetPushToTalkEnabled(false);
    FBY_CHECK(ScopedSession.CanTransmit());

    ScopedSession.SetDeafened(true);
    FBY_CHECK(!ScopedSession.CanTransmit());
    FBY_CHECK(ScopedSession.ParticipantGain(0) == 0.0f);
    ScopedSession.SetDeafened(false);

    ScopedSession.SetParticipantVolume(0, 0.5f);
    FBY_CHECK(std::fabs(ScopedSession.ParticipantGain(0) - 0.5f) < 1e-5f);
    ScopedSession.SetParticipantMuted(0, true);
    FBY_CHECK(ScopedSession.ParticipantGain(0) == 0.0f);
    ScopedSession.SetParticipantMuted(0, false);

    // Talking with a hold tail.
    ScopedSession.SetParticipantLevel(1, 0.1f);
    ScopedSession.Update(1.0f / 60.0f);
    FBY_CHECK(ScopedSession.Participant(1) != nullptr && ScopedSession.Participant(1)->Talking);
    ScopedSession.SetParticipantLevel(1, 0.0f);
    ScopedSession.Update(0.1f);
    FBY_CHECK(ScopedSession.Participant(1)->Talking);
    ScopedSession.Update(0.5f);
    FBY_CHECK(!ScopedSession.Participant(1)->Talking);

    ScopedSession.SetLocalLevel(0.1f);
    ScopedSession.Update(1.0f / 60.0f);
    FBY_CHECK(ScopedSession.LocalTalking());
    ScopedSession.SetLocalLevel(0.0f);
    ScopedSession.Update(1.0f);
    FBY_CHECK(!ScopedSession.LocalTalking());

    ScopedSession.SetMono(true);
    FBY_CHECK(ScopedSession.Mono());

    ScopedSession.RemoveParticipant(0);
    FBY_CHECK(ScopedSession.Count() == 1);
    FBY_CHECK(ScopedSession.Participant(0) == nullptr);
}

void TestCapture()
{
    using namespace Fby;

    AudioDeviceEnumerator ScopedDevices;
    std::string ScopedError;
    if (!ScopedDevices.Initialize(&ScopedError) || ScopedDevices.Devices(AudioDeviceKind::Input).Count == 0)
    {
        std::printf("voice_pipeline_test: no input device, capture skipped\n");
        return;
    }

    std::array<float, 4800> ScopedStorage{};
    FloatRing ScopedRing;
    ScopedRing.Configure(ScopedStorage.data(), static_cast<std::uint32_t>(ScopedStorage.size()), 1);

    VoiceCapture ScopedCapture;
    if (!ScopedCapture.Start(ScopedDevices, ScopedDevices.Devices(AudioDeviceKind::Input).Devices[0].Id, &ScopedRing,
                             48000, &ScopedError))
    {
        std::printf("voice_pipeline_test: capture unavailable (%s)\n", ScopedError.c_str());
        return;
    }
    FBY_CHECK(ScopedCapture.Active());
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    std::array<float, 480> ScopedRead{};
    (void)ScopedRing.Read(ScopedRead.data(), 480);
    ScopedCapture.Stop();
    FBY_CHECK(!ScopedCapture.Active());
}
}

int main()
{
    TestFloatRing();
    TestStreamVoice();
    TestVoiceSession();
    TestCapture();

    if (GFailures == 0)
    {
        std::printf("voice_pipeline_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_pipeline_test: %u failure(s)\n", GFailures);
    return 1;
}
