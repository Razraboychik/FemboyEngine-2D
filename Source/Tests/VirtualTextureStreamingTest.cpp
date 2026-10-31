// FemboyEngine2D - virtual texture streaming test: feedback-driven requests,
// budgeted uploads, LRU eviction and batched page-table updates on synthetic
// chunks (no GPU).
#include "Engine/RHI/VirtualTextureFormat.hpp"
#include "Engine/RHI/VirtualTextureSystem.hpp"

#include "Engine/Assets/VFS.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
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

struct Sink
{
    std::uint32_t uploads = 0;
    std::uint32_t lastLayer = 0;
    std::uint32_t lastOriginX = 0;
    std::uint32_t lastOriginY = 0;
};

bool CountUpload(void* InUser, const Fby::Rhi::VirtualTextureTileUpload& InUpload) noexcept
{
    auto* ScopedSink = static_cast<Sink*>(InUser);
    ++ScopedSink->uploads;
    ScopedSink->lastLayer = InUpload.layer;
    ScopedSink->lastOriginX = InUpload.originX;
    ScopedSink->lastOriginY = InUpload.originY;
    return true;
}

void WriteChunk(const std::filesystem::path& InPath, std::uint32_t InTileSize, std::uint32_t InChunkTiles,
                std::uint8_t InSeed)
{
    const std::uint32_t ScopedChunkPixels = InTileSize * InChunkTiles;
    std::vector<std::byte> ScopedBytes(32 + static_cast<std::size_t>(ScopedChunkPixels) * ScopedChunkPixels * 4u);
    std::memcpy(ScopedBytes.data(), Fby::Rhi::kVirtualTextureChunkMagic, 8);
    const std::uint32_t ScopedValues[6] = {1u, InTileSize, InChunkTiles, 0u, 0u, 0u};
    std::memcpy(ScopedBytes.data() + 8, ScopedValues, sizeof(ScopedValues));
    for (std::uint32_t ScopedY = 0; ScopedY < ScopedChunkPixels; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < ScopedChunkPixels; ++ScopedX)
        {
            const std::size_t ScopedOffset = 32 + (static_cast<std::size_t>(ScopedY) * ScopedChunkPixels + ScopedX) * 4u;
            ScopedBytes[ScopedOffset + 0] = static_cast<std::byte>(InSeed + ScopedX);
            ScopedBytes[ScopedOffset + 1] = static_cast<std::byte>(InSeed + ScopedY);
            ScopedBytes[ScopedOffset + 2] = static_cast<std::byte>(InSeed ^ 0x5Au);
            ScopedBytes[ScopedOffset + 3] = std::byte{0xFF};
        }
    }
    std::ofstream ScopedFile(InPath, std::ios::binary | std::ios::trunc);
    ScopedFile.write(reinterpret_cast<const char*>(ScopedBytes.data()),
                     static_cast<std::streamsize>(ScopedBytes.size()));
}

