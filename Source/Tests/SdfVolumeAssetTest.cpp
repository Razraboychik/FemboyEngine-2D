// FemboyEngine2D - .fsdf serialization tests.
#include "Engine/Physics/SdfVolumeAsset.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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

[[nodiscard]] float BoxDistance(float InX, float InY, float InCenterX, float InCenterY, float InHalfX,
                                float InHalfY)
{
    const float ScopedDx = std::abs(InX - InCenterX) - InHalfX;
    const float ScopedDy = std::abs(InY - InCenterY) - InHalfY;
    const float ScopedOutsideX = std::max(ScopedDx, 0.0f);
    const float ScopedOutsideY = std::max(ScopedDy, 0.0f);
    const float ScopedOutside = std::sqrt(ScopedOutsideX * ScopedOutsideX + ScopedOutsideY * ScopedOutsideY);
    const float ScopedInside = std::min(std::max(ScopedDx, ScopedDy), 0.0f);
    return ScopedOutside + ScopedInside;
}

// Two solid boxes on a 64x64 grid with four world units per cell.
void BuildBoxField(std::vector<float>* OutSamples, Fby::SdfField* OutField)
{
    constexpr std::uint32_t kWidth = 64;
    constexpr std::uint32_t kHeight = 64;
    OutSamples->assign(static_cast<std::size_t>(kWidth) * kHeight, 0.0f);
    for (std::uint32_t ScopedY = 0; ScopedY < kHeight; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < kWidth; ++ScopedX)
        {
            const float ScopedWorldX = 100.0f + (static_cast<float>(ScopedX) + 0.5f) * 4.0f;
            const float ScopedWorldY = 100.0f + (static_cast<float>(ScopedY) + 0.5f) * 4.0f;
            const float ScopedFirst = BoxDistance(ScopedWorldX, ScopedWorldY, 180.0f, 180.0f, 40.0f, 20.0f);
            const float ScopedSecond = BoxDistance(ScopedWorldX, ScopedWorldY, 280.0f, 260.0f, 30.0f, 30.0f);
            (*OutSamples)[static_cast<std::size_t>(ScopedY) * kWidth + ScopedX] =
                std::min(ScopedFirst, ScopedSecond);
        }
    }
    Fby::SdfField ScopedField;
    ScopedField.samples = OutSamples->data();
    ScopedField.width = kWidth;
    ScopedField.height = kHeight;
    ScopedField.origin = Fby::Vector2D{100.0f, 100.0f};
    ScopedField.cellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedField.outsideDistance = 1000.0f;
    *OutField = ScopedField;
}

void TestRoundTrip()
{
    std::vector<float> ScopedSamples;
    Fby::SdfField ScopedField;
    BuildBoxField(&ScopedSamples, &ScopedField);

    Fby::SdfVolumeAsset ScopedBaked;
    Check(ScopedBaked.Build(ScopedField, 32.0f), "bake builds the asset");
    Check(ScopedBaked.IsValid(), "baked asset is valid");
    Check(ScopedBaked.ContentHash() != 0, "baked asset has a content hash");

    std::vector<std::byte> ScopedBytes;
    Check(ScopedBaked.Save(&ScopedBytes), "save writes the asset");
    Check(ScopedBytes.size() > Fby::SdfVolumeHeaderBytes, "saved asset has a payload");

    Fby::SdfVolumeHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, ScopedBytes.data(), sizeof(ScopedHeader));
    Check(ScopedHeader.Magic == Fby::SdfVolumeMagic, "header carries the magic");
    Check(ScopedHeader.Version == Fby::SdfVolumeVersion, "header carries the version");
    Check(ScopedHeader.ContentHash == ScopedBaked.ContentHash(), "header carries the content hash");

    Fby::SdfVolumeAsset ScopedLoaded;
    std::string ScopedError;
    Check(ScopedLoaded.Load(ScopedBytes, &ScopedError), "load restores the asset");
    Check(ScopedError.empty(), "load reports no error");
    Check(ScopedLoaded.ContentHash() == ScopedBaked.ContentHash(), "hash survives the round trip");

    // The sparse bake must answer like the bake it came from, sample for sample.
    bool ScopedSamplesMatch = true;
    for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 64; ++ScopedX)
        {
            const Fby::Vector2D ScopedPoint{100.0f + (static_cast<float>(ScopedX) + 0.37f) * 4.0f,
                                            100.0f + (static_cast<float>(ScopedY) + 0.61f) * 4.0f};
            if (ScopedLoaded.Sample(ScopedPoint) != ScopedBaked.Sample(ScopedPoint))
            {
                ScopedSamplesMatch = false;
            }
        }
    }
    Check(ScopedSamplesMatch, "loaded samples match the baked samples");

    Check(ScopedLoaded.Sample(Fby::Vector2D{180.0f, 180.0f}) < 0.0f, "solid point stays solid");
    Check(ScopedLoaded.Sample(Fby::Vector2D{400.0f, 400.0f}) > 0.0f, "empty point stays outside");
}

void TestDeterminism()
{
    std::vector<float> ScopedSamples;
    Fby::SdfField ScopedField;
    BuildBoxField(&ScopedSamples, &ScopedField);

    Fby::SdfVolumeAsset ScopedFirst;
    Fby::SdfVolumeAsset ScopedSecond;
    Check(ScopedFirst.Build(ScopedField, 32.0f), "first bake builds");
    Check(ScopedSecond.Build(ScopedField, 32.0f), "second bake builds");
    Check(ScopedFirst.ContentHash() == ScopedSecond.ContentHash(), "the same field hashes the same");

    std::vector<std::byte> ScopedFirstBytes;
    std::vector<std::byte> ScopedSecondBytes;
    Check(ScopedFirst.Save(&ScopedFirstBytes), "first save");
    Check(ScopedSecond.Save(&ScopedSecondBytes), "second save");
    Check(ScopedFirstBytes == ScopedSecondBytes, "the same field serializes to the same bytes");
}

