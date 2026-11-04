// FemboyEngine2D - SDF brick atlas packing tests.
#include "Engine/Physics/SdfAtlas.hpp"
#include "Engine/Physics/SdfWorld.hpp"

#include <cmath>
#include <cstdio>

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

void TestAtlasMatchesBrickMap()
{
    Fby::SdfWorldDesc ScopedDesc;
    ScopedDesc.CellSize = Fby::Vector2D{4.0f, 4.0f};
    ScopedDesc.Width = 128;
    ScopedDesc.Height = 128;
    ScopedDesc.NarrowBand = 32.0f;
    ScopedDesc.OutsideDistance = 1000.0f;
    Fby::SdfWorld ScopedWorld;
    Check(ScopedWorld.Configure(ScopedDesc), "configure the atlas world");
    Check(ScopedWorld.AddBox(Fby::Vector2D{256.0f, 256.0f}, Fby::Vector2D{40.0f, 20.0f}), "add a box");
    Check(ScopedWorld.AddCircle(Fby::Vector2D{180.0f, 190.0f}, 24.0f), "add a circle");
    Check(ScopedWorld.BakeAll(), "bake the atlas world");

    Fby::SdfAtlasImage ScopedImage;
    Check(Fby::PackSdfAtlas(ScopedWorld.Bricks(), &ScopedImage), "packing succeeds");
    Check(ScopedImage.Columns > 0, "atlas has columns");
    Check(ScopedImage.Width == ScopedImage.Columns * Fby::SdfBrickSize, "atlas width matches the columns");
    Check(ScopedImage.Height == ScopedImage.Rows * Fby::SdfBrickSize, "atlas height matches the rows");
    Check(ScopedImage.Table.size() == ScopedWorld.Bricks().BrickCount(), "table covers every brick");
    Check(!ScopedImage.Texels.empty(), "atlas carries texels");

    const std::uint32_t ScopedNarrow = static_cast<std::uint32_t>(ScopedWorld.Bricks().NarrowBrickCount());
    bool ScopedTileIndicesValid = true;
    bool ScopedUniformBricksUntiled = true;
    for (const Fby::SdfBrickGpu& ScopedGpu : ScopedImage.Table)
    {
        const bool ScopedNarrowBrick = (ScopedGpu.StateAndFlags & 0x3u) ==
                                       static_cast<std::uint32_t>(Fby::SdfBrickState::Narrow);
        if (ScopedNarrowBrick && ScopedGpu.PayloadOffset >= ScopedNarrow)
        {
            ScopedTileIndicesValid = false;
        }
        if (!ScopedNarrowBrick && ScopedGpu.PayloadOffset != 0xFFFFFFFFu)
        {
            ScopedUniformBricksUntiled = false;
        }
    }
    Check(ScopedTileIndicesValid, "narrow bricks point inside the atlas");
    Check(ScopedUniformBricksUntiled, "empty and full bricks carry no tile");

    bool ScopedSamplesMatch = true;
    for (std::uint32_t ScopedY = 0; ScopedY < 64; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < 64; ++ScopedX)
        {
            const Fby::Vector2D ScopedPoint{static_cast<float>(ScopedX) * 8.0f + 0.37f,
                                            static_cast<float>(ScopedY) * 8.0f + 0.61f};
            const float ScopedAtlas = Fby::SampleSdfAtlas(ScopedImage, ScopedWorld.Bricks(), ScopedPoint);
            const float ScopedBricks = ScopedWorld.Bricks().Sample(ScopedPoint);
            if (std::abs(ScopedAtlas - ScopedBricks) > 1.0e-5f)
            {
                ScopedSamplesMatch = false;
            }
        }
    }
    Check(ScopedSamplesMatch, "atlas sampling matches the brick map");

    // The shipped table must agree with the GPU table the brick map produces,
    // apart from the tile index that replaces the payload offset.
    const std::vector<Fby::SdfBrickGpu> ScopedReference = ScopedWorld.Bricks().BuildGpuTable();
    bool ScopedConstantsMatch = true;
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedReference.size(); ++ScopedIndex)
    {
        if (ScopedReference[ScopedIndex].MinDistance != ScopedImage.Table[ScopedIndex].MinDistance ||
            ScopedReference[ScopedIndex].MaxDistance != ScopedImage.Table[ScopedIndex].MaxDistance ||
            ScopedReference[ScopedIndex].StateAndFlags != ScopedImage.Table[ScopedIndex].StateAndFlags ||
            ScopedReference[ScopedIndex].BrickX != ScopedImage.Table[ScopedIndex].BrickX ||
            ScopedReference[ScopedIndex].BrickY != ScopedImage.Table[ScopedIndex].BrickY)
        {
            ScopedConstantsMatch = false;
        }
    }
    Check(ScopedConstantsMatch, "atlas table keeps the brick metadata");
}
}

int main()
{
    TestAtlasMatchesBrickMap();
    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d sdf atlas test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("sdf_atlas: OK\n");
    return 0;
}
