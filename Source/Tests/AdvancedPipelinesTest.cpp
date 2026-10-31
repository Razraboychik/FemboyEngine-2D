// FemboyEngine2D - advanced AAA pipelines acceptance test.
#include "Engine/Audio/AudioZones.hpp"
#include "Engine/Audio/DspGraph.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/ECS/ChangeMask.hpp"
#include "Engine/Network/NetRollback.hpp"
#include "Engine/Network/RelevanceGrid.hpp"
#include "Engine/RHI/ChangeMaskExtract.hpp"
#include "Engine/RHI/VirtualTextureSystem.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
int GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

constexpr std::uint32_t kTileSize = Fby::Rhi::kVirtualTextureTileSize;

void WriteChunkFile(const std::filesystem::path& InPath, std::uint32_t InTileSize, std::uint32_t InChunkTiles,
                    std::uint8_t InSeed)
{
    const std::uint32_t ScopedChunkPixels = InTileSize * InChunkTiles;
    std::vector<std::byte> ScopedBytes(sizeof(Fby::Rhi::kVirtualTextureChunkHeaderBytes) + 0, std::byte{0});
    ScopedBytes.resize(sizeof(std::uint64_t) + 6u * sizeof(std::uint32_t) + ScopedChunkPixels * ScopedChunkPixels * 4u);
    std::memcpy(ScopedBytes.data(), Fby::Rhi::kVirtualTextureChunkMagic, 8);
    const std::uint32_t ScopedValues[6] = {1u, InTileSize, InChunkTiles, 0u, 0u, 0u};
    std::memcpy(ScopedBytes.data() + 8, ScopedValues, sizeof(ScopedValues));
    std::byte* ScopedPixels = ScopedBytes.data() + 32;
    for (std::uint32_t ScopedY = 0; ScopedY < ScopedChunkPixels; ++ScopedY)
    {
        for (std::uint32_t ScopedX = 0; ScopedX < ScopedChunkPixels; ++ScopedX)
        {
            const std::uint32_t ScopedOffset = (ScopedY * ScopedChunkPixels + ScopedX) * 4u;
            ScopedPixels[ScopedOffset + 0] = static_cast<std::byte>(InSeed + ScopedX);
            ScopedPixels[ScopedOffset + 1] = static_cast<std::byte>(InSeed + ScopedY);
            ScopedPixels[ScopedOffset + 2] = static_cast<std::byte>(InSeed ^ 0x5Au);
            ScopedPixels[ScopedOffset + 3] = std::byte{0xFF};
        }
    }
    std::ofstream ScopedFile(InPath, std::ios::binary | std::ios::trunc);
    ScopedFile.write(reinterpret_cast<const char*>(ScopedBytes.data()),
                     static_cast<std::streamsize>(ScopedBytes.size()));
}

struct SinkState
{
    std::uint32_t uploads = 0;
    std::uint64_t bytes = 0;
    std::uint64_t checksum = 1469598103934665603ull;
};

bool SinkTile(void* InUser, const Fby::Rhi::VirtualTextureTileUpload& InUpload) noexcept
{
    auto* ScopedState = static_cast<SinkState*>(InUser);
    if (InUpload.width != kTileSize || InUpload.height != kTileSize || InUpload.pixels.empty())
    {
        return false;
    }
    for (std::uint32_t ScopedRow = 0; ScopedRow < InUpload.height; ++ScopedRow)
    {
        const std::byte* ScopedRowBytes =
            InUpload.pixels.data() + static_cast<std::size_t>(ScopedRow) * InUpload.rowPitch;
        for (std::uint32_t ScopedByte = 0; ScopedByte < InUpload.width * 4u; ++ScopedByte)
        {
            ScopedState->checksum ^=
                static_cast<std::uint64_t>(std::to_integer<unsigned char>(ScopedRowBytes[ScopedByte]));
            ScopedState->checksum *= 1099511628211ull;
        }
    }
    ++ScopedState->uploads;
    ScopedState->bytes += InUpload.pixels.size();
    return true;
}

