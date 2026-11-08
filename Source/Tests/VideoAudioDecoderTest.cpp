// FemboyEngine2D - Opus packet decoding for video audio tracks.
#include "Engine/Audio/VoiceCodec.hpp"
#include "Engine/Video/VideoAudioDecoder.hpp"

#include <algorithm>
#include <array>
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
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)
}

int main()
{
    Fby::VoiceEncoder ScopedEncoder;
    FBY_CHECK(ScopedEncoder.Initialize());

    std::array<float, Fby::kVoiceFrameSamples> ScopedSamples{};
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedSamples.size(); ++ScopedI)
    {
        ScopedSamples[ScopedI] = std::sin(static_cast<float>(ScopedI) * 0.05f) * 0.25f;
    }
    std::vector<std::uint8_t> ScopedPacket(Fby::kVoiceMaxPacketBytes);
    const std::uint32_t ScopedPacketBytes = ScopedEncoder.Encode(ScopedSamples, ScopedPacket);
    FBY_CHECK(ScopedPacketBytes > 0);

    Fby::Video::VideoAudioDecoder ScopedDecoder;
    std::string ScopedError;
    FBY_CHECK(ScopedDecoder.Initialize(48000, 1, &ScopedError));
    FBY_CHECK(ScopedDecoder.Ready());
    FBY_CHECK(ScopedDecoder.Channels() == 1);

    std::array<float, Fby::Video::kMaxOpusFrameSamples> ScopedPcm{};
    std::uint32_t ScopedFrames = 0;
    FBY_CHECK(ScopedDecoder.Decode(std::span<const std::uint8_t>(ScopedPacket.data(), ScopedPacketBytes), ScopedPcm,
                                   &ScopedFrames, &ScopedError));
    FBY_CHECK(ScopedFrames > 0);
    float ScopedPeak = 0.0f;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedFrames; ++ScopedI)
    {
        ScopedPeak = std::max(ScopedPeak, std::abs(ScopedPcm[ScopedI]));
    }
    FBY_CHECK(ScopedPeak > 0.01f);

    std::array<float, Fby::Video::kMaxOpusFrameSamples> ScopedShort{};
    FBY_CHECK(!ScopedDecoder.Decode(std::span<const std::uint8_t>(), ScopedShort, &ScopedFrames, &ScopedError));
    std::uint8_t ScopedGarbage[3] = {0xFF, 0x00, 0x11};
    FBY_CHECK(!ScopedDecoder.Decode(std::span<const std::uint8_t>(ScopedGarbage, 3), ScopedShort, &ScopedFrames,
                                    &ScopedError));

    ScopedDecoder.Shutdown();
    FBY_CHECK(!ScopedDecoder.Ready());

    if (GFailures == 0)
    {
        std::printf("video_audio: OK (opus decode, malformed packet)\n");
        return 0;
    }
    std::fprintf(stderr, "video_audio: %u failure(s)\n", GFailures);
    return 1;
}