void TestRejectsCorruption()
{
    std::vector<float> ScopedSamples;
    Fby::SdfField ScopedField;
    BuildBoxField(&ScopedSamples, &ScopedField);

    Fby::SdfVolumeAsset ScopedBaked;
    Check(ScopedBaked.Build(ScopedField, 32.0f), "bake for the corruption cases");
    std::vector<std::byte> ScopedBytes;
    Check(ScopedBaked.Save(&ScopedBytes), "save for the corruption cases");

    // A flipped payload byte must fail the content hash.
    std::vector<std::byte> ScopedFlipped = ScopedBytes;
    ScopedFlipped[ScopedFlipped.size() - 1] ^= std::byte{0x01};
    Fby::SdfVolumeAsset ScopedCorrupted;
    std::string ScopedError;
    Check(!ScopedCorrupted.Load(ScopedFlipped, &ScopedError), "payload corruption is rejected");
    Check(!ScopedError.empty(), "payload corruption reports an error");

    // A truncated file must fail before the brick table is trusted.
    std::vector<std::byte> ScopedTruncated(ScopedBytes.begin(), ScopedBytes.end() - 8);
    Check(!ScopedCorrupted.Load(ScopedTruncated, &ScopedError), "truncated file is rejected");

    // A wrong magic must never be parsed as a volume.
    std::vector<std::byte> ScopedWrongMagic = ScopedBytes;
    ScopedWrongMagic[0] = std::byte{0x00};
    Check(!ScopedCorrupted.Load(ScopedWrongMagic, &ScopedError), "wrong magic is rejected");

    // A future version must be rejected instead of guessed.
    Fby::SdfVolumeHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, ScopedBytes.data(), sizeof(ScopedHeader));
    ScopedHeader.Version = Fby::SdfVolumeVersion + 1;
    std::vector<std::byte> ScopedFuture = ScopedBytes;
    std::memcpy(ScopedFuture.data(), &ScopedHeader, sizeof(ScopedHeader));
    Check(!ScopedCorrupted.Load(ScopedFuture, &ScopedError), "unsupported version is rejected");
}

void TestExpandToField()
{
    std::vector<float> ScopedSamples;
    Fby::SdfField ScopedField;
    BuildBoxField(&ScopedSamples, &ScopedField);

    Fby::SdfVolumeAsset ScopedBaked;
    Check(ScopedBaked.Build(ScopedField, 32.0f), "bake for the expansion case");

    std::vector<float> ScopedExpanded;
    Fby::SdfField ScopedExpandedField;
    Check(ScopedBaked.ExpandToField(&ScopedExpanded, &ScopedExpandedField), "expansion succeeds");
    Check(ScopedExpanded.size() == 64u * 64u, "expanded grid has the source size");
    Check(ScopedExpandedField.Valid(), "expanded field is a valid view");

    // Narrow bricks copy their payload exactly; full bricks collapse to their
    // recorded maximum; empty bricks collapse to their minimum.
    bool ScopedNarrowExact = true;
    bool ScopedFullNegative = true;
    bool ScopedEmptyConstant = true;
    for (const Fby::SdfBrickHeader& ScopedBrick : ScopedBaked.Bricks().BrickView())
    {
        for (std::uint32_t ScopedY = 0; ScopedY < Fby::SdfBrickSize; ++ScopedY)
        {
            const std::uint32_t ScopedTargetY = ScopedBrick.BrickY * Fby::SdfBrickSize + ScopedY;
            if (ScopedTargetY >= 64)
            {
                break;
            }
            for (std::uint32_t ScopedX = 0; ScopedX < Fby::SdfBrickSize; ++ScopedX)
            {
                const std::uint32_t ScopedTargetX = ScopedBrick.BrickX * Fby::SdfBrickSize + ScopedX;
                if (ScopedTargetX >= 64)
                {
                    break;
                }
                const std::size_t ScopedIndex = static_cast<std::size_t>(ScopedTargetY) * 64 + ScopedTargetX;
                if (ScopedBrick.State == Fby::SdfBrickState::Narrow &&
                    std::abs(ScopedExpanded[ScopedIndex] - ScopedSamples[ScopedIndex]) > 1.0e-3f)
                {
                    ScopedNarrowExact = false;
                }
                if (ScopedBrick.State == Fby::SdfBrickState::Full && ScopedExpanded[ScopedIndex] >= 0.0f)
                {
                    ScopedFullNegative = false;
                }
                if (ScopedBrick.State == Fby::SdfBrickState::Empty &&
                    ScopedExpanded[ScopedIndex] != ScopedBrick.MinDistance)
                {
                    ScopedEmptyConstant = false;
                }
            }
        }
    }
    Check(ScopedNarrowExact, "narrow bricks reproduce the dense samples");
    Check(ScopedFullNegative, "full bricks stay negative");
    Check(ScopedEmptyConstant, "empty bricks use their constant");
    Check(ScopedExpanded[static_cast<std::size_t>(19) * 64 + 19] < 0.0f, "expanded solid cell is negative");
}
}

int main()
{
    TestRoundTrip();
    TestDeterminism();
    TestRejectsCorruption();
    TestExpandToField();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d sdf volume test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_volume: OK\n");
    return 0;
}
