// FemboyEngine2D - virtual texture address contract tests (no GPU).
#include "Engine/RHI/VirtualTextureFormat.hpp"

#include <cstdio>
#include <cstdint>

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

void TestPageTable()
{
    using namespace Fby::Rhi;
    std::uint32_t ScopedLayer = 0;
    std::uint32_t ScopedSlot = 0;

    const std::uint32_t ScopedEntry = PackVirtualTexturePageTable(3, 37);
    FBY_CHECK(UnpackVirtualTexturePageTable(ScopedEntry, &ScopedLayer, &ScopedSlot));
    FBY_CHECK(ScopedLayer == 3);
    FBY_CHECK(ScopedSlot == 37);

    FBY_CHECK(!UnpackVirtualTexturePageTable(kVirtualTexturePageTableMissing, &ScopedLayer, &ScopedSlot));
    FBY_CHECK(PackVirtualTexturePageTable(256, 0) == kVirtualTexturePageTableMissing);
    FBY_CHECK(PackVirtualTexturePageTable(0, 0x10000) == kVirtualTexturePageTableMissing);
    FBY_CHECK(PackVirtualTexturePageTable(255, 0xFFFF) == 0x00FFFFFFu);
    FBY_CHECK(UnpackVirtualTexturePageTable(0x01000000u, &ScopedLayer, &ScopedSlot) == false);
}

void TestSlotGrid()
{
    using namespace Fby::Rhi;
    FBY_CHECK(VirtualTextureTilesPerAxis(2048, 64) == 32);
    FBY_CHECK(VirtualTextureTilesPerAxis(2048, 0) == 0);
    FBY_CHECK(VirtualTextureSlotsPerLayer(2048, 64) == 1024);
    FBY_CHECK(VirtualTextureSlotsPerLayer(1024, 64) == 256);

    const std::uint32_t ScopedAxis = 32;
    FBY_CHECK(VirtualTextureSlotFromCell(ScopedAxis, 5, 1) == 37);
    FBY_CHECK(VirtualTextureSlotFromCell(ScopedAxis, 31, 31) == 1023);
    FBY_CHECK(VirtualTextureSlotFromCell(ScopedAxis, 32, 0) == 0x10000u);

    std::uint32_t ScopedX = 0;
    std::uint32_t ScopedY = 0;
    FBY_CHECK(VirtualTextureCellFromSlot(ScopedAxis, 37, &ScopedX, &ScopedY));
    FBY_CHECK(ScopedX == 5 && ScopedY == 1);
    FBY_CHECK(!VirtualTextureCellFromSlot(ScopedAxis, 1024, &ScopedX, &ScopedY));

    FBY_CHECK(VirtualTextureSlotOriginX(64, ScopedAxis, 37) == 320);
    FBY_CHECK(VirtualTextureSlotOriginY(64, ScopedAxis, 37) == 64);
    FBY_CHECK(VirtualTextureSlotOriginX(64, ScopedAxis, 1023) == 1984);
    FBY_CHECK(VirtualTextureSlotOriginY(64, ScopedAxis, 1023) == 1984);

    // Every slot round-trips through the cell decomposition.
    bool ScopedAllSlots = true;
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < 1024; ++ScopedSlot)
    {
        std::uint32_t ScopedCellX = 0;
        std::uint32_t ScopedCellY = 0;
        ScopedAllSlots = ScopedAllSlots && VirtualTextureCellFromSlot(ScopedAxis, ScopedSlot, &ScopedCellX, &ScopedCellY) &&
                         VirtualTextureSlotFromCell(ScopedAxis, ScopedCellX, ScopedCellY) == ScopedSlot;
    }
    FBY_CHECK(ScopedAllSlots);
}

