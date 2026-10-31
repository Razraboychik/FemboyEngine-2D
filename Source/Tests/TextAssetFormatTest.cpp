// FemboyEngine2D - cooked text asset format test.
#include "Engine/Text/TextAssetFormat.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void TestRoundTrip()
{
    using namespace Fby::Text;

    const std::string ScopedSource =
        "First paragraph with several words.\nSecond paragraph with a long line that wraps at a space.";
    std::string ScopedError;
    const std::vector<std::byte> ScopedBytes = BuildTextAsset(ScopedSource, 24, &ScopedError);
    Check(!ScopedBytes.empty(), "text asset builds");
    TextAssetView ScopedView;
    Check(OpenTextAsset(ScopedBytes, &ScopedView, &ScopedError) == TextAssetStatus::Ok, "text asset opens");
    Check(ScopedView.Header.BlockCount > 1, "text asset has multiple blocks");
    Check(ScopedView.Text == ScopedSource, "text asset keeps the source bytes");

    std::string ScopedReconstructed;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedView.Header.BlockCount; ++ScopedIndex)
    {
        ScopedReconstructed += ScopedView.Block(ScopedIndex);
    }
    Check(ScopedReconstructed == ScopedSource, "text asset blocks reconstruct the source");
    Check(ScopedView.Block(999).empty(), "out-of-range text block is empty");

    const std::vector<std::byte> ScopedEmpty = BuildTextAsset("", 16, &ScopedError);
    TextAssetView ScopedEmptyView;
    Check(OpenTextAsset(ScopedEmpty, &ScopedEmptyView, &ScopedError) == TextAssetStatus::Ok,
          "empty text asset opens");
    Check(ScopedEmptyView.Header.BlockCount == 1, "empty text asset has one block");
}

void TestCorruption()
{
    using namespace Fby::Text;

    std::vector<std::byte> ScopedBytes = BuildTextAsset("hello text", 8, nullptr);
    Check(!ScopedBytes.empty(), "corruption source builds");

    std::vector<std::byte> ScopedBadMagic = ScopedBytes;
    ScopedBadMagic[0] = std::byte{'X'};
    TextAssetView ScopedView;
    Check(OpenTextAsset(ScopedBadMagic, &ScopedView, nullptr) == TextAssetStatus::InvalidHeader,
          "bad magic rejects");

    std::vector<std::byte> ScopedTruncated(ScopedBytes.begin(), ScopedBytes.begin() + 12);
    Check(OpenTextAsset(ScopedTruncated, &ScopedView, nullptr) == TextAssetStatus::Truncated,
          "truncated header rejects");

    std::vector<std::byte> ScopedBadRange = ScopedBytes;
    TextAssetHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, ScopedBadRange.data(), sizeof(ScopedHeader));
    TextAssetBlock ScopedBlock{};
    ScopedBlock.Offset = 0;
    ScopedBlock.Length = 1000;
    std::memcpy(ScopedBadRange.data() + ScopedHeader.BlockTableOffset, &ScopedBlock, sizeof(ScopedBlock));
    Check(OpenTextAsset(ScopedBadRange, &ScopedView, nullptr) == TextAssetStatus::InvalidBlockRange,
          "block range corruption rejects");
}
}

int main()
{
    TestRoundTrip();
    TestCorruption();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text asset format test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text asset format tests: ok\n");
    return 0;
}
