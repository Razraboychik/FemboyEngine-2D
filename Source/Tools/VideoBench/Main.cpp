// FemboyEngine2D - AV1 decode benchmark (real file, no renderer).
#include "Engine/Video/Av1Backend.hpp"
#include "Engine/Video/VideoPlayer.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int InCount, char** InArgs)
{
    std::string ScopedPath;
    std::uint32_t ScopedFrames = 300;
    for (int ScopedIndex = 1; ScopedIndex < InCount; ++ScopedIndex)
    {
        const std::string ScopedArgument = InArgs[ScopedIndex];
        if (ScopedArgument == "--file" && ScopedIndex + 1 < InCount)
        {
            ScopedPath = InArgs[++ScopedIndex];
        }
        else if (ScopedArgument == "--frames" && ScopedIndex + 1 < InCount)
        {
            ScopedFrames = static_cast<std::uint32_t>(std::stoul(InArgs[++ScopedIndex]));
        }
        else
        {
            std::fprintf(stderr, "usage: fby_video_bench --file <av1.webm> [--frames N]\n");
            return 1;
        }
    }
    if (ScopedPath.empty() || ScopedFrames == 0)
    {
        std::fprintf(stderr, "usage: fby_video_bench --file <av1.webm> [--frames N]\n");
        return 1;
    }
    const Fby::Video::Av1Backend* ScopedBackend = Fby::Video::Libdav1dBackend();
    if (ScopedBackend == nullptr)
    {
        std::fprintf(stderr, "fby_video_bench: dav1d is not available\n");
        return 77;
    }
    std::ifstream ScopedFile(ScopedPath, std::ios::binary);
    if (!ScopedFile)
    {
        std::fprintf(stderr, "fby_video_bench: cannot open %s\n", ScopedPath.c_str());
        return 1;
    }
    const std::vector<char> ScopedBytes((std::istreambuf_iterator<char>(ScopedFile)), std::istreambuf_iterator<char>());
    const std::span<const std::byte> ScopedData(reinterpret_cast<const std::byte*>(ScopedBytes.data()),
                                                ScopedBytes.size());
    Fby::Video::VideoPlayer ScopedPlayer;
    std::string ScopedError;
    if (!ScopedPlayer.Open(ScopedData, nullptr, &ScopedError, nullptr, ScopedBackend))
    {
        std::fprintf(stderr, "fby_video_bench: open failed: %s\n", ScopedError.c_str());
        return 1;
    }

    std::uint32_t ScopedDecoded = 0;
    const auto ScopedStart = std::chrono::steady_clock::now();
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedFrames; ++ScopedIndex)
    {
        const Fby::Video::DecodeStatus ScopedStatus = ScopedPlayer.PumpFrame();
        if (ScopedStatus == Fby::Video::DecodeStatus::Decoded)
        {
            ++ScopedDecoded;
        }
        else if (ScopedStatus == Fby::Video::DecodeStatus::RingFull)
        {
            ScopedPlayer.Advance(1.0f / 60.0f, 1.0f, false, false);
        }
        else
        {
            break;
        }
    }
    const auto ScopedElapsed = std::chrono::steady_clock::now() - ScopedStart;
    const double ScopedMs = std::chrono::duration<double, std::milli>(ScopedElapsed).count();
    std::printf("video_bench: %ux%u, %u frames, %.2f ms, %.3f ms/frame, %.1f fps\n", ScopedPlayer.Track().width,
                ScopedPlayer.Track().height, ScopedDecoded, ScopedMs, ScopedDecoded > 0 ? ScopedMs / ScopedDecoded : 0.0,
                ScopedMs > 0.0 ? ScopedDecoded * 1000.0 / ScopedMs : 0.0);
    return ScopedDecoded > 0 ? 0 : 1;
}