void TestWorldTiles()
{
    using namespace Fby::Rhi;
    constexpr float kTileWorld = 256.0f;
    FBY_CHECK(VirtualTextureWorldToTile(0.0f, kTileWorld) == 0);
    FBY_CHECK(VirtualTextureWorldToTile(255.9f, kTileWorld) == 0);
    FBY_CHECK(VirtualTextureWorldToTile(256.0f, kTileWorld) == 1);
    FBY_CHECK(VirtualTextureWorldToTile(-1.0f, kTileWorld) == -1);
    FBY_CHECK(VirtualTextureWorldToTile(-256.0f, kTileWorld) == -1);
    FBY_CHECK(VirtualTextureWorldToTile(-257.0f, kTileWorld) == -2);
    FBY_CHECK(VirtualTextureWorldToTile(5.0f, 0.0f) == 0);

    FBY_CHECK(VirtualTextureTileToWorld(3, kTileWorld) == 768.0f);
    FBY_CHECK(VirtualTextureTileToWorld(-2, kTileWorld) == -512.0f);

    FBY_CHECK(VirtualTextureTileLocalUv(384.0f, kTileWorld) == 0.5f);
    FBY_CHECK(VirtualTextureTileLocalUv(-1.0f, kTileWorld) > 0.99f);
    FBY_CHECK(VirtualTextureTileLocalUv(0.0f, kTileWorld) == 0.0f);
}

void TestLayerPrediction()
{
    using namespace Fby::Rhi;
    // The hash must be stable and stay inside the layer count.
    bool ScopedMatches = true;
    bool ScopedStable = true;
    for (std::int64_t ScopedY = -3; ScopedY <= 3; ++ScopedY)
    {
        for (std::int64_t ScopedX = -3; ScopedX <= 3; ++ScopedX)
        {
            const std::uint32_t ScopedLayer = VirtualTextureLayerForTile(ScopedX, ScopedY, 8);
            ScopedMatches = ScopedMatches && ScopedLayer < 8;
            ScopedStable = ScopedStable && ScopedLayer == VirtualTextureLayerForTile(ScopedX, ScopedY, 8);
        }
    }
    FBY_CHECK(ScopedMatches);
    FBY_CHECK(ScopedStable);
    FBY_CHECK(VirtualTextureTileHash(0, 0) != VirtualTextureTileHash(1, 0));
    FBY_CHECK(VirtualTextureTileHash(-1, -1) != VirtualTextureTileHash(1, 1));
    FBY_CHECK(VirtualTextureLayerForTile(5, 5, 0) == 0);
}

void TestFeedback()
{
    using namespace Fby::Rhi;
    std::int32_t ScopedTileX = 0;
    std::int32_t ScopedTileY = 0;
    FBY_CHECK(UnpackVirtualTextureFeedback(PackVirtualTextureFeedback(0, 0), &ScopedTileX, &ScopedTileY));
    FBY_CHECK(ScopedTileX == 0 && ScopedTileY == 0);

    FBY_CHECK(UnpackVirtualTextureFeedback(PackVirtualTextureFeedback(-7, 12), &ScopedTileX, &ScopedTileY));
    FBY_CHECK(ScopedTileX == -7 && ScopedTileY == 12);

    FBY_CHECK(UnpackVirtualTextureFeedback(PackVirtualTextureFeedback(100000, -100000), &ScopedTileX, &ScopedTileY));
    FBY_CHECK(ScopedTileX == kVirtualTextureMaxFeedbackTile);
    FBY_CHECK(ScopedTileY == -kVirtualTextureMaxFeedbackTile);

    FBY_CHECK(!UnpackVirtualTextureFeedback(kVirtualTextureFeedbackEmpty, &ScopedTileX, &ScopedTileY));

    // Valid pairs never collide with the empty sentinel.
    bool ScopedNoCollision = true;
    const std::int32_t ScopedSamples[6] = {-16383, -1, 0, 1, 16383, 1024};
    for (const std::int32_t ScopedX : ScopedSamples)
    {
        for (const std::int32_t ScopedY : ScopedSamples)
        {
            ScopedNoCollision = ScopedNoCollision && PackVirtualTextureFeedback(ScopedX, ScopedY) != kVirtualTextureFeedbackEmpty;
        }
    }
    FBY_CHECK(ScopedNoCollision);
}
}

int main()
{
    TestPageTable();
    TestSlotGrid();
    TestWorldTiles();
    TestLayerPrediction();
    TestFeedback();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u virtual texture format test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("virtual texture format tests passed\n");
    return 0;
}