void TestVirtualTexture()
{
    namespace fs = std::filesystem;
    const fs::path ScopedRoot = fs::current_path() / "_fby_virtual_texture_test";
    std::error_code ScopedError;
    fs::remove_all(ScopedRoot, ScopedError);
    fs::create_directories(ScopedRoot / "vt");
    WriteChunkFile(ScopedRoot / "vt" / "chunk_0_0.fvtc", kTileSize, 4, 7);
    WriteChunkFile(ScopedRoot / "vt" / "chunk_2_2.fvtc", kTileSize, 4, 9);

    {
        Fby::VirtualFileSystem ScopedVfs;
        FBY_CHECK(ScopedVfs.MountDirectory(ScopedRoot.string(), 0));

        SinkState ScopedSink;
        Fby::Rhi::VirtualTextureDesc ScopedDesc;
        ScopedDesc.layerCount = 2;
        ScopedDesc.pageSize = 256;
        ScopedDesc.tileSize = 64;
        ScopedDesc.chunkTiles = 4;
        ScopedDesc.worldTileSize = 64.0f;

        {
            // The page table and the 64k-entry resident hash are ~2 MiB. A shipping
            auto ScopedVirtual = std::make_unique<Fby::Rhi::VirtualTextureSystem>();
            FBY_CHECK(ScopedVirtual->Initialize(ScopedDesc, ScopedVfs, "vt", &SinkTile, &ScopedSink));
            FBY_CHECK(ScopedVirtual->VramBudgetBytes() == 2ull * 256ull * 256ull * 4ull);

            const Fby::Rhi::VirtualTextureCamera ScopedCamera{{128.0f, 128.0f}, {256.0f, 256.0f}, 1.0f, 0.0f};
            ScopedVirtual->Update(ScopedCamera);
            FBY_CHECK(ScopedVirtual->Stats().visibleTiles == 16);
            FBY_CHECK(ScopedSink.uploads == 16);
            FBY_CHECK(ScopedVirtual->ResidentTileCount() == 16);
            const Fby::Rhi::VirtualTextureAddress ScopedAddress = ScopedVirtual->Resolve(0, 0);
            FBY_CHECK(ScopedAddress.resident);
            FBY_CHECK(ScopedAddress.layer < 2);
            FBY_CHECK(ScopedAddress.slotX < 4 && ScopedAddress.slotY < 4);

            ScopedVirtual->Update(ScopedCamera);
            FBY_CHECK(ScopedSink.uploads == 16);
            FBY_CHECK(ScopedVirtual->Stats().uploads == 0);

            const Fby::Rhi::VirtualTextureCamera ScopedMoved{{640.0f, 640.0f}, {256.0f, 256.0f}, 1.0f, 0.0f};
            ScopedVirtual->Update(ScopedMoved);
            FBY_CHECK(ScopedVirtual->Stats().visibleTiles == 16);
            FBY_CHECK(ScopedVirtual->Stats().uploads > 0);
            FBY_CHECK(ScopedVirtual->ResidentTileCount() <= 2 * 16);
            FBY_CHECK(ScopedVirtual->Stats().vramBudgetBytes == ScopedVirtual->VramBudgetBytes());
            FBY_CHECK(!ScopedVirtual->Resolve(2, 2).resident || ScopedVirtual->Resolve(2, 2).layer < 2);

            ScopedVirtual->Shutdown();
        }
    }
    fs::remove_all(ScopedRoot, ScopedError);
}

