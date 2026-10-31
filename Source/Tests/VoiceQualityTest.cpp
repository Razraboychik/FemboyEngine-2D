// FemboyEngine2D - RNNoise wet/dry, Opus FEC and jitter first-packet tests.
#include "Engine/Audio/VoiceChannel.hpp"
#include "Engine/Audio/VoiceCodec.hpp"
#include "Engine/Audio/VoiceDsp.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
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

bool NullSend(void*, std::uint8_t, std::span<const std::uint8_t>) noexcept
{
    return true;
}

[[nodiscard]] std::vector<float> NoisySpeech(std::uint32_t InFrames)
{
    std::vector<float> ScopedSamples(InFrames);
    std::uint32_t ScopedState = 0x12345678u;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InFrames; ++ScopedIndex)
    {
        ScopedState = ScopedState * 1664525u + 1013904223u;
        const float ScopedNoise =
            static_cast<float>((ScopedState >> 8) & 0xFFFFu) / 65535.0f * 2.0f - 1.0f;
        const float ScopedTone =
            0.15f * std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(ScopedIndex) / 48000.0f);
        ScopedSamples[ScopedIndex] = ScopedTone + 0.05f * ScopedNoise;
    }
    return ScopedSamples;
}

[[nodiscard]] Fby::VoiceDspSettings ChainSettings(Fby::VoiceDenoiseTier InTier, float InStrength)
{
    Fby::VoiceDspSettings ScopedSettings{};
    ScopedSettings.Denoise = InTier;
    ScopedSettings.DenoiseStrength = InStrength;
    ScopedSettings.AgcEnabled = false;
    ScopedSettings.GateThresholdDb = -90.0f;
    return ScopedSettings;
}

int RunRnnoiseMix()
{
    using namespace Fby;
    if (!VoiceRnnoiseAvailable())
    {
        std::printf("rnnoise_mix: RNNoise is not compiled in, skipped\n");
        return 0;
    }
    const std::uint32_t ScopedFrames = kVoiceFrameSamples * 8;
    const std::vector<float> ScopedInput = NoisySpeech(ScopedFrames);
    std::vector<float> ScopedDry = ScopedInput;
    std::vector<float> ScopedMix0 = ScopedInput;
    std::vector<float> ScopedMix1 = ScopedInput;
    std::vector<float> ScopedMixHalf = ScopedInput;

    VoiceDspChain ScopedDryChain;
    VoiceDspChain ScopedWet0;
    VoiceDspChain ScopedWet1;
    VoiceDspChain ScopedHalf;
    ScopedDryChain.Configure(ChainSettings(VoiceDenoiseTier::None, 0.0f), 48000);
    ScopedWet0.Configure(ChainSettings(VoiceDenoiseTier::Rnnoise, 0.0f), 48000);
    ScopedWet1.Configure(ChainSettings(VoiceDenoiseTier::Rnnoise, 1.0f), 48000);
    ScopedHalf.Configure(ChainSettings(VoiceDenoiseTier::Rnnoise, 0.5f), 48000);
    ScopedDryChain.Process(ScopedDry.data(), ScopedFrames);
    ScopedWet0.Process(ScopedMix0.data(), ScopedFrames);
    ScopedWet1.Process(ScopedMix1.data(), ScopedFrames);
    ScopedHalf.Process(ScopedMixHalf.data(), ScopedFrames);

    // Wet/dry is a linear crossfade: strength 0.5 is the exact average of the
    // dry and wet chains for every sample after the one-frame RNNoise latency.
    float ScopedMaxMixError = 0.0f;
    for (std::uint32_t ScopedIndex = 2 * kVoiceRnnoiseFrame; ScopedIndex < ScopedFrames; ++ScopedIndex)
    {
        const float ScopedExpected = 0.5f * (ScopedMix0[ScopedIndex] + ScopedMix1[ScopedIndex]);
        ScopedMaxMixError = std::max(ScopedMaxMixError, std::fabs(ScopedMixHalf[ScopedIndex] - ScopedExpected));
    }
    FBY_CHECK(ScopedMaxMixError < 1e-5f);

    // Strength 0 reproduces the dry chain shifted by the RNNoise frame latency.
    float ScopedMaxDryError = 0.0f;
    for (std::uint32_t ScopedIndex = 2 * kVoiceRnnoiseFrame; ScopedIndex < ScopedFrames; ++ScopedIndex)
    {
        ScopedMaxDryError = std::max(
            ScopedMaxDryError,
            std::fabs(ScopedMix0[ScopedIndex] - ScopedDry[ScopedIndex - kVoiceRnnoiseFrame]));
    }
    FBY_CHECK(ScopedMaxDryError < 1e-4f);
    FBY_CHECK(ScopedWet1.RnnoiseSpeechProbability() >= 0.0f);
    FBY_CHECK(ScopedWet1.RnnoiseSpeechProbability() <= 1.0f);

    if (GFailures == 0)
    {
        std::printf("rnnoise_mix_test: all checks passed (max mix error %.2e)\n", static_cast<double>(ScopedMaxMixError));
        return 0;
    }
    std::fprintf(stderr, "rnnoise_mix_test: %u failure(s)\n", GFailures);
    return 1;
}

