// FemboyEngine2D - full-band AEC: delay lock, ERLE, drift and double-talk.
#include "Engine/Audio/VoiceAec.hpp"

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
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

constexpr std::uint32_t kRate = 48000;
constexpr std::uint32_t kBlock = 480;
constexpr std::uint32_t kDelay = 2400;

[[nodiscard]] std::vector<float> Noise(std::uint32_t InFrames, float InAmplitude, std::uint32_t InSeed)
{
    std::vector<float> ScopedSamples(InFrames);
    std::uint32_t ScopedState = InSeed;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedState = ScopedState * 1664525u + 1013904223u;
        const float ScopedUnit = static_cast<float>((ScopedState >> 9) & 0x7FFFFFu) / static_cast<float>(0x7FFFFF);
        ScopedSamples[ScopedIndex] = InAmplitude * (ScopedUnit * 2.0f - 1.0f);
    }
    return ScopedSamples;
}

// Wideband program material: white noise with a gentle one-pole tilt so the
// spectrum has energy up to Nyquist without being a single tone.
[[nodiscard]] std::vector<float> Program(std::uint32_t InFrames, float InAmplitude, std::uint32_t InSeed)
{
    const std::vector<float> ScopedWhite = Noise(InFrames, InAmplitude, InSeed);
    std::vector<float> ScopedSamples(InFrames);
    float ScopedState = 0.0f;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedState += 0.5f * (ScopedWhite[ScopedIndex] - ScopedState);
        ScopedSamples[ScopedIndex] = ScopedState;
    }
    return ScopedSamples;
}

struct EchoResult
{
    float ErleDb = 0.0f;
    std::uint32_t Delay = 0;
    bool Locked = false;
    bool DoubleTalk = false;
    double InputPower = 0.0;
    double OutputPower = 0.0;
};

[[nodiscard]] EchoResult RunEcho(const std::vector<float>& InReference, const std::vector<float>& InMic,
                                 Fby::VoiceAecSettings InSettings, std::uint32_t InFrames)
{
    Fby::VoiceAec ScopedAec;
    ScopedAec.Configure(InSettings, kRate);
    std::vector<float> ScopedMic(InMic.begin(), InMic.end());
    bool ScopedDoubleTalk = false;
    for (std::uint32_t ScopedOffset = 0; ScopedOffset + kBlock <= InFrames; ScopedOffset += kBlock)
    {
        ScopedAec.Process(ScopedMic.data() + ScopedOffset, InReference.data() + ScopedOffset, kBlock);
        ScopedDoubleTalk = ScopedDoubleTalk || ScopedAec.DoubleTalk();
    }

    EchoResult ScopedResult;
    ScopedResult.ErleDb = ScopedAec.ErleDb();
    ScopedResult.Delay = ScopedAec.DelaySamples();
    ScopedResult.Locked = ScopedAec.DelayLocked();
    ScopedResult.DoubleTalk = ScopedDoubleTalk;
    const std::uint32_t ScopedStart = InFrames - kRate;
    for (std::uint32_t ScopedIndex = ScopedStart; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedResult.InputPower += static_cast<double>(InMic[ScopedIndex]) * InMic[ScopedIndex];
        ScopedResult.OutputPower += static_cast<double>(ScopedMic[ScopedIndex]) * ScopedMic[ScopedIndex];
    }
    return ScopedResult;
}

void TestSettings()
{
    using namespace Fby;

    VoiceAecSettings ScopedBad{};
    ScopedBad.TailMs = 500.0f;
    ScopedBad.MaxDelayMs = 300.0f;
    ScopedBad.StepSize = 5.0f;
    FBY_CHECK(ScopedBad.Issue() != nullptr);
    ScopedBad.Clamp();
    FBY_CHECK(ScopedBad.TailMs <= 160.0f);
    FBY_CHECK(ScopedBad.MaxDelayMs <= 120.0f);
    FBY_CHECK(ScopedBad.StepSize <= 1.0f);
    FBY_CHECK(ScopedBad.Issue() == nullptr);

    VoiceAecSettings ScopedDefault{};
    VoiceAec ScopedAec;
    ScopedAec.Configure(ScopedDefault, kRate);
    FBY_CHECK(ScopedAec.Partitions() == 48);
    FBY_CHECK(!ScopedAec.DelayLocked());
}