void TestChangeMaskExtract()
{
    Fby::Ecs::Registry ScopedRegistry;
    std::array<Fby::Entity, 2> ScopedEntities{};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 2; ++ScopedIndex)
    {
        ScopedEntities[ScopedIndex] = ScopedRegistry.create();
        Fby::Transform2D ScopedTransform;
        ScopedTransform.Position = Fby::Vector2D{static_cast<float>(ScopedIndex) * 32.0f, 0.0f};
        ScopedRegistry.emplace<Fby::Transform2D>(ScopedEntities[ScopedIndex], ScopedTransform);
        Fby::Sprite2D ScopedSprite;
        ScopedSprite.Size = Fby::Vector2D{16.0f, 16.0f};
        ScopedRegistry.emplace<Fby::Sprite2D>(ScopedEntities[ScopedIndex], ScopedSprite);
        Fby::DrawOrder ScopedOrder;
        ScopedOrder.Layer = static_cast<std::uint16_t>(ScopedIndex);
        ScopedRegistry.emplace<Fby::DrawOrder>(ScopedEntities[ScopedIndex], ScopedOrder);
        Fby::Change::Ensure(ScopedRegistry, ScopedEntities[ScopedIndex]);
    }

    Fby::Rhi::ChangeMaskRenderExtract<64> ScopedExtract;
    FBY_CHECK(ScopedExtract.Extract(ScopedRegistry));
    FBY_CHECK(ScopedExtract.Stats().recomputed == 2);
    FBY_CHECK(ScopedExtract.ActiveCount() == 2);
    FBY_CHECK(ScopedExtract.DirtyRanges().size() == 1);

    auto& ScopedTransform = ScopedRegistry.get<Fby::Transform2D>(ScopedEntities[0]);
    ScopedTransform.Position.X = 12.0f;
    Fby::Change::MarkTransform(ScopedRegistry, ScopedEntities[0]);
    FBY_CHECK(ScopedExtract.Extract(ScopedRegistry));
    FBY_CHECK(ScopedExtract.Stats().recomputed == 1);
    FBY_CHECK(ScopedExtract.Stats().skippedStatic >= 1);
    FBY_CHECK(!ScopedExtract.DirtyRanges().empty());
    FBY_CHECK(ScopedExtract.DirtyRanges().size() == 1);

    FBY_CHECK(ScopedExtract.Extract(ScopedRegistry));
    FBY_CHECK(ScopedExtract.Stats().recomputed == 0);
    FBY_CHECK(ScopedExtract.DirtyRanges().empty());
    FBY_CHECK(ScopedExtract.Items()[0].position.X == 0.0f || ScopedExtract.Items()[0].position.X == 12.0f);

    // Non-render bits are consumed but never reach the GPU.
    Fby::Change::Mark(ScopedRegistry, ScopedEntities[1], Fby::ChangeMaskBit::Audio);
    FBY_CHECK(ScopedExtract.Extract(ScopedRegistry));
    FBY_CHECK(ScopedExtract.Stats().recomputed == 0);
    FBY_CHECK(ScopedExtract.DirtyRanges().empty());

    // A layer change moves the item in the sort, so the safe upload is full.
    auto& ScopedOrder = ScopedRegistry.get<Fby::DrawOrder>(ScopedEntities[0]);
    ScopedOrder.Layer = 9;
    Fby::Change::MarkDrawOrder(ScopedRegistry, ScopedEntities[0]);
    FBY_CHECK(ScopedExtract.Extract(ScopedRegistry));
    FBY_CHECK(ScopedExtract.Stats().structuralRebuilds == 1);
    FBY_CHECK(ScopedExtract.DirtyRanges().size() == 1);
    FBY_CHECK(ScopedExtract.DirtyRanges()[0].count == 2);

    Fby::Rhi::FramePacket ScopedPacket;
    Fby::Rhi::CameraData ScopedCamera;
    ScopedExtract.FillPacket(ScopedPacket, ScopedCamera, Fby::Color32::Black());
    FBY_CHECK(ScopedPacket.items.size() == 2);
    FBY_CHECK(ScopedPacket.batches.size() == 2);
    FBY_CHECK(ScopedPacket.instanceRangesAreDeltas);
    FBY_CHECK(ScopedPacket.instanceRanges.size() == 1);

    FBY_CHECK(ScopedExtract.Extract(ScopedRegistry));
    Fby::Rhi::FramePacket ScopedStaticPacket;
    ScopedExtract.FillPacket(ScopedStaticPacket, ScopedCamera, Fby::Color32::Black());
    FBY_CHECK(ScopedStaticPacket.instanceRangesAreDeltas);
    FBY_CHECK(ScopedStaticPacket.instanceRanges.empty());

    ScopedRegistry.destroy(ScopedEntities[1]);
    FBY_CHECK(ScopedExtract.Extract(ScopedRegistry));
    FBY_CHECK(ScopedExtract.ActiveCount() == 1);
}

Fby::Entity CreateRelevantEntity(Fby::Ecs::Registry& InRegistry, Fby::Vector2D InPosition)
{
    const Fby::Entity ScopedEntity = InRegistry.create();
    Fby::Transform2D ScopedTransform;
    ScopedTransform.Position = InPosition;
    InRegistry.emplace<Fby::Transform2D>(ScopedEntity, ScopedTransform);
    Fby::Velocity ScopedVelocity;
    InRegistry.emplace<Fby::Velocity>(ScopedEntity, ScopedVelocity);
    return ScopedEntity;
}