struct PacketBuild
{
    std::array<std::uint8_t, Fby::kVoicePacketHeaderBytes + Fby::kVoiceMaxPacketBytes> Data{};
    std::uint32_t Bytes = 0;
};

[[nodiscard]] PacketBuild BuildPacket(std::uint8_t InSpeaker, std::uint16_t InSequence,
                                      std::span<const std::uint8_t> InPayload)
{
    PacketBuild ScopedPacket;
    const std::uint32_t ScopedMagic = Fby::kVoicePacketMagic;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 4; ++ScopedIndex)
    {
        ScopedPacket.Data[ScopedIndex] = static_cast<std::uint8_t>((ScopedMagic >> (ScopedIndex * 8)) & 0xFFu);
    }
    ScopedPacket.Data[4] = 1;
    ScopedPacket.Data[5] = 0;
    ScopedPacket.Data[6] = InSpeaker;
    ScopedPacket.Data[7] = 0;
    ScopedPacket.Data[8] = static_cast<std::uint8_t>(InSequence & 0xFFu);
    ScopedPacket.Data[9] = static_cast<std::uint8_t>((InSequence >> 8) & 0xFFu);
    ScopedPacket.Data[10] = static_cast<std::uint8_t>(Fby::kVoiceFrameSamples & 0xFFu);
    ScopedPacket.Data[11] = static_cast<std::uint8_t>((Fby::kVoiceFrameSamples >> 8) & 0xFFu);
    std::memcpy(ScopedPacket.Data.data() + Fby::kVoicePacketHeaderBytes, InPayload.data(), InPayload.size());
    ScopedPacket.Bytes = Fby::kVoicePacketHeaderBytes + static_cast<std::uint32_t>(InPayload.size());
    return ScopedPacket;
}

[[nodiscard]] bool EncodeFrame(Fby::VoiceEncoder& InEncoder, const std::vector<float>& InSamples, PacketBuild* OutPacket)
{
    std::array<std::uint8_t, Fby::kVoiceMaxPacketBytes> ScopedPayload{};
    const std::uint32_t ScopedBytes =
        InEncoder.Encode(std::span<const float>(InSamples.data(), Fby::kVoiceFrameSamples), ScopedPayload);
    if (ScopedBytes == 0)
    {
        return false;
    }
    *OutPacket = BuildPacket(0, 0, std::span<const std::uint8_t>(ScopedPayload.data(), ScopedBytes));
    return true;
}

int RunJitterInit()
{
    using namespace Fby;
    VoiceEncoder ScopedEncoder;
    FBY_CHECK(ScopedEncoder.Initialize(VoiceCodecConfig{}));
    const std::vector<float> ScopedTone = NoisySpeech(kVoiceFrameSamples);
    std::array<PacketBuild, 3> ScopedPackets{};
    for (auto& ScopedPacket : ScopedPackets)
    {
        FBY_CHECK(EncodeFrame(ScopedEncoder, ScopedTone, &ScopedPacket));
    }

    VoiceChannel ScopedChannel;
    VoiceChannelConfig ScopedConfig{};
    ScopedConfig.LocalPlayer = 1;
    ScopedConfig.Routing = VoiceRouting::Mesh;
    FBY_CHECK(ScopedChannel.Start(ScopedConfig, &NullSend, nullptr));
    FBY_CHECK(ScopedChannel.AddParticipant(0));

    // Deliver 5 first, then the earlier 4 and 3 before the first playout tick.
    const std::uint16_t ScopedOrder[3] = {5, 4, 3};
    for (const std::uint16_t ScopedSequence : ScopedOrder)
    {
        PacketBuild ScopedPacket = ScopedPackets[0];
        ScopedPacket.Data[8] = static_cast<std::uint8_t>(ScopedSequence & 0xFFu);
        ScopedPacket.Data[9] = static_cast<std::uint8_t>((ScopedSequence >> 8) & 0xFFu);
        FBY_CHECK(ScopedChannel.ReceivePacket(0, std::span<const std::uint8_t>(ScopedPacket.Data.data(),
                                                                              ScopedPacket.Bytes)));
    }
    ScopedChannel.Tick(0.02f, nullptr);
    FBY_CHECK(ScopedChannel.Stats().FirstPlayedValid);
    FBY_CHECK(ScopedChannel.Stats().FirstPlayedSequence == 3);

    if (GFailures == 0)
    {
        std::printf("jitter_init_test: all checks passed (first played %u)\n",
                    static_cast<unsigned>(ScopedChannel.Stats().FirstPlayedSequence));
        return 0;
    }
    std::fprintf(stderr, "jitter_init_test: %u failure(s)\n", GFailures);
    return 1;
}

