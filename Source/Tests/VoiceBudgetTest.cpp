// FemboyEngine2D - voice CPU and bandwidth budget checks.
#include "Engine/Audio/VoiceAec.hpp"
#include "Engine/Audio/VoiceCodec.hpp"
#include "Engine/Audio/VoiceDsp.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
constexpr std::uint32_t kPacketHeaderBytes = 12;

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
        ScopedSamples[ScopedIndex] = 0.25f * std::sin(2.0f * 3.14159265f * 300.0f *
                                                      static_cast<float>(ScopedIndex) / 48000.0f);
    }
    return ScopedSamples;
}

void TestCodecBandwidth()
{
    using namespace Fby;

    VoiceEncoder ScopedEncoder;
    if (!ScopedEncoder.Initialize())
    {
        std::printf("voice_budget_test: Opus unavailable, bandwidth skipped\n");
        return;
    }
    constexpr std::uint32_t kFrames = 100;
    const std::vector<float> ScopedSpeech = Sine(kVoiceFrameSamples);
    std::vector<std::uint8_t> ScopedPacket(kVoiceMaxPacketBytes);
    std::uint64_t ScopedBytes = 0;
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < kFrames; ++ScopedFrame)
    {
        const std::uint32_t ScopedEncoded = ScopedEncoder.Encode(ScopedSpeech, ScopedPacket);
        FBY_CHECK(ScopedEncoded > 0);
        ScopedBytes += ScopedEncoded + kPacketHeaderBytes;
    }
    const double ScopedSeconds = static_cast<double>(kFrames) * 0.02;
    const double ScopedBitsPerSecond = static_cast<double>(ScopedBytes) * 8.0 / ScopedSeconds;
    std::printf("voice_budget_test: codec %.0f bit/s with packet headers\n", ScopedBitsPerSecond);
    FBY_CHECK(ScopedBitsPerSecond < 60000.0);
}

void TestDspBudget()
{
    using namespace Fby;

    VoiceDspSettings ScopedSettings{};
    ScopedSettings.Denoise = VoiceDenoiseTier::Spectral;
    VoiceDspChain ScopedChain;
    ScopedChain.Configure(ScopedSettings, 48000);
    std::vector<float> ScopedBlock = Sine(480);
    const auto ScopedStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 1000; ++ScopedIndex)
    {
        ScopedChain.Process(ScopedBlock.data(), 480);
    }
    const double ScopedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ScopedStart).count();
    std::printf("voice_budget_test: spectral 10s audio in %.3f s\n", ScopedSeconds);
    FBY_CHECK(ScopedSeconds < 2.0);
}

void TestAecBudget()
{
    using namespace Fby;

    VoiceAecSettings ScopedSettings{};
    VoiceAec ScopedAec;
    ScopedAec.Configure(ScopedSettings, 48000);
    std::vector<float> ScopedMic(480, 0.01f);
    std::vector<float> ScopedReference(480, 0.02f);
    const auto ScopedStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 1000; ++ScopedIndex)
    {
        ScopedAec.Process(ScopedMic.data(), ScopedReference.data(), 480);
    }
    const double ScopedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ScopedStart).count();
    std::printf("voice_budget_test: AEC 10s audio in %.3f s\n", ScopedSeconds);
    FBY_CHECK(ScopedSeconds < 1.0);
}

void TestRnnoiseBudget()
{
    using namespace Fby;

    if (!VoiceRnnoiseAvailable())
    {
        std::printf("voice_budget_test: RNNoise not compiled in, skipped\n");
        return;
    }
    VoiceDspSettings ScopedSettings{};
    ScopedSettings.Denoise = VoiceDenoiseTier::Rnnoise;
    ScopedSettings.AgcEnabled = false;
    VoiceDspChain ScopedChain;
    ScopedChain.Configure(ScopedSettings, 48000);
    std::vector<float> ScopedBuffer(480);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 480; ++ScopedIndex)
    {
        ScopedBuffer[ScopedIndex] = 0.05f * std::sin(2.0f * 3.14159265f * 300.0f *
                                                    static_cast<float>(ScopedIndex) / 48000.0f);
    }
    const auto ScopedStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedBlock = 0; ScopedBlock < 1000; ++ScopedBlock)
    {
        ScopedChain.Process(ScopedBuffer.data(), static_cast<std::uint32_t>(ScopedBuffer.size()));
    }
    const double ScopedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ScopedStart).count();
    std::printf("voice_budget_test: RNNoise 10s audio in %.3f s\n", ScopedSeconds);
    FBY_CHECK(ScopedSeconds < 1.0);
}
}

int main()
{
    TestCodecBandwidth();
    TestDspBudget();
    TestAecBudget();
    TestRnnoiseBudget();

    if (GFailures == 0)
    {
        std::printf("voice_budget_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_budget_test: %u failure(s)\n", GFailures);
    return 1;
}