void TestRelevanceGrid()
{
    Fby::Ecs::Registry ScopedRegistry;
    std::array<Fby::Entity, 4> ScopedEntities{
        CreateRelevantEntity(ScopedRegistry, {100.0f, 100.0f}),
        CreateRelevantEntity(ScopedRegistry, {300.0f, 300.0f}),
        CreateRelevantEntity(ScopedRegistry, {700.0f, 100.0f}),
        CreateRelevantEntity(ScopedRegistry, {3000.0f, 3000.0f}),
    };

    Fby::Net::NetEntityMap ScopedMap;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedEntities.size(); ++ScopedIndex)
    {
        FBY_CHECK(ScopedMap.Register(Fby::Net::NetEntityId{ScopedIndex + 1}, ScopedEntities[ScopedIndex]));
    }

    Fby::Net::RelevanceConfig ScopedConfig;
    ScopedConfig.origin = Fby::Vector2D{0.0f, 0.0f};
    ScopedConfig.cellSize = 512.0f;
    ScopedConfig.cellsX = 16;
    ScopedConfig.cellsY = 16;
    ScopedConfig.neighborRadius = 1;
    ScopedConfig.nearRadius = 3;
    ScopedConfig.farRadius = 8;
    ScopedConfig.nearInterval = 2;
    ScopedConfig.farInterval = 4;

    Fby::Net::RelevanceGrid ScopedGrid;
    FBY_CHECK(ScopedGrid.Initialize(ScopedConfig));
    ScopedGrid.Rebuild(ScopedRegistry, ScopedMap);
    FBY_CHECK(ScopedGrid.Stats().placedEntities == 4);
    ScopedGrid.SetPlayerPosition(Fby::Net::NetPlayerId{0}, Fby::Vector2D{100.0f, 100.0f});

    Fby::Net::RelevancePlan ScopedPlan;
    FBY_CHECK(ScopedGrid.BuildPlan(Fby::Net::NetPlayerId{0}, 0, ScopedPlan));
    FBY_CHECK(ScopedPlan.Contains(Fby::Net::NetEntityId{1}));
    FBY_CHECK(ScopedPlan.Contains(Fby::Net::NetEntityId{2}));
    FBY_CHECK(ScopedPlan.Contains(Fby::Net::NetEntityId{3}));
    FBY_CHECK(ScopedPlan.Contains(Fby::Net::NetEntityId{4}));
    FBY_CHECK(ScopedPlan.immediateCount >= 3);
    FBY_CHECK(ScopedPlan.farCount >= 1);

    // Tick 1 is not the far entity's phase; the near band still replicates.
    FBY_CHECK(ScopedGrid.BuildPlan(Fby::Net::NetPlayerId{0}, 1, ScopedPlan));
    FBY_CHECK(ScopedPlan.Contains(Fby::Net::NetEntityId{1}));
    FBY_CHECK(!ScopedPlan.Contains(Fby::Net::NetEntityId{4}));

    // Integration: session-owned map, snapshot delta filtered to the plan.
    ScopedRegistry.get<Fby::Transform2D>(ScopedEntities[0]).Position = Fby::Vector2D{110.0f, 110.0f};
    ScopedRegistry.get<Fby::Transform2D>(ScopedEntities[3]).Position = Fby::Vector2D{9000.0f, 9000.0f};
    Fby::Ecs::SnapshotSchema ScopedSchema = Fby::Ecs::SnapshotSchema::BuiltIns();
    Fby::Net::NetRollbackSession::Config ScopedSessionConfig;
    ScopedSessionConfig.playerCount = 2;
    ScopedSessionConfig.rollbackTicks = 8;
    std::vector<std::byte> ScopedArena(8u * 1024u * 1024u);
    Fby::Net::NetRollbackSession ScopedSession;
    FBY_CHECK(ScopedSession.Initialize(ScopedRegistry, ScopedSchema, ScopedSessionConfig, ScopedArena) ==
              Fby::Net::NetRollbackSession::Result::Ok);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedEntities.size(); ++ScopedIndex)
    {
        FBY_CHECK(ScopedSession.RegisterEntity(ScopedEntities[ScopedIndex], Fby::Net::NetEntityId{ScopedIndex + 1})
                      .IsValid());
    }
    FBY_CHECK(ScopedSession.ConfigureRelevanceGrid(ScopedConfig) == Fby::Net::NetRollbackSession::Result::Ok);
    FBY_CHECK(ScopedSession.SetRelevancePlayerPosition(Fby::Net::NetPlayerId{0}, Fby::Vector2D{100.0f, 100.0f}) ==
              Fby::Net::NetRollbackSession::Result::Ok);
    FBY_CHECK(ScopedSession.RefreshRelevanceGrid() == Fby::Net::NetRollbackSession::Result::Ok);
    FBY_CHECK(ScopedSession.BuildRelevancePlan(Fby::Net::NetPlayerId{0}, 0, ScopedPlan) ==
              Fby::Net::NetRollbackSession::Result::Ok);

    std::vector<std::byte> ScopedBase(Fby::Ecs::SnapshotSize(ScopedRegistry, ScopedSchema));
    Fby::DeterministicRng ScopedRng;
    FBY_CHECK(Fby::Ecs::CaptureSnapshot(ScopedRegistry, ScopedSchema, ScopedBase, 1, ScopedRng));

    ScopedRegistry.get<Fby::Transform2D>(ScopedEntities[0]).Position = Fby::Vector2D{130.0f, 130.0f};
    ScopedRegistry.get<Fby::Transform2D>(ScopedEntities[3]).Position = Fby::Vector2D{9050.0f, 9050.0f};
    std::vector<std::byte> ScopedDelta(Fby::Ecs::SnapshotSize(ScopedRegistry, ScopedSchema));
    std::size_t ScopedDeltaBytes = 0;
    FBY_CHECK(
        Fby::Ecs::CaptureDelta(ScopedRegistry, ScopedSchema, ScopedBase, ScopedDelta, 2, ScopedRng, &ScopedDeltaBytes));
    ScopedDelta.resize(ScopedDeltaBytes);
    const Fby::Ecs::SnapshotView ScopedDeltaView = Fby::Ecs::SnapshotView::Open(ScopedDelta);
    FBY_CHECK(ScopedDeltaView.header.entityCount == 2);

    std::vector<std::byte> ScopedFiltered(ScopedDelta.size());
    std::size_t ScopedFilteredBytes = 0;
    FBY_CHECK(ScopedSession.FilterDeltaForPlayer(ScopedPlan, ScopedDelta, ScopedFiltered, &ScopedFilteredBytes) ==
              Fby::Net::NetRollbackSession::Result::Ok);
    const Fby::Ecs::SnapshotView ScopedFilteredView =
        Fby::Ecs::SnapshotView::Open(std::span<const std::byte>(ScopedFiltered.data(), ScopedFilteredBytes));
    FBY_CHECK(ScopedFilteredView.header.entityCount == 1);
    FBY_CHECK(ScopedFilteredView.header.payloadBytes < ScopedDeltaView.header.payloadBytes);
}

