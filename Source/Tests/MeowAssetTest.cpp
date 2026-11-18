// FemboyEngine2D - editor meow asset decode regression test.
#include "Engine/Audio/OggOpus.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        std::fflush(stdout);
        ++GFailures;
    }
}
}

int main()
{
    std::ifstream ScopedStream(FBY_MEOW_ASSET, std::ios::binary);
    Check(static_cast<bool>(ScopedStream), "the editor meow asset opens");
    std::vector<std::byte> ScopedBytes;
    if (ScopedStream)
    {
        ScopedStream.seekg(0, std::ios::end);
        const std::streamsize ScopedSize = ScopedStream.tellg();
        ScopedStream.seekg(0, std::ios::beg);
        if (ScopedSize > 0)
        {
            ScopedBytes.resize(static_cast<std::size_t>(ScopedSize));
            ScopedStream.read(reinterpret_cast<char*>(ScopedBytes.data()), ScopedSize);
        }
    }
    Check(ScopedBytes.size() > 100, "the editor meow asset is non-empty");

    std::vector<float> ScopedSamples;
    std::uint32_t ScopedFrames = 0;
    std::string ScopedError;
    const bool ScopedDecoded = Fby::Audio::DecodeOggOpusToStereo(
        ScopedBytes, Fby::Audio::kOggMaxWholeTrackFrames, &ScopedSamples, &ScopedFrames, &ScopedError);
    Check(ScopedDecoded, "the trimmed meow asset decodes as Ogg Opus");
    if (!ScopedDecoded)
    {
        std::printf("decode error: %s\n", ScopedError.c_str());
        return 1;
    }
    Check(ScopedFrames > 20000 && ScopedFrames < 40000, "the trimmed meow asset is about 0.6 seconds");
    Check(ScopedSamples.size() == static_cast<std::size_t>(ScopedFrames) * 2u,
          "the decoder returns stereo frames");
    float ScopedPeak = 0.0f;
    for (const float ScopedSample : ScopedSamples)
    {
        ScopedPeak = std::max(ScopedPeak, std::fabs(ScopedSample));
    }
    std::printf("meow frames=%u samples=%zu peak=%f\n", ScopedFrames, ScopedSamples.size(),
                static_cast<double>(ScopedPeak));
    Check(ScopedPeak > 0.01f, "the trimmed meow asset is audible");

    if (GFailures != 0)
    {
        std::printf("%d meow asset test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("meow_asset_test: all tests passed (%u frames)\n", ScopedFrames);
    return 0;
}