void TestEchoCancellation()
{
    using namespace Fby;

    const std::uint32_t ScopedFrames = kRate * 4;
    const std::vector<float> ScopedReference = Program(ScopedFrames, 0.35f, 0x5EEDu);
    const std::vector<float> ScopedNoise = Noise(ScopedFrames, 0.01f, 0xBEEFu);
    std::vector<float> ScopedMic(ScopedFrames, 0.0f);
    for (std::uint32_t ScopedIndex = kDelay; ScopedIndex < ScopedFrames; ++ScopedIndex)
    {
        ScopedMic[ScopedIndex] = 0.4f * ScopedReference[ScopedIndex - kDelay] + ScopedNoise[ScopedIndex];
    }

    VoiceAecSettings ScopedSettings{};
    ScopedSettings.TailMs = 32.0f;
    ScopedSettings.MaxDelayMs = 80.0f;
    ScopedSettings.StepSize = 0.5f;
    const EchoResult ScopedResult = RunEcho(ScopedReference, ScopedMic, ScopedSettings, ScopedFrames);

    FBY_CHECK(ScopedResult.Locked);
    const std::int32_t ScopedDelayError =
        static_cast<std::int32_t>(ScopedResult.Delay) - static_cast<std::int32_t>(kDelay);
    FBY_CHECK(ScopedDelayError >= -4 && ScopedDelayError <= 4);
    FBY_CHECK(ScopedResult.ErleDb > 12.0f);
    // The last second must actually lose the echo, not just report it.
    const double ScopedReductionDb =
        10.0 * std::log10((ScopedResult.InputPower + 1e-9) / (ScopedResult.OutputPower + 1e-9));
    std::fprintf(stderr, "aec delay=%u erle=%.2f reduction=%.2f\n", ScopedResult.Delay,
                 static_cast<double>(ScopedResult.ErleDb), ScopedReductionDb);
    FBY_CHECK(ScopedReductionDb > 10.0);
}

// Echo produced by a reference clock that runs faster/slower than the capture
// clock. The echo is a linearly interpolated resampling of the reference.
[[nodiscard]] EchoResult RunDriftEcho(std::int32_t InPpm, std::uint32_t InFrames)
{
    using namespace Fby;
    const std::vector<float> ScopedReference = Program(InFrames, 0.35f, 0x1234u);
    std::vector<float> ScopedMic(InFrames, 0.0f);
    const double ScopedRatio = 1.0 + static_cast<double>(InPpm) * 1.0e-6;
    for (std::uint32_t ScopedIndex = kDelay + 8; ScopedIndex < InFrames; ++ScopedIndex)
    {
        const double ScopedPosition = static_cast<double>(ScopedIndex - kDelay) * ScopedRatio;
        const auto ScopedBase = static_cast<std::uint32_t>(ScopedPosition);
        if (ScopedBase + 1 >= InFrames)
        {
            break;
        }
        const float ScopedFraction = static_cast<float>(ScopedPosition - static_cast<double>(ScopedBase));
        const float ScopedSample = ScopedReference[ScopedBase] * (1.0f - ScopedFraction) +
                                   ScopedReference[ScopedBase + 1] * ScopedFraction;
        ScopedMic[ScopedIndex] = 0.4f * ScopedSample;
    }
    VoiceAecSettings ScopedSettings{};
    ScopedSettings.TailMs = 32.0f;
    ScopedSettings.MaxDelayMs = 80.0f;
    ScopedSettings.StepSize = 0.4f;
    return RunEcho(ScopedReference, ScopedMic, ScopedSettings, InFrames);
}

void TestDrift()
{
    const std::uint32_t ScopedFrames = kRate * 6;
    const EchoResult ScopedPlus = RunDriftEcho(100, ScopedFrames);
    const EchoResult ScopedMinus = RunDriftEcho(-100, ScopedFrames);
    std::fprintf(stderr, "aec drift +100ppm erle=%.2f delay=%u, -100ppm erle=%.2f delay=%u\n",
                 static_cast<double>(ScopedPlus.ErleDb), ScopedPlus.Delay,
                 static_cast<double>(ScopedMinus.ErleDb), ScopedMinus.Delay);
    FBY_CHECK(ScopedPlus.Locked);
    FBY_CHECK(ScopedMinus.Locked);
    FBY_CHECK(ScopedPlus.ErleDb > 8.0f);
    FBY_CHECK(ScopedMinus.ErleDb > 8.0f);
}