void TestAudioZones()
{
    Fby::Ecs::Registry ScopedRegistry;
    const Fby::Entity ScopedListener = ScopedRegistry.create();
    Fby::AudioListener ScopedListenerComponent;
    ScopedListenerComponent.position = Fby::Vector2D{0.0f, 0.0f};
    ScopedRegistry.emplace<Fby::AudioListener>(ScopedListener, ScopedListenerComponent);
    Fby::AudioContextComponent ScopedContext;
    ScopedRegistry.emplace<Fby::AudioContextComponent>(ScopedListener, ScopedContext);

    const Fby::Entity ScopedCave = ScopedRegistry.create();
    Fby::AudioZoneComponent ScopedCaveComponent;
    ScopedCaveComponent.Min = Fby::Vector2D{-100.0f, -100.0f};
    ScopedCaveComponent.Max = Fby::Vector2D{100.0f, 100.0f};
    ScopedCaveComponent.ZoneID = 7;
    ScopedCaveComponent.Kind = static_cast<std::uint32_t>(Fby::AudioZoneKind::Cave);
    ScopedCaveComponent.Priority = 5;
    ScopedCaveComponent.BusMask = Fby::kAudioZoneBusMaster | Fby::kAudioZoneBusSfx;
    ScopedCaveComponent.ReverbGain = 0.45f;
    ScopedCaveComponent.LowPassHz = 900.0f;
    ScopedCaveComponent.FadeSeconds = 0.1f;
    ScopedRegistry.emplace<Fby::AudioZoneComponent>(ScopedCave, ScopedCaveComponent);

    const Fby::Entity ScopedWater = ScopedRegistry.create();
    Fby::AudioZoneComponent ScopedWaterComponent;
    ScopedWaterComponent.Min = Fby::Vector2D{-50.0f, -50.0f};
    ScopedWaterComponent.Max = Fby::Vector2D{50.0f, 50.0f};
    ScopedWaterComponent.ZoneID = 8;
    ScopedWaterComponent.Kind = static_cast<std::uint32_t>(Fby::AudioZoneKind::Underwater);
    ScopedWaterComponent.Priority = 9;
    ScopedWaterComponent.ReverbGain = 0.2f;
    ScopedWaterComponent.LowPassHz = 450.0f;
    ScopedWaterComponent.FadeSeconds = 0.1f;
    ScopedRegistry.emplace<Fby::AudioZoneComponent>(ScopedWater, ScopedWaterComponent);

    Fby::AudioZoneSystem ScopedSystem;
    ScopedSystem.Update(ScopedRegistry, static_cast<Fby::AudioSystem*>(nullptr), 1.0f / 60.0f);
    const auto& ScopedResolved = ScopedRegistry.get<Fby::AudioContextComponent>(ScopedListener);
    FBY_CHECK(ScopedResolved.ActiveZoneID == 8);
    FBY_CHECK(ScopedResolved.TargetLowPassHz == 450.0f);
    FBY_CHECK(ScopedResolved.ReverbGain < 1.0f);
    FBY_CHECK(ScopedResolved.LowPassHz < 20000.0f);

    for (std::uint32_t ScopedStep = 0; ScopedStep < 120; ++ScopedStep)
    {
        ScopedSystem.Update(ScopedRegistry, static_cast<Fby::AudioSystem*>(nullptr), 1.0f / 60.0f);
    }
    FBY_CHECK(ScopedResolved.Blend > 0.99f);
    FBY_CHECK(ScopedResolved.ReverbGain < 0.3f);
    FBY_CHECK(ScopedResolved.LowPassHz < 500.0f);

    // Move the listener outside both zones: target returns to open and the
    auto& ScopedListenerState = ScopedRegistry.get<Fby::AudioListener>(ScopedListener);
    ScopedListenerState.position = Fby::Vector2D{1000.0f, 1000.0f};
    ScopedSystem.Update(ScopedRegistry, static_cast<Fby::AudioSystem*>(nullptr), 1.0f / 60.0f);
    FBY_CHECK(ScopedResolved.ActiveZoneID == 0);
    FBY_CHECK(ScopedResolved.TargetLowPassHz == 20000.0f);
    FBY_CHECK(ScopedResolved.LowPassHz > 450.0f);
    FBY_CHECK(ScopedResolved.Blend < 0.5f);

    // DSP filter path: finite output, gain/cutoff read back, no device opened.
    Fby::DspGraph ScopedGraph;
    FBY_CHECK(ScopedGraph.Initialize(48000));
    Fby::BusFilterParams ScopedFilter;
    ScopedFilter.reverbGain = 0.5f;
    ScopedFilter.lowPassHz = 1200.0f;
    ScopedFilter.smoothingSeconds = 0.05f;
    FBY_CHECK(ScopedGraph.SetBusFilter(Fby::kMasterBus, ScopedFilter) == Fby::DspResult::Ok);
    float ScopedBuffer[256 * 2] = {};
    ScopedGraph.ProcessOffline(ScopedBuffer, 256);
    bool ScopedFinite = true;
    for (const float ScopedSample : ScopedBuffer)
    {
        ScopedFinite = ScopedFinite && std::isfinite(ScopedSample);
    }
    FBY_CHECK(ScopedFinite);
    FBY_CHECK(std::fabs(ScopedGraph.BusFilter(Fby::kMasterBus).lowPassHz - 1200.0f) < 0.01f);
    FBY_CHECK(std::fabs(ScopedGraph.BusFilter(Fby::kMasterBus).reverbGain - 0.5f) < 0.001f);
}
}

int main()
{
    std::fprintf(stderr, "advanced: virtual texture\n");
    TestVirtualTexture();
    std::fprintf(stderr, "advanced: change mask\n");
    TestChangeMaskExtract();
    std::fprintf(stderr, "advanced: relevance grid\n");
    TestRelevanceGrid();
    std::fprintf(stderr, "advanced: audio zones\n");
    TestAudioZones();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "advanced pipelines: %d failure(s)\n", GFailures);
        return 1;
    }
    std::printf("advanced pipelines: OK (virtual texture, change mask, relevance grid, audio zones)\n");
    return 0;
}
