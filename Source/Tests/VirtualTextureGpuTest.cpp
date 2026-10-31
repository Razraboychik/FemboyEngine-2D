// FemboyEngine2D - virtual texture GPU bridge tests: tile mapping and the Null
// backend lifecycle (no device required).
#include "Engine/RHI/VirtualTextureGpu.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <span>

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

struct UploadFixture
{
    std::array<std::byte, 64 * 64 * 4> Pixels{};
    Fby::Rhi::VirtualTextureTileUpload Upload{};

    UploadFixture()
    {
        Upload.layer = 2;
        Upload.originX = 320;
        Upload.originY = 64;
        Upload.width = 64;
        Upload.height = 64;
        Upload.rowPitch = 64 * 4;
        Upload.pixels = Pixels;
    }
};

void TestTileMapping()
{
    using namespace Fby::Rhi;
    UploadFixture ScopedFixture;
    VirtualTexturePageLocation ScopedLocation;
    FBY_CHECK(MapVirtualTextureTile(ScopedFixture.Upload, 64, 32, &ScopedLocation));
    FBY_CHECK(ScopedLocation.SlotX == 5);
    FBY_CHECK(ScopedLocation.SlotY == 1);

    // Unaligned origin is rejected: slot addressing would silently drift.
    ScopedFixture.Upload.originX = 100;
    FBY_CHECK(!MapVirtualTextureTile(ScopedFixture.Upload, 64, 32, &ScopedLocation));

    // A row pitch smaller than the tile width is rejected.
    ScopedFixture.Upload.originX = 320;
    ScopedFixture.Upload.rowPitch = 10;
    FBY_CHECK(!MapVirtualTextureTile(ScopedFixture.Upload, 64, 32, &ScopedLocation));

    // Pixel payload smaller than rowPitch * height is rejected.
    ScopedFixture.Upload.rowPitch = 64 * 4;
    ScopedFixture.Upload.pixels = std::span<const std::byte>(ScopedFixture.Pixels.data(), 100);
    FBY_CHECK(!MapVirtualTextureTile(ScopedFixture.Upload, 64, 32, &ScopedLocation));
    ScopedFixture.Upload.pixels = ScopedFixture.Pixels;

    // Slot outside the page grid is rejected.
    ScopedFixture.Upload.originX = 32 * 64;
    FBY_CHECK(!MapVirtualTextureTile(ScopedFixture.Upload, 64, 32, &ScopedLocation));

    // A tile larger than the configured tile size is rejected.
    ScopedFixture.Upload.originX = 0;
    ScopedFixture.Upload.width = 128;
    FBY_CHECK(!MapVirtualTextureTile(ScopedFixture.Upload, 64, 32, &ScopedLocation));

    // Zero tile size / grid is rejected instead of dividing by zero.
    FBY_CHECK(!MapVirtualTextureTile(ScopedFixture.Upload, 0, 32, &ScopedLocation));
    FBY_CHECK(!MapVirtualTextureTile(ScopedFixture.Upload, 64, 0, &ScopedLocation));
}

void TestNullLifecycle()
{
    using namespace Fby::Rhi;
    DeviceDesc ScopedDesc;
    ScopedDesc.backend = Backend::Null;
    ScopedDesc.nativeWindow = nullptr;
    std::unique_ptr<RHI> ScopedRhi = RHI::Create(ScopedDesc);
    FBY_CHECK(ScopedRhi != nullptr);
    if (ScopedRhi == nullptr)
    {
        return;
    }
    FBY_CHECK(ScopedRhi->GetBackend() == Backend::Null);

    VirtualTextureResourceDesc ScopedVirtual;
    ScopedVirtual.layerCount = 2;
    ScopedVirtual.pageSize = 128;
    ScopedVirtual.tileSize = 64;
    ScopedVirtual.pageTableSize = 64;
    const VirtualTextureHandle ScopedHandle = ScopedRhi->CreateVirtualTexture(ScopedVirtual);
    FBY_CHECK(!ScopedHandle.IsValid());

    const std::array<std::uint32_t, 1> ScopedEntry{0xDEADBEEFu};
    FBY_CHECK(!ScopedRhi->UpdateVirtualTexturePageTable(ScopedHandle, 1, 1, ScopedEntry));

    UploadFixture ScopedFixture;
    FBY_CHECK(!ScopedRhi->UploadVirtualTexturePage(ScopedHandle, 0, 0, 0, 64, 64, 64 * 4, ScopedFixture.Pixels));

    // The bridge must refuse an invalid handle before touching the backend.
    VirtualTextureRhiTarget ScopedTarget{ScopedRhi.get(), ScopedHandle, 64, 2};
    FBY_CHECK(!UploadVirtualTextureTileToRhi(&ScopedTarget, ScopedFixture.Upload));
    FBY_CHECK(!UploadVirtualTextureTileToRhi(nullptr, ScopedFixture.Upload));

    // A valid handle on a null renderer is refused as well.
    VirtualTextureHandle ScopedFake{1};
    VirtualTextureRhiTarget ScopedNoRenderer{nullptr, ScopedFake, 64, 2};
    FBY_CHECK(!UploadVirtualTextureTileToRhi(&ScopedNoRenderer, ScopedFixture.Upload));

    ScopedRhi->DestroyVirtualTexture(ScopedHandle);
    ScopedRhi->Shutdown();
}