void TestDoubleTalk()
{
    using namespace Fby;

    const std::uint32_t ScopedEchoFrames = kRate * 2;
    const std::vector<float> ScopedReference = Noise(ScopedEchoFrames, 0.15f, 0x4321u);
    std::vector<float> ScopedMic(ScopedEchoFrames, 0.0f);
    for (std::uint32_t ScopedIndex = 480; ScopedIndex < ScopedEchoFrames; ++ScopedIndex)
    {
        ScopedMic[ScopedIndex] = 0.4f * ScopedReference[ScopedIndex - 480];
    }

    VoiceAecSettings ScopedSettings{};
    ScopedSettings.TailMs = 32.0f;
    ScopedSettings.MaxDelayMs = 30.0f;
    ScopedSettings.StepSize = 0.5f;
    VoiceAec ScopedAec;
    ScopedAec.Configure(ScopedSettings, kRate);
    for (std::uint32_t ScopedOffset = 0; ScopedOffset + kBlock <= ScopedEchoFrames; ScopedOffset += kBlock)
    {
        ScopedAec.Process(ScopedMic.data() + ScopedOffset, ScopedReference.data() + ScopedOffset, kBlock);
    }
    FBY_CHECK(ScopedAec.ErleDb() > 8.0f);

    // Near-end speech must survive; the update freezes while it dominates.
    const std::uint32_t ScopedTalkFrames = kBlock * 20;
    const std::vector<float> ScopedTalkReference = Noise(ScopedTalkFrames, 0.15f, 0x4321u);
    std::vector<float> ScopedTalkMic(ScopedTalkFrames, 0.0f);
    double ScopedNearPower = 0.0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedTalkFrames; ++ScopedIndex)
    {
        const float ScopedEcho =
            ScopedIndex >= 480 ? 0.4f * ScopedTalkReference[ScopedIndex - 480] : 0.0f;
        const float ScopedNear =
            0.2f * std::sin(2.0f * 3.14159265f * 500.0f * static_cast<float>(ScopedIndex) /
                            static_cast<float>(kRate));
        ScopedTalkMic[ScopedIndex] = ScopedEcho + ScopedNear;
        ScopedNearPower += static_cast<double>(ScopedNear) * ScopedNear;
    }

    bool ScopedSawDoubleTalk = false;
    for (std::uint32_t ScopedOffset = 0; ScopedOffset + kBlock <= ScopedTalkFrames; ScopedOffset += kBlock)
    {
        ScopedAec.Process(ScopedTalkMic.data() + ScopedOffset, ScopedTalkReference.data() + ScopedOffset, kBlock);
        ScopedSawDoubleTalk = ScopedSawDoubleTalk || ScopedAec.DoubleTalk();
    }
    double ScopedOutPower = 0.0;
    for (const float ScopedSample : ScopedTalkMic)
    {
        ScopedOutPower += static_cast<double>(ScopedSample) * ScopedSample;
    }
    std::fprintf(stderr, "aec double-talk saw=%d near=%.4f out=%.4f\n", ScopedSawDoubleTalk ? 1 : 0,
                 ScopedNearPower, ScopedOutPower);
    FBY_CHECK(ScopedSawDoubleTalk);
    FBY_CHECK(ScopedOutPower > ScopedNearPower * 0.1);
    FBY_CHECK(std::isfinite(ScopedAec.ErleDb()));
}

void TestBypass()
{
    using namespace Fby;

    VoiceAecSettings ScopedSettings{};
    VoiceAec ScopedAec;
    ScopedAec.Configure(ScopedSettings, kRate);
    std::vector<float> ScopedMic = {0.1f, 0.2f, 0.3f};
    ScopedAec.Process(ScopedMic.data(), nullptr, static_cast<std::uint32_t>(ScopedMic.size()));
    FBY_CHECK(std::fabs(ScopedMic[2] - 0.3f) < 1e-6f);
}
}

int main()
{
    TestSettings();
    TestEchoCancellation();
    TestDrift();
    TestDoubleTalk();
    TestBypass();

    if (GFailures == 0)
    {
        std::printf("voice_aec_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_aec_test: %u failure(s)\n", GFailures);
    return 1;
}
