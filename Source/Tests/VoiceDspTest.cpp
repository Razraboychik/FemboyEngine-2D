// FemboyEngine2D - microphone chain stage and settings/preset tests.
#include "Engine/Audio/VoiceDsp.hpp"

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

constexpr std::uint32_t kRate = 48000;

[[nodiscard]] std::vector<float> Sine(float InAmplitude, float InFrequency, std::uint32_t InFrames)
{
    std::vector<float> ScopedSamples(InFrames);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedSamples[ScopedIndex] = InAmplitude * std::sin(2.0f * 3.14159265f * InFrequency *
                                                            static_cast<float>(ScopedIndex) / static_cast<float>(kRate));
    }
    return ScopedSamples;
}

[[nodiscard]] std::vector<float> Noise(float InAmplitude, std::uint32_t InFrames)
{
    std::vector<float> ScopedSamples(InFrames);
    std::uint32_t ScopedState = 0x12345678u;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedState = ScopedState * 1664525u + 1013904223u;
        const float ScopedUnit = static_cast<float>((ScopedState >> 9) & 0x7FFFFFu) / static_cast<float>(0x7FFFFF);
        ScopedSamples[ScopedIndex] = InAmplitude * (ScopedUnit * 2.0f - 1.0f);
    }
    return ScopedSamples;
}

[[nodiscard]] float Rms(const std::vector<float>& InSamples)
{
    double ScopedSum = 0.0;
    for (const float ScopedSample : InSamples)
    {
        ScopedSum += static_cast<double>(ScopedSample) * ScopedSample;
    }
    return InSamples.empty() ? 0.0f : static_cast<float>(std::sqrt(ScopedSum / InSamples.size()));
}

[[nodiscard]] bool AllFinite(const std::vector<float>& InSamples)
{
    for (const float ScopedSample : InSamples)
    {
        if (!std::isfinite(ScopedSample))
        {
            return false;
        }
    }
    return true;
}

void TestSettingsAndPresets()
{
    using namespace Fby;

    VoiceDspSettings ScopedBad{};
    ScopedBad.GateThresholdDb = -200.0f;
    ScopedBad.AgcTargetDbfs = 5.0f;
    ScopedBad.HighPassHz = 5.0f;
    ScopedBad.DenoiseStrength = 9.0f;
    FBY_CHECK(ScopedBad.Issue() != nullptr);
    ScopedBad.Clamp();
    FBY_CHECK(ScopedBad.GateThresholdDb >= -90.0f);
    FBY_CHECK(ScopedBad.AgcTargetDbfs <= -3.0f);
    FBY_CHECK(ScopedBad.HighPassHz >= 20.0f);
    FBY_CHECK(ScopedBad.DenoiseStrength <= 2.0f);
    FBY_CHECK(ScopedBad.Issue() == nullptr);

    for (std::uint8_t ScopedPreset = 0; ScopedPreset < static_cast<std::uint8_t>(VoicePresetKind::Count);
         ++ScopedPreset)
    {
        const VoiceDspSettings ScopedSettings = VoicePresetSettings(static_cast<VoicePresetKind>(ScopedPreset));
        FBY_CHECK(ScopedSettings.Issue() == nullptr);
    }
    FBY_CHECK(!(VoicePresetSettings(VoicePresetKind::Headset) == VoicePresetSettings(VoicePresetKind::NoisyRoom)));
    FBY_CHECK(VoicePresetSettings(VoicePresetKind::LowBandwidth).Denoise == VoiceDenoiseTier::Gate);
}

void TestDcAndHighPass()
{
    using namespace Fby;

    VoiceDspSettings ScopedSettings{};
    ScopedSettings.Denoise = VoiceDenoiseTier::None;
    ScopedSettings.AgcEnabled = false;
    VoiceDspChain ScopedChain;
    ScopedChain.Configure(ScopedSettings, kRate);

    std::vector<float> ScopedInput(4000, 0.5f);
    ScopedChain.Process(ScopedInput.data(), static_cast<std::uint32_t>(ScopedInput.size()));
    FBY_CHECK(std::fabs(ScopedInput.back()) < 0.005f);
}

void TestGate()
{
    using namespace Fby;

    VoiceDspSettings ScopedSettings{};
    ScopedSettings.Denoise = VoiceDenoiseTier::Gate;
    ScopedSettings.GateThresholdDb = -30.0f;
    ScopedSettings.AgcEnabled = false;
    VoiceDspChain ScopedChain;
    ScopedChain.Configure(ScopedSettings, kRate);

    std::vector<float> ScopedSilence(1000, 0.0f);
    ScopedChain.Process(ScopedSilence.data(), static_cast<std::uint32_t>(ScopedSilence.size()));
    FBY_CHECK(!ScopedChain.GateOpen());

    std::vector<float> ScopedVoice = Sine(0.3f, 440.0f, 2000);
    ScopedChain.Process(ScopedVoice.data(), static_cast<std::uint32_t>(ScopedVoice.size()));
    FBY_CHECK(ScopedChain.GateOpen());
    FBY_CHECK(Rms(ScopedVoice) > 0.05f);

    std::vector<float> ScopedAfter(3000, 0.0f);
    ScopedChain.Process(ScopedAfter.data(), static_cast<std::uint32_t>(ScopedAfter.size()));
    FBY_CHECK(!ScopedChain.GateOpen());
}

