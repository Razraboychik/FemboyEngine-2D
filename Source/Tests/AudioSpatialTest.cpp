// FemboyEngine2D - spatial audio tests: live gain and stereo pan.
#include "Engine/Audio/Audio.hpp"
#include "Engine/Audio/DspGraph.hpp"

#include <cmath>
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

[[nodiscard]] std::vector<float> MakeMonoClip(float InValue, std::uint32_t InFrames)
{
    return std::vector<float>(InFrames, InValue);
}

[[nodiscard]] Fby::DspClipView ClipView(const std::vector<float>& InSamples, std::uint32_t InChannels) noexcept
{
    Fby::DspClipView ScopedView;
    ScopedView.samples = reinterpret_cast<const std::byte*>(InSamples.data());
    ScopedView.frameCount = static_cast<std::uint32_t>(InSamples.size() / InChannels);
    ScopedView.channels = InChannels;
    ScopedView.sampleRate = 48000;
    ScopedView.format = Fby::Assets::PcmFormat::PcmF32;
    return ScopedView;
}

void TestLiveGainAndPan()
{
    Fby::DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));

    const std::vector<float> ScopedSamples = MakeMonoClip(1.0f, 512);
    const Fby::DspClipView ScopedClip = ClipView(ScopedSamples, 1);
    std::uint32_t ScopedVoice = Fby::kInvalidVoice;
    FBY_CHECK(ScopedGraph.PlayOneShot(ScopedClip, Fby::kSfxBus, 1.0f, 1.0f, true, &ScopedVoice) ==
              Fby::DspResult::Ok);
    FBY_CHECK(ScopedVoice != Fby::kInvalidVoice);

    std::vector<float> ScopedOut(64 * 2);
    ScopedGraph.ProcessOffline(ScopedOut.data(), 64);
    FBY_CHECK(std::fabs(ScopedOut[0] - 1.0f) < 0.01f);
    FBY_CHECK(std::fabs(ScopedOut[1] - 1.0f) < 0.01f);

    // Hard right: the left channel ramps to silence over one block.
    FBY_CHECK(ScopedGraph.SetVoiceSpatial(ScopedVoice, 1.0f, 1.0f) == Fby::DspResult::Ok);
    ScopedGraph.ProcessOffline(ScopedOut.data(), 64);
    const float ScopedLeftTail = ScopedOut[63 * 2 + 0];
    const float ScopedRightTail = ScopedOut[63 * 2 + 1];
    FBY_CHECK(std::fabs(ScopedLeftTail) < 0.01f);
    FBY_CHECK(std::fabs(ScopedRightTail - 1.0f) < 0.01f);

    // Half volume, centred: both channels reach 0.5 after the ramp.
    FBY_CHECK(ScopedGraph.SetVoiceSpatial(ScopedVoice, 0.5f, 0.0f) == Fby::DspResult::Ok);
    ScopedGraph.ProcessOffline(ScopedOut.data(), 64);
    ScopedGraph.ProcessOffline(ScopedOut.data(), 64);
    FBY_CHECK(std::fabs(ScopedOut[63 * 2 + 0] - 0.5f) < 0.01f);
    FBY_CHECK(std::fabs(ScopedOut[63 * 2 + 1] - 0.5f) < 0.01f);

    // Hard left: the right channel ramps down.
    FBY_CHECK(ScopedGraph.SetVoiceSpatial(ScopedVoice, 1.0f, -1.0f) == Fby::DspResult::Ok);
    ScopedGraph.ProcessOffline(ScopedOut.data(), 64);
    FBY_CHECK(std::fabs(ScopedOut[63 * 2 + 0] - 1.0f) < 0.01f);
    FBY_CHECK(std::fabs(ScopedOut[63 * 2 + 1]) < 0.01f);

    FBY_CHECK(ScopedGraph.SetVoiceSpatial(9999, 1.0f, 0.0f) == Fby::DspResult::InvalidArgument);
    FBY_CHECK(ScopedGraph.SetVoiceSpatial(ScopedVoice + 1, 1.0f, 0.0f) == Fby::DspResult::NotFound);

    FBY_CHECK(ScopedGraph.StopVoice(ScopedVoice) == Fby::DspResult::Ok);
    ScopedGraph.ProcessOffline(ScopedOut.data(), 64);
    FBY_CHECK(std::fabs(ScopedOut[0]) < 0.01f);
    FBY_CHECK(std::fabs(ScopedOut[1]) < 0.01f);
    FBY_CHECK(ScopedGraph.SetVoiceSpatial(ScopedVoice, 1.0f, 0.0f) == Fby::DspResult::NotFound);
}

void TestAudioSystemValidation()
{
    Fby::AudioSystem ScopedSystem;
    // Not initialized: a spatial update must fail cleanly instead of touching a
    // voice table that does not exist yet.
    FBY_CHECK(!ScopedSystem.SetVoiceSpatial(0, 1.0f, 0.0f));
    FBY_CHECK(!ScopedSystem.SetVoiceSpatial(Fby::kInvalidAudioVoice, 1.0f, 0.0f));
}
}

int main()
{
    TestLiveGainAndPan();
    TestAudioSystemValidation();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u spatial audio test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("spatial audio tests passed\n");
    return 0;
}