// Camera path: the camera drags the view over a 4x4 tile area while the VT only
// has four slots per layer, so streaming must evict and reload.
void TestCameraStreaming()
{
    using namespace Fby::Rhi;
    const std::filesystem::path ScopedRoot = std::filesystem::current_path() / "fby_vt_stream_test";
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::create_directories(ScopedRoot, ScopedError);
    constexpr std::uint32_t kTileSize = 64;
    constexpr std::uint32_t kChunkTiles = 2;
    for (std::int64_t ScopedChunkY = -1; ScopedChunkY <= 1; ++ScopedChunkY)
    {
        for (std::int64_t ScopedChunkX = -1; ScopedChunkX <= 1; ++ScopedChunkX)
        {
            char ScopedName[64]{};
            std::snprintf(ScopedName, sizeof(ScopedName), "chunk_%lld_%lld.fvtc",
                          static_cast<long long>(ScopedChunkX), static_cast<long long>(ScopedChunkY));
            WriteChunk(ScopedRoot / ScopedName, kTileSize, kChunkTiles,
                       static_cast<std::uint8_t>(ScopedChunkX * 7 + ScopedChunkY * 13));
        }
    }

    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedVfsError;
    FBY_CHECK(ScopedVfs.MountDirectory(ScopedRoot.string(), 0, &ScopedVfsError));

    VirtualTextureDesc ScopedDesc;
    ScopedDesc.layerCount = 1;
    ScopedDesc.pageSize = 128;
    ScopedDesc.tileSize = kTileSize;
    ScopedDesc.chunkTiles = kChunkTiles;
    ScopedDesc.worldTileSize = 256.0f;

    Sink ScopedSink;
    auto ScopedVirtual = std::make_unique<VirtualTextureSystem>();
    FBY_CHECK(ScopedVirtual->Initialize(ScopedDesc, ScopedVfs, ".", &CountUpload, &ScopedSink));
    ScopedVirtual->SetMaxLoadsPerUpdate(2);
    FBY_CHECK(ScopedVirtual->MaxLoadsPerUpdate() == 2);

    std::array<VirtualTextureTileId, 9> ScopedRequests{};
    std::uint32_t ScopedMaxResident = 0;
    std::uint32_t ScopedMaxUploadsPerFrame = 0;
    std::uint32_t ScopedUploads = 0;
    std::uint32_t ScopedTableResident = 0;
    std::uint32_t ScopedTableMissing = 0;

    // Sweep the camera centre across tiles (0,0) .. (3,3) twice.
    for (std::uint32_t ScopedStep = 0; ScopedStep < 60; ++ScopedStep)
    {
        const float ScopedPhase = static_cast<float>(ScopedStep % 30) / 29.0f;
        const Fby::Vector2D ScopedCenter{(0.5f + ScopedPhase * 3.0f) * 256.0f,
                                         (0.5f + static_cast<float>((ScopedStep / 30)) * 3.0f) * 256.0f};
        ScopedVirtual->BeginFrame();
        ScopedVirtual->SetFocus(ScopedCenter);
        const std::int32_t ScopedTileX = static_cast<std::int32_t>(std::floor(ScopedCenter.X / 256.0f));
        const std::int32_t ScopedTileY = static_cast<std::int32_t>(std::floor(ScopedCenter.Y / 256.0f));
        std::uint32_t ScopedCount = 0;
        for (std::int32_t ScopedY = -1; ScopedY <= 1; ++ScopedY)
        {
            for (std::int32_t ScopedX = -1; ScopedX <= 1; ++ScopedX)
            {
                ScopedRequests[ScopedCount++] = VirtualTextureTileId{ScopedTileX + ScopedX, ScopedTileY + ScopedY};
            }
        }
        ScopedVirtual->RequestTiles(ScopedRequests);
        ScopedVirtual->ClearPendingTableUpdates();

        ScopedMaxResident = std::max(ScopedMaxResident, ScopedVirtual->Stats().residentTiles);
        ScopedMaxUploadsPerFrame = std::max(ScopedMaxUploadsPerFrame, ScopedVirtual->Stats().uploads);
        ScopedUploads += ScopedVirtual->Stats().uploads;
        for (const VirtualTextureTableUpdate& ScopedUpdate : ScopedVirtual->PendingTableUpdates())
        {
            (void)ScopedUpdate;
        }
    }
    FBY_CHECK(ScopedVirtual->ResidentTileCount() <= 4);
    FBY_CHECK(ScopedMaxResident <= 4);
    FBY_CHECK(ScopedMaxUploadsPerFrame <= 2);
    FBY_CHECK(ScopedUploads > 0);
    FBY_CHECK(ScopedSink.uploads > 0);
    FBY_CHECK(ScopedSink.lastOriginX % kTileSize == 0);
    FBY_CHECK(ScopedSink.lastOriginY % kTileSize == 0);

    // Warm state: requesting the same tiles again must not upload anything and
    // must not miss in the VFS.
    const Fby::Vector2D ScopedCenter{0.5f * 256.0f, 0.5f * 256.0f};
    ScopedVirtual->BeginFrame();
    ScopedVirtual->SetFocus(ScopedCenter);
    ScopedRequests[0] = VirtualTextureTileId{0, 0};
    ScopedVirtual->RequestTiles(std::span<const VirtualTextureTileId>(ScopedRequests.data(), 1));
    for (std::uint32_t ScopedI = 0; ScopedI < 3; ++ScopedI)
    {
        ScopedVirtual->BeginFrame();
        ScopedVirtual->RequestTiles(std::span<const VirtualTextureTileId>(ScopedRequests.data(), 1));
    }
    FBY_CHECK(ScopedVirtual->Stats().uploads == 0);
    FBY_CHECK(ScopedVirtual->Stats().vfsMisses == 0);
    FBY_CHECK(ScopedVirtual->Stats().residentTiles <= 4);

    // Table batches: loads produce resident entries, evictions the sentinel.
    ScopedVirtual->BeginFrame();
    ScopedRequests[0] = VirtualTextureTileId{9, 9};
    ScopedVirtual->RequestTiles(std::span<const VirtualTextureTileId>(ScopedRequests.data(), 1));
    for (const VirtualTextureTableUpdate& ScopedUpdate : ScopedVirtual->PendingTableUpdates())
    {
        if (ScopedUpdate.Entry == kVirtualTexturePageTableMissing)
        {
            ++ScopedTableMissing;
        }
        else
        {
            ++ScopedTableResident;
            std::uint32_t ScopedLayer = 0;
            std::uint32_t ScopedSlot = 0;
            FBY_CHECK(UnpackVirtualTexturePageTable(ScopedUpdate.Entry, &ScopedLayer, &ScopedSlot));
            FBY_CHECK(ScopedLayer < ScopedDesc.layerCount);
            FBY_CHECK(ScopedSlot < VirtualTextureSlotsPerLayer(ScopedDesc.pageSize, ScopedDesc.tileSize));
        }
    }
    FBY_CHECK(ScopedUploads + ScopedTableResident >= 1);
    FBY_CHECK(ScopedTableMissing >= 1);

    // The camera-driven path also streams and batches table updates.
    ScopedVirtual->ClearPendingTableUpdates();
    ScopedVirtual->SetMaxLoadsPerUpdate(16);
    ScopedVirtual->Update(Fby::Vector2D{1.5f * 256.0f, 1.5f * 256.0f}, Fby::Vector2D{256.0f, 256.0f}, 1.0f);
    FBY_CHECK(ScopedVirtual->Stats().uploads > 0);
    FBY_CHECK(!ScopedVirtual->PendingTableUpdates().empty());

    ScopedVirtual->Shutdown();
    std::filesystem::remove_all(ScopedRoot, ScopedError);
}