void TestAgcAndLimiter()
{
    using namespace Fby;

    VoiceDspSettings ScopedAgcSettings{};
    ScopedAgcSettings.Denoise = VoiceDenoiseTier::None;
    ScopedAgcSettings.AgcEnabled = true;
    ScopedAgcSettings.AgcTargetDbfs = -20.0f;
    ScopedAgcSettings.AgcMaxGainDb = 30.0f;
    VoiceDspChain ScopedChain;
    ScopedChain.Configure(ScopedAgcSettings, kRate);

    std::vector<float> ScopedQuiet = Sine(0.01f, 440.0f, kRate * 2);
    const float ScopedInputRms = Rms(ScopedQuiet);
    ScopedChain.Process(ScopedQuiet.data(), static_cast<std::uint32_t>(ScopedQuiet.size()));
    const float ScopedOutputRms = Rms(ScopedQuiet);
    FBY_CHECK(ScopedOutputRms > ScopedInputRms * 3.0f);
    FBY_CHECK(ScopedChain.CurrentAgcGainDb() > 3.0f);

    VoiceDspSettings ScopedLimiterSettings{};
    ScopedLimiterSettings.Denoise = VoiceDenoiseTier::None;
    ScopedLimiterSettings.AgcEnabled = false;
    ScopedLimiterSettings.LimiterCeilingDb = -6.0f;
    VoiceDspChain ScopedLimiter;
    ScopedLimiter.Configure(ScopedLimiterSettings, kRate);
    std::vector<float> ScopedHot(2000, 1.0f);
    ScopedLimiter.Process(ScopedHot.data(), static_cast<std::uint32_t>(ScopedHot.size()));
    const float ScopedCeiling = std::pow(10.0f, -6.0f * 0.05f);
    FBY_CHECK(std::fabs(ScopedHot.back()) <= ScopedCeiling + 1e-4f);
}

void TestSpectral()
{
    using namespace Fby;

    VoiceDspSettings ScopedSettings{};
    ScopedSettings.Denoise = VoiceDenoiseTier::Spectral;
    ScopedSettings.AgcEnabled = false;
    VoiceDspChain ScopedChain;
    ScopedChain.Configure(ScopedSettings, kRate);

    std::vector<float> ScopedSilence(8192, 0.0f);
    ScopedChain.Process(ScopedSilence.data(), static_cast<std::uint32_t>(ScopedSilence.size()));
    FBY_CHECK(AllFinite(ScopedSilence));
    FBY_CHECK(Rms(ScopedSilence) < 1e-4f);

    ScopedChain.Reset();
    std::vector<float> ScopedTone = Sine(0.3f, 440.0f, 8192);
    ScopedChain.Process(ScopedTone.data(), static_cast<std::uint32_t>(ScopedTone.size()));
    FBY_CHECK(AllFinite(ScopedTone));
    FBY_CHECK(Rms(ScopedTone) > 0.02f);
    FBY_CHECK(Rms(ScopedTone) < 0.8f);

    ScopedChain.Reset();
    std::vector<float> ScopedNoise = Noise(0.05f, 8192);
    const float ScopedNoiseRms = Rms(ScopedNoise);
    ScopedChain.Process(ScopedNoise.data(), static_cast<std::uint32_t>(ScopedNoise.size()));
    FBY_CHECK(AllFinite(ScopedNoise));
    FBY_CHECK(Rms(ScopedNoise) <= ScopedNoiseRms * 1.25f);

    std::vector<float> ScopedNull;
    VoiceDspChain ScopedNoInput;
    ScopedNoInput.Configure(ScopedSettings, kRate);
    ScopedNoInput.Process(nullptr, 0);

    if (VoiceRnnoiseAvailable())
    {
        VoiceDspSettings ScopedRnnoiseSettings{};
        ScopedRnnoiseSettings.Denoise = VoiceDenoiseTier::Rnnoise;
        ScopedRnnoiseSettings.AgcEnabled = false;
        VoiceDspChain ScopedRnnoise;
        ScopedRnnoise.Configure(ScopedRnnoiseSettings, kRate);
        std::vector<float> ScopedRnnoiseTone = Sine(0.2f, 300.0f, 8192);
        ScopedRnnoise.Process(ScopedRnnoiseTone.data(), static_cast<std::uint32_t>(ScopedRnnoiseTone.size()));
        FBY_CHECK(AllFinite(ScopedRnnoiseTone));
        FBY_CHECK(Rms(ScopedRnnoiseTone) > 0.005f);
    }
}
}

int main()
{
    TestSettingsAndPresets();
    TestDcAndHighPass();
    TestGate();
    TestAgcAndLimiter();
    TestSpectral();

    if (GFailures == 0)
    {
        std::printf("voice_dsp_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_dsp_test: %u failure(s)\n", GFailures);
    return 1;
}