int RunVoiceFec()
{
    using namespace Fby;
    VoiceEncoder ScopedEncoder;
    VoiceCodecConfig ScopedCodec{};
    ScopedCodec.InbandFec = true;
    ScopedCodec.PacketLossPercent = 20;
    FBY_CHECK(ScopedEncoder.Initialize(ScopedCodec));
    const std::vector<float> ScopedTone = NoisySpeech(kVoiceFrameSamples);
    std::array<PacketBuild, 3> ScopedPackets{};
    for (auto& ScopedPacket : ScopedPackets)
    {
        FBY_CHECK(EncodeFrame(ScopedEncoder, ScopedTone, &ScopedPacket));
    }

    VoiceChannel ScopedChannel;
    VoiceChannelConfig ScopedConfig{};
    ScopedConfig.LocalPlayer = 1;
    ScopedConfig.Routing = VoiceRouting::Mesh;
    FBY_CHECK(ScopedChannel.Start(ScopedConfig, &NullSend, nullptr));
    FBY_CHECK(ScopedChannel.AddParticipant(0));

    // Frame 0 arrives, frame 1 is lost, frame 2 carries the FEC for frame 1.
    PacketBuild ScopedFirst = ScopedPackets[0];
    ScopedFirst.Data[8] = 0;
    ScopedFirst.Data[9] = 0;
    FBY_CHECK(ScopedChannel.ReceivePacket(
        0, std::span<const std::uint8_t>(ScopedFirst.Data.data(), ScopedFirst.Bytes)));
    ScopedChannel.Tick(0.02f, nullptr);
    FBY_CHECK(ScopedChannel.Stats().FramesDecoded >= 1);

    PacketBuild ScopedThird = ScopedPackets[2];
    ScopedThird.Data[8] = 2;
    ScopedThird.Data[9] = 0;
    FBY_CHECK(ScopedChannel.ReceivePacket(
        0, std::span<const std::uint8_t>(ScopedThird.Data.data(), ScopedThird.Bytes)));
    ScopedChannel.Tick(0.02f, nullptr);
    FBY_CHECK(ScopedChannel.Stats().FramesFecRecovered >= 1);
    // The FEC packet itself still decodes on the next playout step.
    ScopedChannel.Tick(0.02f, nullptr);
    FBY_CHECK(ScopedChannel.Stats().FramesDecoded >= 2);

    if (GFailures == 0)
    {
        std::printf("voice_fec_test: all checks passed (fec %llu)\n",
                    static_cast<unsigned long long>(ScopedChannel.Stats().FramesFecRecovered));
        return 0;
    }
    std::fprintf(stderr, "voice_fec_test: %u failure(s)\n", GFailures);
    return 1;
}
}

int main(int argc, char** argv)
{
    const std::string_view ScopedMode = argc > 1 ? std::string_view(argv[1]) : std::string_view("all");
    if (ScopedMode == "rnnoise_mix")
    {
        return RunRnnoiseMix();
    }
    if (ScopedMode == "jitter_init")
    {
        return RunJitterInit();
    }
    if (ScopedMode == "voice_fec")
    {
        return RunVoiceFec();
    }
    std::fprintf(stderr, "usage: %s rnnoise_mix|jitter_init|voice_fec\n", argv[0]);
    return 2;
}