void TestDeterminism()
{
    using namespace Fby::Rhi;
    const std::filesystem::path ScopedRoot = std::filesystem::current_path() / "fby_vt_determinism_test";
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::create_directories(ScopedRoot, ScopedError);
    for (std::int64_t ScopedChunkY = -1; ScopedChunkY <= 1; ++ScopedChunkY)
    {
        for (std::int64_t ScopedChunkX = -1; ScopedChunkX <= 1; ++ScopedChunkX)
        {
            char ScopedName[64]{};
            std::snprintf(ScopedName, sizeof(ScopedName), "chunk_%lld_%lld.fvtc",
                          static_cast<long long>(ScopedChunkX), static_cast<long long>(ScopedChunkY));
            WriteChunk(ScopedRoot / ScopedName, 64, 2, 0);
        }
    }

    auto Run = [&ScopedRoot](std::array<VirtualTextureAddress, 9>* OutAddresses)
    {
        using namespace Fby::Rhi;
        Fby::VirtualFileSystem ScopedVfs;
        std::string ScopedErrorText;
        (void)ScopedVfs.MountDirectory(ScopedRoot.string(), 0, &ScopedErrorText);
        VirtualTextureDesc ScopedDesc;
        ScopedDesc.layerCount = 2;
        ScopedDesc.pageSize = 128;
        ScopedDesc.tileSize = 64;
        ScopedDesc.chunkTiles = 2;
        ScopedDesc.worldTileSize = 256.0f;
        Sink ScopedSink;
        // The system holds several megabytes of residency tables: keep it on the heap.
        auto ScopedVirtual = std::make_unique<VirtualTextureSystem>();
        (void)ScopedVirtual->Initialize(ScopedDesc, ScopedVfs, ".", &CountUpload, &ScopedSink);
        ScopedVirtual->SetMaxLoadsPerUpdate(8);
        std::array<VirtualTextureTileId, 9> ScopedRequests{};
        std::uint32_t ScopedCount = 0;
        for (std::int32_t ScopedY = -1; ScopedY <= 1; ++ScopedY)
        {
            for (std::int32_t ScopedX = -1; ScopedX <= 1; ++ScopedX)
            {
                ScopedRequests[ScopedCount++] = VirtualTextureTileId{ScopedX, ScopedY};
            }
        }
        ScopedVirtual->BeginFrame();
        ScopedVirtual->SetFocus(Fby::Vector2D{0.0f, 0.0f});
        ScopedVirtual->RequestTiles(ScopedRequests);
        ScopedCount = 0;
        for (std::int32_t ScopedY = -1; ScopedY <= 1; ++ScopedY)
        {
            for (std::int32_t ScopedX = -1; ScopedX <= 1; ++ScopedX)
            {
                (*OutAddresses)[ScopedCount++] = ScopedVirtual->Resolve(ScopedX, ScopedY);
            }
        }
        ScopedVirtual->Shutdown();
    };

    std::array<VirtualTextureAddress, 9> ScopedFirst{};
    std::array<VirtualTextureAddress, 9> ScopedSecond{};
    Run(&ScopedFirst);
    Run(&ScopedSecond);
    for (std::size_t ScopedI = 0; ScopedI < ScopedFirst.size(); ++ScopedI)
    {
        FBY_CHECK(ScopedFirst[ScopedI].resident == ScopedSecond[ScopedI].resident);
        FBY_CHECK(ScopedFirst[ScopedI].layer == ScopedSecond[ScopedI].layer);
        FBY_CHECK(ScopedFirst[ScopedI].slotX == ScopedSecond[ScopedI].slotX);
        FBY_CHECK(ScopedFirst[ScopedI].slotY == ScopedSecond[ScopedI].slotY);
    }
    FBY_CHECK(ScopedFirst[4].resident);
    std::filesystem::remove_all(ScopedRoot, ScopedError);
}

