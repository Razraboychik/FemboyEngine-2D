// FemboyEngine2D - fixed PCM ring and audio clock for video playback.
#include "Engine/Video/VideoAudioStream.hpp"

#include <array>
#include <cmath>
#include <cstdio>

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
    Fby::Video::VideoAudioStream ScopedStream;
    std::string ScopedError;
    FBY_CHECK(ScopedStream.Initialize(48000, 1, &ScopedError));
    FBY_CHECK(ScopedStream.IsOpen());
    FBY_CHECK(ScopedStream.Channels() == 1);
    FBY_CHECK(ScopedStream.AudioClockNs() == 0);

    std::array<float, 960> ScopedBlock{};
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedBlock.size(); ++ScopedI)
    {
        ScopedBlock[ScopedI] = std::sin(static_cast<float>(ScopedI) * 0.02f) * 0.1f;
    }
    FBY_CHECK(ScopedStream.PushPcm(ScopedBlock, 0));
    FBY_CHECK(ScopedStream.PushPcm(ScopedBlock, 20000000ull));
    FBY_CHECK(ScopedStream.AvailableFrames() == 1920);
    FBY_CHECK(ScopedStream.LastPushedTimestampNs() == 20000000ull);

    std::array<float, 480> ScopedHalf{};
    FBY_CHECK(ScopedStream.Read(ScopedHalf) == 480);
    FBY_CHECK(ScopedStream.AudioClockNs() == 10000000ull);
    FBY_CHECK(ScopedStream.AvailableFrames() == 1440);
    FBY_CHECK(ScopedStream.Read(ScopedHalf) == 480);
    FBY_CHECK(ScopedStream.AudioClockNs() == 20000000ull);

    // Drain the remainder before filling the ring.
    std::array<float, Fby::Video::VideoAudioStream::kMaxRingFrames> ScopedFull{};
    FBY_CHECK(ScopedStream.Read(ScopedFull) == 960);
    FBY_CHECK(ScopedStream.AvailableFrames() == 0);

    // The ring is fixed: a push that does not fit is rejected whole.
    FBY_CHECK(ScopedStream.PushPcm(ScopedFull, 40000000ull));
    FBY_CHECK(ScopedStream.AvailableFrames() == Fby::Video::VideoAudioStream::kMaxRingFrames);
    FBY_CHECK(!ScopedStream.PushPcm(ScopedBlock, 60000000ull));
    FBY_CHECK(ScopedStream.Read(ScopedFull) == Fby::Video::VideoAudioStream::kMaxRingFrames);
    FBY_CHECK(ScopedStream.AvailableFrames() == 0);

    // Wrap-around read/write: push 4000, consume 3000, push 3000, read 4000.
    std::array<float, 4000> ScopedFourThousand{};
    FBY_CHECK(ScopedStream.PushPcm(ScopedFourThousand, 100000000ull));
    std::array<float, 3000> ScopedThreeThousand{};
    FBY_CHECK(ScopedStream.Read(ScopedThreeThousand) == 3000);
    FBY_CHECK(ScopedStream.PushPcm(ScopedThreeThousand, 120000000ull));
    FBY_CHECK(ScopedStream.AvailableFrames() == 4000);
    FBY_CHECK(ScopedStream.Read(ScopedFull) == 4000);

    // Playback rate: 2x consumes two source frames per output frame; 0.5x
    // duplicates each source frame.
    Fby::Video::VideoAudioStream ScopedRate;
    FBY_CHECK(ScopedRate.Initialize(48000, 1, &ScopedError));
    ScopedRate.SetPlaybackRate(2.0f);
    FBY_CHECK(ScopedRate.PlaybackRate() == 2.0f);
    FBY_CHECK(ScopedRate.PushPcm(ScopedBlock, 0));
    std::array<float, 480> ScopedFast{};
    FBY_CHECK(ScopedRate.Read(ScopedFast) == 480);
    FBY_CHECK(ScopedRate.AvailableFrames() == 0);
    FBY_CHECK(ScopedRate.AudioClockNs() == 20000000ull);
    ScopedRate.SetPlaybackRate(0.5f);
    FBY_CHECK(ScopedRate.PushPcm(ScopedHalf, 0));
    std::array<float, 960> ScopedSlow{};
    FBY_CHECK(ScopedRate.Read(ScopedSlow) == 960);
    FBY_CHECK(ScopedRate.AudioClockNs() == 30000000ull);
    ScopedRate.SetPlaybackRate(1.0f);
    FBY_CHECK(ScopedRate.Read(ScopedFast) == 0);
    ScopedRate.Close();

    ScopedStream.Close();
    FBY_CHECK(!ScopedStream.IsOpen());
    FBY_CHECK(!ScopedStream.PushPcm(ScopedBlock, 0));

    if (GFailures == 0)
    {
        std::printf("video_audio_stream: OK (ring, wrap, clock, full)\n");
        return 0;
    }
    std::fprintf(stderr, "video_audio_stream: %u failure(s)\n", GFailures);
    return 1;
}