void TestSampleMath()
{
    using namespace Fby::Rhi;
    using Fby::Vector2D;
    // The shader compares the instance flag against 1u << 5.
    FBY_CHECK(kRenderFlagVirtualTexture == (1u << 5u));
    // 4x4 table, one resident tile at cell (1, 1) -> layer 2, slot 37.
    std::array<std::uint32_t, 16> ScopedTable{};
    for (std::uint32_t& ScopedEntry : ScopedTable)
    {
        ScopedEntry = kVirtualTexturePageTableMissing;
    }
    ScopedTable[1 * 4 + 1] = PackVirtualTexturePageTable(2, 37);

    constexpr float kTileWorld = 256.0f;
    constexpr std::uint32_t kTileSize = 64;
    constexpr std::uint32_t kPageSize = 2048;
    VirtualTextureSample ScopedSample;
    const Vector2D ScopedWorld{1.4f * kTileWorld, 1.6f * kTileWorld};
    FBY_CHECK(ResolveVirtualTextureSample(ScopedWorld, kTileWorld, Vector2D{0.0f, 0.0f}, 4, ScopedTable, kTileSize,
                                          kPageSize, &ScopedSample));
    FBY_CHECK(ScopedSample.Layer == 2);
    FBY_CHECK(ScopedSample.SlotX == 5);
    FBY_CHECK(ScopedSample.SlotY == 1);
    FBY_CHECK(std::fabs(ScopedSample.LocalU - 0.4f) < 0.001f);
    FBY_CHECK(std::fabs(ScopedSample.LocalV - 0.6f) < 0.001f);

    // Missing page (cell 0, 0) and outside the table are not resident.
    FBY_CHECK(!ResolveVirtualTextureSample(Vector2D{0.5f * kTileWorld, 0.5f * kTileWorld}, kTileWorld,
                                           Vector2D{0.0f, 0.0f}, 4, ScopedTable, kTileSize, kPageSize, nullptr));
    FBY_CHECK(!ResolveVirtualTextureSample(Vector2D{5.5f * kTileWorld, 0.5f * kTileWorld}, kTileWorld,
                                           Vector2D{0.0f, 0.0f}, 4, ScopedTable, kTileSize, kPageSize, nullptr));

    // Negative world coordinates land on the same grid as the CPU residency.
    FBY_CHECK(!ResolveVirtualTextureSample(Vector2D{-0.5f * kTileWorld, -0.5f * kTileWorld}, kTileWorld,
                                           Vector2D{0.0f, 0.0f}, 4, ScopedTable, kTileSize, kPageSize, nullptr));

    // A table origin shifts the lookup without changing the tile grid.
    FBY_CHECK(ResolveVirtualTextureSample(Vector2D{2.4f * kTileWorld, 2.6f * kTileWorld}, kTileWorld,
                                          Vector2D{1.0f, 1.0f}, 4, ScopedTable, kTileSize, kPageSize, &ScopedSample));
    FBY_CHECK(ScopedSample.Layer == 2);
    FBY_CHECK(ScopedSample.SlotX == 5);

    // Invalid layout parameters are rejected.
    FBY_CHECK(!ResolveVirtualTextureSample(ScopedWorld, 0.0f, Vector2D{0.0f, 0.0f}, 4, ScopedTable, kTileSize,
                                           kPageSize, nullptr));
    FBY_CHECK(!ResolveVirtualTextureSample(ScopedWorld, kTileWorld, Vector2D{0.0f, 0.0f}, 4, ScopedTable, 0,
                                           kPageSize, nullptr));
}

void TestFeedbackDedup()
{
    using namespace Fby::Rhi;
    std::array<std::uint32_t, 8> ScopedTexels{};
    ScopedTexels[0] = PackVirtualTextureFeedback(1, 2);
    ScopedTexels[1] = PackVirtualTextureFeedback(1, 2);
    ScopedTexels[2] = kVirtualTextureFeedbackEmpty;
    ScopedTexels[3] = PackVirtualTextureFeedback(-3, 4);
    ScopedTexels[4] = PackVirtualTextureFeedback(1, 2);
    ScopedTexels[5] = 0u;

    std::array<std::uint32_t, 4> ScopedUnique{};
    const std::uint32_t ScopedCount = DeduplicateVirtualTextureFeedback(ScopedTexels, ScopedUnique);
    FBY_CHECK(ScopedCount == 2);
    FBY_CHECK(ScopedUnique[0] == PackVirtualTextureFeedback(1, 2));
    FBY_CHECK(ScopedUnique[1] == PackVirtualTextureFeedback(-3, 4));

    // Output capacity limits the result instead of overflowing.
    std::array<std::uint32_t, 1> ScopedSmall{};
    FBY_CHECK(DeduplicateVirtualTextureFeedback(ScopedTexels, ScopedSmall) == 1);

    // Null backend reports no feedback.
    DeviceDesc ScopedDesc;
    ScopedDesc.backend = Backend::Null;
    std::unique_ptr<RHI> ScopedRhi = RHI::Create(ScopedDesc);
    FBY_CHECK(ScopedRhi != nullptr);
    if (ScopedRhi != nullptr)
    {
        std::array<std::uint32_t, 4> ScopedRead{};
        FBY_CHECK(ScopedRhi->ReadVirtualTextureFeedback(ScopedRead) == 0);
        ScopedRhi->Shutdown();
    }
}
}

int main()
{
    TestTileMapping();
    TestSampleMath();
    TestFeedbackDedup();
    TestNullLifecycle();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u virtual texture GPU test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("virtual texture GPU tests passed\n");
    return 0;
}