void TestStress()
{
    using namespace Fby::Rhi;
    const std::filesystem::path ScopedRoot = std::filesystem::current_path() / "fby_vt_stress_test";
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::create_directories(ScopedRoot, ScopedError);
    for (std::int64_t ScopedChunkY = 0; ScopedChunkY <= 4; ++ScopedChunkY)
    {
        for (std::int64_t ScopedChunkX = 0; ScopedChunkX <= 4; ++ScopedChunkX)
        {
            char ScopedName[64]{};
            std::snprintf(ScopedName, sizeof(ScopedName), "chunk_%lld_%lld.fvtc",
                          static_cast<long long>(ScopedChunkX), static_cast<long long>(ScopedChunkY));
            WriteChunk(ScopedRoot / ScopedName, 64, 2, 3);
        }
    }
    Fby::VirtualFileSystem ScopedVfs;
    std::string ScopedVfsError;
    FBY_CHECK(ScopedVfs.MountDirectory(ScopedRoot.string(), 0, &ScopedVfsError));
    VirtualTextureDesc ScopedDesc;
    ScopedDesc.layerCount = 2;
    ScopedDesc.pageSize = 128;
    ScopedDesc.tileSize = 64;
    ScopedDesc.chunkTiles = 2;
    ScopedDesc.worldTileSize = 256.0f;
    Sink ScopedSink;
    auto ScopedVirtual = std::make_unique<VirtualTextureSystem>();
    FBY_CHECK(ScopedVirtual->Initialize(ScopedDesc, ScopedVfs, ".", &CountUpload, &ScopedSink));
    ScopedVirtual->SetMaxLoadsPerUpdate(2);

    // Two cameras ping-pong every frame, with a fast zoom sweep on the CPU path.
    std::array<VirtualTextureTileId, 9> ScopedRequests{};
    for (std::uint32_t ScopedFrame = 0; ScopedFrame < 120; ++ScopedFrame)
    {
        const bool ScopedSide = (ScopedFrame / 6) % 2 == 0;
        const Fby::Vector2D ScopedCenter = ScopedSide ? Fby::Vector2D{1.5f * 256.0f, 1.5f * 256.0f}
                                                      : Fby::Vector2D{6.5f * 256.0f, 1.5f * 256.0f};
        const float ScopedZoom = 0.3f + 2.2f * (static_cast<float>(ScopedFrame % 17) / 16.0f);
        ScopedVirtual->BeginFrame();
        ScopedVirtual->SetFocus(ScopedCenter);
        std::uint32_t ScopedCount = 0;
        const std::int32_t ScopedTileX = static_cast<std::int32_t>(ScopedCenter.X / 256.0f);
        const std::int32_t ScopedTileY = static_cast<std::int32_t>(ScopedCenter.Y / 256.0f);
        for (std::int32_t ScopedY = -1; ScopedY <= 1; ++ScopedY)
        {
            for (std::int32_t ScopedX = -1; ScopedX <= 1; ++ScopedX)
            {
                ScopedRequests[ScopedCount++] = VirtualTextureTileId{ScopedTileX + ScopedX, ScopedTileY + ScopedY};
            }
        }
        ScopedVirtual->RequestTiles(ScopedRequests);
        FBY_CHECK(ScopedVirtual->Stats().uploads <= 2);
        FBY_CHECK(ScopedVirtual->ResidentTileCount() <= 8);
        ScopedVirtual->ClearPendingTableUpdates();
        // Fast zoom on the camera-driven path must not blow up the request loop.
        ScopedVirtual->Update(ScopedCenter, Fby::Vector2D{2400.0f, 1400.0f}, ScopedZoom, 0.7f);
        FBY_CHECK(ScopedVirtual->ResidentTileCount() <= 8);
        FBY_CHECK(ScopedVirtual->Stats().visibleTiles <= kVirtualTextureMaxVisibleTiles);
    }

    // Empty chunks: far outside the generated area, the stream fails softly.
    ScopedVirtual->BeginFrame();
    ScopedVirtual->SetFocus(Fby::Vector2D{60.0f * 256.0f, 60.0f * 256.0f});
    std::array<VirtualTextureTileId, 1> ScopedEmpty{VirtualTextureTileId{60, 60}};
    ScopedVirtual->RequestTiles(ScopedEmpty);
    FBY_CHECK(ScopedVirtual->Stats().vfsMisses == 1);
    FBY_CHECK(ScopedVirtual->ResidentTileCount() <= 8);
    FBY_CHECK(!ScopedVirtual->Resolve(60, 60).resident);

    ScopedVirtual->Shutdown();
    std::filesystem::remove_all(ScopedRoot, ScopedError);
}
}

int main()
{
    TestCameraStreaming();
    TestDeterminism();
    TestStress();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u virtual texture streaming test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("virtual texture streaming tests passed\n");
    return 0;
}
