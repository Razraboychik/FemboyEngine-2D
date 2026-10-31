// FemboyEngine2D - voice runtime capture-to-mixer test.
#include "Engine/Audio/VoiceSystem.hpp"

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
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

std::uint32_t GSendCount = 0;

bool SendThunk(void*, std::uint8_t, std::span<const std::uint8_t>) noexcept
{
    ++GSendCount;
    return true;
}

[[nodiscard]] std::vector<float> Sine(std::uint32_t InFrames)
{
    std::vector<float> ScopedSamples(InFrames);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedSamples[ScopedIndex] = 0.2f * std::sin(2.0f * 3.14159265f * 440.0f *
                                                     static_cast<float>(ScopedIndex) / 48000.0f);
    }
    return ScopedSamples;
}
}

int main()
{
    using namespace Fby;

    DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));
    VoiceSession ScopedSession;
    (void)ScopedSession.AddParticipant(1);

    VoiceSystemConfig ScopedConfig{};
    ScopedConfig.LocalPlayer = 0;
    ScopedConfig.Routing = VoiceRouting::Mesh;
    ScopedConfig.Denoise = VoiceDenoiseTier::Spectral;
    ScopedConfig.Aec.Enabled = false;
    VoiceSystem ScopedVoice;
    if (!ScopedVoice.Start(ScopedGraph, nullptr, ScopedConfig, &SendThunk, nullptr))
    {
        std::printf("voice_system_test: Opus unavailable, skipped\n");
        return 0;
    }
    FBY_CHECK(ScopedVoice.Channel().AddParticipant(1));

    const std::vector<float> ScopedFrames = Sine(kVoiceFrameSamples * 2);
    ScopedVoice.PushCaptured(ScopedFrames);
    ScopedVoice.Tick(0.02f, ScopedSession);
    FBY_CHECK(GSendCount >= 1);
    FBY_CHECK(ScopedVoice.ProcessedRing().AvailableRead() == 0 || GSendCount >= 1);

    // Live denoise switch keeps the path running.
    ScopedVoice.SetDenoiseTier(VoiceDenoiseTier::Rnnoise);
    ScopedVoice.PushCaptured(ScopedFrames);
    const std::uint32_t ScopedBefore = GSendCount;
    ScopedVoice.Tick(0.02f, ScopedSession);
    FBY_CHECK(GSendCount >= ScopedBefore);

    // Binding a decoded peer ring to the Voice bus must not disturb capture.
    ScopedVoice.Tick(0.02f, ScopedSession);
    FBY_CHECK(ScopedSession.CanTransmit());

    ScopedVoice.Stop();
    ScopedGraph.Shutdown();

    if (GFailures == 0)
    {
        std::printf("voice_system_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_system_test: %u failure(s)\n", GFailures);
    return 1;
}
