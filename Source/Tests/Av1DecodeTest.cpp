// FemboyEngine2D - manual AV1 decode smoke test (requires an AV1 WebM path).
#include "Engine/Video/Av1Backend.hpp"
#include "Engine/Video/VideoPlayer.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int InCount, char** InArgs)
{
    if (InCount < 2)
    {
        std::fprintf(stderr, "usage: fby_av1_decode_test <file.webm>\n");
        return 2;
    }
    const Fby::Video::Av1Backend* ScopedBackend = Fby::Video::Libdav1dBackend();
    if (ScopedBackend == nullptr)
    {
        std::fprintf(stderr, "AV1 backend is not built (dav1d missing)\n");
        return 77;
    }
    std::ifstream ScopedFile(InArgs[1], std::ios::binary);
    if (!ScopedFile)
    {
        std::fprintf(stderr, "cannot open %s\n", InArgs[1]);
        return 2;
    }
    const std::vector<char> ScopedBytes((std::istreambuf_iterator<char>(ScopedFile)), std::istreambuf_iterator<char>());
    const std::span<const std::byte> ScopedData(reinterpret_cast<const std::byte*>(ScopedBytes.data()), ScopedBytes.size());
    Fby::Video::VideoPlayer ScopedPlayer;
    std::string ScopedError;
    if (!ScopedPlayer.Open(ScopedData, nullptr, &ScopedError, nullptr, ScopedBackend))
    {
        std::fprintf(stderr, "open failed: %s\n", ScopedError.c_str());
        return 1;
    }
    std::printf("opened %ux%u codec %s\n", ScopedPlayer.Track().width, ScopedPlayer.Track().height,
                ScopedPlayer.Track().codecId);
    std::uint32_t ScopedDecoded = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < 8; ++ScopedI)
    {
        const Fby::Video::DecodeStatus ScopedStatus = ScopedPlayer.PumpFrame();
        if (ScopedStatus == Fby::Video::DecodeStatus::Decoded)
        {
            ++ScopedDecoded;
        }
        else if (ScopedStatus != Fby::Video::DecodeStatus::RingFull)
        {
            break;
        }
    }
    std::printf("decoded %u frame(s), error=%s\n", ScopedDecoded, ScopedPlayer.ErrorText().data());
    return ScopedDecoded > 0 ? 0 : 1;
}
