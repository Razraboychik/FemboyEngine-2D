// FemboyEngine2D - virtual texture residency and chunk streaming.
#include "Engine/RHI/VirtualTextureSystem.hpp"

#include "Engine/RHI/VirtualTextureFormat.hpp"

#include "Engine/Assets/VFS.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <system_error>

namespace Fby::Rhi
{
namespace
{
struct ChunkHeader
{
    char magic[8] = {};
    std::uint32_t version = 0;
    std::uint32_t tileSize = 0;
    std::uint32_t chunkTiles = 0;
    std::uint32_t format = 0;
    std::uint32_t reserved0 = 0;
    std::uint32_t reserved1 = 0;
};

static_assert(sizeof(ChunkHeader) == kVirtualTextureChunkHeaderBytes);
static_assert(std::is_trivially_copyable_v<ChunkHeader>);

[[nodiscard]] bool AppendText(char* InDestination, std::size_t InCapacity, std::size_t* InLength,
                              std::string_view InText) noexcept
{
    if (*InLength + InText.size() > InCapacity)
    {
        return false;
    }
    std::memcpy(InDestination + *InLength, InText.data(), InText.size());
    *InLength += InText.size();
    return true;
}

[[nodiscard]] bool AppendInteger(char* InDestination, std::size_t InCapacity, std::size_t* InLength,
                                 std::int64_t InValue) noexcept
{
    char ScopedBuffer[24]{};
    const auto ScopedResult = std::to_chars(ScopedBuffer, ScopedBuffer + sizeof(ScopedBuffer), InValue);
    if (ScopedResult.ec != std::errc{})
    {
        return false;
    }
    return AppendText(InDestination, InCapacity, InLength,
                      std::string_view(ScopedBuffer, static_cast<std::size_t>(ScopedResult.ptr - ScopedBuffer)));
}

[[nodiscard]] std::uint32_t IntegerSquareRoot(std::uint32_t InValue) noexcept
{
    std::uint32_t ScopedResult = 0;
    while ((ScopedResult + 1u) * (ScopedResult + 1u) <= InValue)
    {
        ++ScopedResult;
    }
    return ScopedResult;
}
}

bool VirtualTextureSystem::Initialize(const VirtualTextureDesc& InDesc, VirtualFileSystem& InVfs,
                                      const char* InChunkRoot, VirtualTextureUploadFn InUpload, void* InUploadUser,
                                      std::string_view* InError) noexcept
{
    const auto ScopedFail = [InError](std::string_view InMessage)
    {
        if (InError != nullptr)
        {
            *InError = InMessage;
        }
        return false;
    };

    if (InDesc.layerCount == 0 || InDesc.layerCount > kVirtualTextureMaxLayers)
    {
        return ScopedFail("virtual texture: layerCount must be 1..16");
    }
    if (InDesc.tileSize == 0 || InDesc.tileSize > InDesc.pageSize || (InDesc.pageSize % InDesc.tileSize) != 0)
    {
        return ScopedFail("virtual texture: pageSize must be a multiple of tileSize");
    }
    if (InDesc.pageSize > kVirtualTexturePageSize)
    {
        return ScopedFail("virtual texture: pageSize exceeds the 2048 fixed page");
    }
    if (InDesc.chunkTiles == 0 || InDesc.chunkTiles > 16)
    {
        return ScopedFail("virtual texture: chunkTiles must be 1..16");
    }
    if (InDesc.worldTileSize <= 0.0f)
    {
        return ScopedFail("virtual texture: worldTileSize must be positive");
    }
    if (InChunkRoot == nullptr || InChunkRoot[0] == '\0')
    {
        return ScopedFail("virtual texture: a non-empty chunk root is required");
    }

    if (InDesc.tileSize == 0 || InDesc.tileSize > 128)
    {
        return false;
    }
    MTilesPerAxis = VirtualTextureTilesPerAxis(InDesc.pageSize, InDesc.tileSize);
    if (MTilesPerAxis == 0 || MTilesPerAxis > kVirtualTextureTilesPerAxis)
    {
        return ScopedFail("virtual texture: page holds more tiles than the fixed pool");
    }
    MSlotsPerLayer = MTilesPerAxis * MTilesPerAxis;

    const std::size_t ScopedRootBytes = std::strlen(InChunkRoot);
    if (ScopedRootBytes >= kVirtualTextureMaxChunkRootBytes)
    {
        return ScopedFail("virtual texture: chunk root is longer than 191 bytes");
    }
    std::memset(MChunkRoot, 0, sizeof(MChunkRoot));
    std::memcpy(MChunkRoot, InChunkRoot, ScopedRootBytes);
    MChunkRootLength = ScopedRootBytes;

    MDesc = InDesc;
    MVfs = &InVfs;
    MUpload = InUpload;
    MUploadUser = InUploadUser;
    MFrame = 0;
    MResidentCount = 0;
    MStats = VirtualTextureStats{};
    MStats.vramBudgetBytes = VramBudgetBytes();
    MVictimCursor.fill(0);
    MLayerResident.fill(0);
    for (PageSlot& ScopedSlot : MSlots)
    {
        ScopedSlot = PageSlot{};
    }
    for (HashEntry& ScopedEntry : MHash)
    {
        ScopedEntry = HashEntry{};
    }
    MInitialized = true;
    return true;
}

void VirtualTextureSystem::Shutdown() noexcept
{
    MInitialized = false;
    MVfs = nullptr;
    MUpload = nullptr;
    MUploadUser = nullptr;
    MChunkRootLength = 0;
    std::memset(MChunkRoot, 0, sizeof(MChunkRoot));
    for (PageSlot& ScopedSlot : MSlots)
    {
        ScopedSlot = PageSlot{};
    }
    for (HashEntry& ScopedEntry : MHash)
    {
        ScopedEntry = HashEntry{};
    }
    MResidentCount = 0;
    MFrame = 0;
    MStats = VirtualTextureStats{};
}

std::uint64_t VirtualTextureSystem::HashTile(std::int64_t InTileX, std::int64_t InTileY) noexcept
{
    return VirtualTextureTileHash(InTileX, InTileY);
}

std::int64_t VirtualTextureSystem::FloorDiv(std::int64_t InValue, std::int64_t InDivisor) noexcept
{
    std::int64_t ScopedQuotient = InValue / InDivisor;
    const std::int64_t ScopedRemainder = InValue % InDivisor;
    if (ScopedRemainder != 0 && ((ScopedRemainder < 0) != (InDivisor < 0)))
    {
        --ScopedQuotient;
    }
    return ScopedQuotient;
}

std::uint32_t VirtualTextureSystem::LayerForTile(std::int64_t InTileX, std::int64_t InTileY) const noexcept
{
    return VirtualTextureLayerForTile(InTileX, InTileY, MDesc.layerCount);
}

std::uint32_t VirtualTextureSystem::FindHash(std::int64_t InTileX, std::int64_t InTileY) const noexcept
{
    std::uint32_t ScopedIndex = static_cast<std::uint32_t>(HashTile(InTileX, InTileY)) & kHashMask;
    for (std::uint32_t ScopedProbe = 0; ScopedProbe < kHashCapacity; ++ScopedProbe)
    {
        const HashEntry& ScopedEntry = MHash[ScopedIndex];
        if (ScopedEntry.state == kHashEmpty)
        {
            return kHashCapacity;
        }
        if (ScopedEntry.state == kHashOccupied && ScopedEntry.tileX == InTileX && ScopedEntry.tileY == InTileY)
        {
            return ScopedIndex;
        }
        ScopedIndex = (ScopedIndex + 1u) & kHashMask;
    }
    return kHashCapacity;
}

void VirtualTextureSystem::InsertHash(std::int64_t InTileX, std::int64_t InTileY, std::uint32_t InSlot,
                                      std::uint32_t InLayer) noexcept
{
    std::uint32_t ScopedIndex = static_cast<std::uint32_t>(HashTile(InTileX, InTileY)) & kHashMask;
    std::uint32_t ScopedTombstone = kHashCapacity;
    for (std::uint32_t ScopedProbe = 0; ScopedProbe < kHashCapacity; ++ScopedProbe)
    {
        HashEntry& ScopedEntry = MHash[ScopedIndex];
        if (ScopedEntry.state == kHashEmpty)
        {
            HashEntry& ScopedTarget = ScopedTombstone != kHashCapacity ? MHash[ScopedTombstone] : ScopedEntry;
            ScopedTarget.tileX = InTileX;
            ScopedTarget.tileY = InTileY;
            ScopedTarget.slot = InSlot;
            ScopedTarget.layer = static_cast<std::uint16_t>(InLayer);
            ScopedTarget.state = kHashOccupied;
            return;
        }
        if (ScopedEntry.state == kHashTombstone && ScopedTombstone == kHashCapacity)
        {
            ScopedTombstone = ScopedIndex;
        }
        else if (ScopedEntry.state == kHashOccupied && ScopedEntry.tileX == InTileX && ScopedEntry.tileY == InTileY)
        {
            ScopedEntry.slot = InSlot;
            ScopedEntry.layer = static_cast<std::uint16_t>(InLayer);
            return;
        }
        ScopedIndex = (ScopedIndex + 1u) & kHashMask;
    }
}

void VirtualTextureSystem::RemoveHash(std::int64_t InTileX, std::int64_t InTileY) noexcept
{
    std::uint32_t ScopedIndex = static_cast<std::uint32_t>(HashTile(InTileX, InTileY)) & kHashMask;
    for (std::uint32_t ScopedProbe = 0; ScopedProbe < kHashCapacity; ++ScopedProbe)
    {
        HashEntry& ScopedEntry = MHash[ScopedIndex];
        if (ScopedEntry.state == kHashEmpty)
        {
            return;
        }
        if (ScopedEntry.state == kHashOccupied && ScopedEntry.tileX == InTileX && ScopedEntry.tileY == InTileY)
        {
            ScopedEntry.state = kHashTombstone;
            ScopedEntry.slot = 0;
            ScopedEntry.layer = 0;
            return;
        }
        ScopedIndex = (ScopedIndex + 1u) & kHashMask;
    }
}

std::uint32_t VirtualTextureSystem::ChooseVictim(std::uint32_t InLayer) noexcept
{
    const std::uint32_t ScopedBase = InLayer * MSlotsPerLayer;
    std::uint32_t ScopedCursor = MVictimCursor[InLayer] % MSlotsPerLayer;
    for (std::uint32_t ScopedProbe = 0; ScopedProbe < MSlotsPerLayer; ++ScopedProbe)
    {
        const std::uint32_t ScopedLocal = ScopedCursor;
        PageSlot& ScopedSlot = MSlots[ScopedBase + ScopedLocal];
        if (ScopedSlot.occupied == 0 || ScopedSlot.lastUsedFrame != MFrame)
        {
            MVictimCursor[InLayer] = (ScopedLocal + 1u) % MSlotsPerLayer;
            if (ScopedSlot.occupied != 0)
            {
                RemoveHash(ScopedSlot.tileX, ScopedSlot.tileY);
                PushTableUpdate(ScopedSlot.tileX, ScopedSlot.tileY, kVirtualTexturePageTableMissing);
                ScopedSlot.occupied = 0;
                --MResidentCount;
                if (MLayerResident[InLayer] != 0)
                {
                    --MLayerResident[InLayer];
                }
                ++MStats.evictions;
            }
            return ScopedLocal;
        }
        ScopedCursor = (ScopedCursor + 1u) % MSlotsPerLayer;
    }
    // Every page in this pool is visible this frame. Skip the request instead of
    ++MStats.spills;
    return std::numeric_limits<std::uint32_t>::max();
}

bool VirtualTextureSystem::BuildChunkPath(std::int64_t InTileX, std::int64_t InTileY, char* OutPath,
                                          std::size_t InCapacity, std::size_t* OutLength) const noexcept
{
    std::size_t ScopedLength = 0;
    if (!AppendText(OutPath, InCapacity, &ScopedLength, std::string_view(MChunkRoot, MChunkRootLength)))
    {
        return false;
    }
    const std::int64_t ScopedChunkX = FloorDiv(InTileX, static_cast<std::int64_t>(MDesc.chunkTiles));
    const std::int64_t ScopedChunkY = FloorDiv(InTileY, static_cast<std::int64_t>(MDesc.chunkTiles));
    if (!AppendText(OutPath, InCapacity, &ScopedLength, "/chunk_") ||
        !AppendInteger(OutPath, InCapacity, &ScopedLength, ScopedChunkX) ||
        !AppendText(OutPath, InCapacity, &ScopedLength, "_") ||
        !AppendInteger(OutPath, InCapacity, &ScopedLength, ScopedChunkY) ||
        !AppendText(OutPath, InCapacity, &ScopedLength, ".fvtc"))
    {
        return false;
    }
    OutPath[ScopedLength] = '\0';
    *OutLength = ScopedLength;
    return true;
}

bool VirtualTextureSystem::LoadTile(std::int64_t InTileX, std::int64_t InTileY, std::uint32_t InLayer,
                                    std::uint32_t* OutSlot) noexcept
{
    if (MVfs == nullptr)
    {
        return false;
    }
    const std::uint32_t ScopedLocal = ChooseVictim(InLayer);
    if (ScopedLocal == std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    const std::uint32_t ScopedGlobal = InLayer * MSlotsPerLayer + ScopedLocal;
    PageSlot& ScopedSlot = MSlots[ScopedGlobal];
    ScopedSlot.tileX = InTileX;
    ScopedSlot.tileY = InTileY;
    ScopedSlot.lastUsedFrame = MFrame;
    ScopedSlot.occupied = 1;
    InsertHash(InTileX, InTileY, ScopedLocal, InLayer);

    char ScopedPath[512]{};
    std::size_t ScopedPathLength = 0;
    if (!BuildChunkPath(InTileX, InTileY, ScopedPath, sizeof(ScopedPath), &ScopedPathLength))
    {
        RemoveHash(InTileX, InTileY);
        ScopedSlot.occupied = 0;
        return false;
    }

    const auto ScopedBytes = MVfs->Read(std::string_view(ScopedPath, ScopedPathLength));
    if (!ScopedBytes || ScopedBytes->size() < sizeof(ChunkHeader))
    {
        ++MStats.vfsMisses;
        RemoveHash(InTileX, InTileY);
        ScopedSlot.occupied = 0;
        return false;
    }

    ChunkHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, ScopedBytes->data(), sizeof(ScopedHeader));
    const std::uint32_t ScopedChunkPixels = MDesc.chunkTiles * MDesc.tileSize;
    const std::uint64_t ScopedExpected = static_cast<std::uint64_t>(sizeof(ChunkHeader)) +
                                         static_cast<std::uint64_t>(ScopedChunkPixels) * ScopedChunkPixels * 4ull;
    if (std::memcmp(ScopedHeader.magic, kVirtualTextureChunkMagic, sizeof(ScopedHeader.magic)) != 0 ||
        ScopedHeader.version != 1 || ScopedHeader.tileSize != MDesc.tileSize ||
        ScopedHeader.chunkTiles != MDesc.chunkTiles || ScopedHeader.format != 0 ||
        ScopedBytes->size() != ScopedExpected)
    {
        ++MStats.vfsMisses;
        RemoveHash(InTileX, InTileY);
        ScopedSlot.occupied = 0;
        return false;
    }

    const std::int64_t ScopedChunkX = FloorDiv(InTileX, static_cast<std::int64_t>(MDesc.chunkTiles));
    const std::int64_t ScopedChunkY = FloorDiv(InTileY, static_cast<std::int64_t>(MDesc.chunkTiles));
    const std::int64_t ScopedLocalX = InTileX - ScopedChunkX * static_cast<std::int64_t>(MDesc.chunkTiles);
    const std::int64_t ScopedLocalY = InTileY - ScopedChunkY * static_cast<std::int64_t>(MDesc.chunkTiles);
    const std::uint32_t ScopedSlotX = ScopedLocal % MTilesPerAxis;
    const std::uint32_t ScopedSlotY = ScopedLocal / MTilesPerAxis;
    const std::uint32_t ScopedRowPitch = ScopedChunkPixels * 4u;
    const std::uint32_t ScopedPixelOffset =
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(ScopedLocalY) * MDesc.tileSize * ScopedRowPitch) +
                                   static_cast<std::uint64_t>(ScopedLocalX) * MDesc.tileSize * 4ull);
    const std::uint64_t ScopedTileBytes = static_cast<std::uint64_t>(MDesc.tileSize) * MDesc.tileSize * 4ull;
    const std::uint64_t ScopedLastRow = static_cast<std::uint64_t>(MDesc.tileSize - 1u) * ScopedRowPitch;
    if (static_cast<std::uint64_t>(ScopedPixelOffset) + ScopedLastRow + static_cast<std::uint64_t>(MDesc.tileSize) * 4ull >
        ScopedBytes->size() - sizeof(ChunkHeader))
    {
        ++MStats.vfsMisses;
        RemoveHash(InTileX, InTileY);
        ScopedSlot.occupied = 0;
        return false;
    }

    // Pack the strided chunk rows into a contiguous tile for the GPU copy.
    const std::byte* ScopedSource = ScopedBytes->data() + sizeof(ChunkHeader) + ScopedPixelOffset;
    std::span<const std::byte> ScopedPixels(ScopedSource, static_cast<std::size_t>(ScopedTileBytes));
    std::uint32_t ScopedUploadRowPitch = ScopedRowPitch;
    if (ScopedRowPitch != MDesc.tileSize * 4u)
    {
        const std::size_t ScopedRowBytes = static_cast<std::size_t>(MDesc.tileSize) * 4u;
        for (std::uint32_t ScopedRow = 0; ScopedRow < MDesc.tileSize; ++ScopedRow)
        {
            std::memcpy(MTileScratch.data() + static_cast<std::size_t>(ScopedRow) * ScopedRowBytes,
                        ScopedSource + static_cast<std::size_t>(ScopedRow) * ScopedRowPitch, ScopedRowBytes);
        }
        ScopedPixels = std::span<const std::byte>(MTileScratch.data(), static_cast<std::size_t>(ScopedTileBytes));
        ScopedUploadRowPitch = MDesc.tileSize * 4u;
    }

    VirtualTextureTileUpload ScopedUpload;
    ScopedUpload.layer = InLayer;
    ScopedUpload.originX = ScopedSlotX * MDesc.tileSize;
    ScopedUpload.originY = ScopedSlotY * MDesc.tileSize;
    ScopedUpload.width = MDesc.tileSize;
    ScopedUpload.height = MDesc.tileSize;
    ScopedUpload.rowPitch = ScopedUploadRowPitch;
    ScopedUpload.pixels = ScopedPixels;
    if (MUpload != nullptr && !MUpload(MUploadUser, ScopedUpload))
    {
        RemoveHash(InTileX, InTileY);
        ScopedSlot.occupied = 0;
        ++MStats.vfsMisses;
        return false;
    }

    ++MResidentCount;
    ++MLayerResident[InLayer];
    ++MStats.uploads;
    MStats.uploadedBytes += static_cast<std::uint64_t>(MDesc.tileSize) * MDesc.tileSize * 4ull;
    *OutSlot = ScopedLocal;
    PushTableUpdate(InTileX, InTileY, PackVirtualTexturePageTable(InLayer, ScopedLocal));
    return true;
}

void VirtualTextureSystem::Update(const VirtualTextureCamera& InCamera) noexcept
{
    Update(InCamera.center, InCamera.viewSize, InCamera.zoom, InCamera.rotation);
}

void VirtualTextureSystem::BeginFrame() noexcept
{
    if (!MInitialized)
    {
        return;
    }
    ++MFrame;
    MStats.frame = MFrame;
    MStats.visibleTiles = 0;
    MStats.uploads = 0;
    MStats.evictions = 0;
    MStats.vfsMisses = 0;
    MStats.uploadedBytes = 0;
    MStats.spills = 0;
    MStats.residentTiles = MResidentCount;
    MStats.vramBudgetBytes = VramBudgetBytes();
}

void VirtualTextureSystem::PushTableUpdate(std::int64_t InTileX, std::int64_t InTileY, std::uint32_t InEntry) noexcept
{
    if (MTableUpdateCount >= MTableUpdates.size())
    {
        return;
    }
    VirtualTextureTableUpdate& ScopedUpdate = MTableUpdates[MTableUpdateCount++];
    ScopedUpdate.TileX = static_cast<std::int32_t>(InTileX);
    ScopedUpdate.TileY = static_cast<std::int32_t>(InTileY);
    ScopedUpdate.Entry = InEntry;
}

void VirtualTextureSystem::RequestTiles(std::span<const VirtualTextureTileId> InTiles) noexcept
{
    if (!MInitialized || InTiles.empty())
    {
        return;
    }
    const std::uint32_t ScopedCount =
        std::min<std::uint32_t>(static_cast<std::uint32_t>(InTiles.size()), kVirtualTextureMaxVisibleTiles);
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        MSortScratch[ScopedI] = InTiles[ScopedI];
    }

    // Nearest-first: the tile under the camera centre loads before the corners.
    const float ScopedTileWorld = MDesc.worldTileSize;
    std::sort(MSortScratch.begin(), MSortScratch.begin() + ScopedCount,
              [this, ScopedTileWorld](const VirtualTextureTileId& InA, const VirtualTextureTileId& InB) noexcept
              {
                  const float ScopedAX = (static_cast<float>(InA.X) + 0.5f) * ScopedTileWorld - MFocusWorld.X;
                  const float ScopedAY = (static_cast<float>(InA.Y) + 0.5f) * ScopedTileWorld - MFocusWorld.Y;
                  const float ScopedBX = (static_cast<float>(InB.X) + 0.5f) * ScopedTileWorld - MFocusWorld.X;
                  const float ScopedBY = (static_cast<float>(InB.Y) + 0.5f) * ScopedTileWorld - MFocusWorld.Y;
                  const float ScopedDA = ScopedAX * ScopedAX + ScopedAY * ScopedAY;
                  const float ScopedDB = ScopedBX * ScopedBX + ScopedBY * ScopedBY;
                  if (ScopedDA != ScopedDB)
                  {
                      return ScopedDA < ScopedDB;
                  }
                  // Equal distance (symmetric grids): a total order keeps slot
                  // assignment identical between runs.
                  if (InA.Y != InB.Y)
                  {
                      return InA.Y < InB.Y;
                  }
                  return InA.X < InB.X;
              });

    std::uint32_t ScopedLoads = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        const VirtualTextureTileId& ScopedTile = MSortScratch[ScopedI];
        if (ScopedI > 0 && ScopedTile.X == MSortScratch[ScopedI - 1].X &&
            ScopedTile.Y == MSortScratch[ScopedI - 1].Y)
        {
            continue;
        }
        const std::uint32_t ScopedHash = FindHash(ScopedTile.X, ScopedTile.Y);
        if (ScopedHash != kHashCapacity)
        {
            // Resident: keep it hot for the LRU and count it as visible.
            const HashEntry& ScopedEntry = MHash[ScopedHash];
            if (ScopedEntry.layer < MDesc.layerCount && ScopedEntry.slot < MSlotsPerLayer)
            {
                MSlots[ScopedEntry.layer * MSlotsPerLayer + ScopedEntry.slot].lastUsedFrame = MFrame;
            }
            ++MStats.visibleTiles;
            continue;
        }
        if (ScopedLoads >= MMaxLoadsPerUpdate)
        {
            // Budget exhausted: the tile stays unrequested until a later frame.
            ++MStats.spills;
            continue;
        }
        const std::uint32_t ScopedLayer = LayerForTile(ScopedTile.X, ScopedTile.Y);
        std::uint32_t ScopedSlot = 0;
        if (!LoadTile(ScopedTile.X, ScopedTile.Y, ScopedLayer, &ScopedSlot))
        {
            continue;
        }
        MSlots[ScopedLayer * MSlotsPerLayer + ScopedSlot].lastUsedFrame = MFrame;
        ++ScopedLoads;
        ++MStats.visibleTiles;
    }
    MStats.residentTiles = MResidentCount;
}

void VirtualTextureSystem::Update(Vector2D InCenter, Vector2D InViewSize, float InZoom,
                                  float InRotationRadians) noexcept
{
    if (!MInitialized)
    {
        return;
    }
    BeginFrame();
    MFocusWorld = InCenter;

    const float ScopedZoom = InZoom > 0.0001f ? InZoom : 1.0f;
    const float ScopedCos = std::fabs(std::cos(InRotationRadians));
    const float ScopedSin = std::fabs(std::sin(InRotationRadians));
    const float ScopedHalfX = (InViewSize.X * 0.5f) / ScopedZoom;
    const float ScopedHalfY = (InViewSize.Y * 0.5f) / ScopedZoom;
    const float ScopedExtentX = ScopedHalfX * ScopedCos + ScopedHalfY * ScopedSin;
    const float ScopedExtentY = ScopedHalfX * ScopedSin + ScopedHalfY * ScopedCos;
    const float ScopedTileWorld = MDesc.worldTileSize;
    const std::int64_t ScopedMargin = static_cast<std::int64_t>(MDesc.prefetchTiles);

    std::int64_t ScopedMinX =
        FloorDiv(static_cast<std::int64_t>(std::floor((InCenter.X - ScopedExtentX) / ScopedTileWorld)), 1) -
        ScopedMargin;
    std::int64_t ScopedMaxX =
        FloorDiv(static_cast<std::int64_t>(std::floor((InCenter.X + ScopedExtentX) / ScopedTileWorld)), 1) +
        ScopedMargin;
    std::int64_t ScopedMinY =
        FloorDiv(static_cast<std::int64_t>(std::floor((InCenter.Y - ScopedExtentY) / ScopedTileWorld)), 1) -
        ScopedMargin;
    std::int64_t ScopedMaxY =
        FloorDiv(static_cast<std::int64_t>(std::floor((InCenter.Y + ScopedExtentY) / ScopedTileWorld)), 1) +
        ScopedMargin;

    // A pathological zoom-out must never turn one Update into an unbounded loop.
    const std::uint32_t ScopedMaxAxis = IntegerSquareRoot(kVirtualTextureMaxVisibleTiles);
    if (ScopedMaxX - ScopedMinX + 1 > static_cast<std::int64_t>(ScopedMaxAxis))
    {
        const std::int64_t ScopedCenter = (ScopedMinX + ScopedMaxX) / 2;
        ScopedMinX = ScopedCenter - static_cast<std::int64_t>(ScopedMaxAxis / 2);
        ScopedMaxX = ScopedMinX + static_cast<std::int64_t>(ScopedMaxAxis) - 1;
    }
    if (ScopedMaxY - ScopedMinY + 1 > static_cast<std::int64_t>(ScopedMaxAxis))
    {
        const std::int64_t ScopedCenter = (ScopedMinY + ScopedMaxY) / 2;
        ScopedMinY = ScopedCenter - static_cast<std::int64_t>(ScopedMaxAxis / 2);
        ScopedMaxY = ScopedMinY + static_cast<std::int64_t>(ScopedMaxAxis) - 1;
    }

    for (std::int64_t ScopedTileY = ScopedMinY; ScopedTileY <= ScopedMaxY; ++ScopedTileY)
    {
        for (std::int64_t ScopedTileX = ScopedMinX; ScopedTileX <= ScopedMaxX; ++ScopedTileX)
        {
            const std::uint32_t ScopedLayer = LayerForTile(ScopedTileX, ScopedTileY);
            const std::uint32_t ScopedHash = FindHash(ScopedTileX, ScopedTileY);
            std::uint32_t ScopedLocal = 0;
            if (ScopedHash != kHashCapacity)
            {
                ScopedLocal = MHash[ScopedHash].slot;
            }
            else if (!LoadTile(ScopedTileX, ScopedTileY, ScopedLayer, &ScopedLocal))
            {
                continue;
            }
            MSlots[ScopedLayer * MSlotsPerLayer + ScopedLocal].lastUsedFrame = MFrame;
            ++MStats.visibleTiles;
        }
    }
    MStats.residentTiles = MResidentCount;
}

VirtualTextureAddress VirtualTextureSystem::Resolve(std::int64_t InTileX, std::int64_t InTileY) const noexcept
{
    VirtualTextureAddress ScopedAddress;
    const std::uint32_t ScopedHash = FindHash(InTileX, InTileY);
    if (ScopedHash == kHashCapacity)
    {
        return ScopedAddress;
    }
    const HashEntry& ScopedEntry = MHash[ScopedHash];
    if (ScopedEntry.state != kHashOccupied || ScopedEntry.layer >= MDesc.layerCount)
    {
        return ScopedAddress;
    }
    std::uint32_t ScopedSlotX = 0;
    std::uint32_t ScopedSlotY = 0;
    if (!VirtualTextureCellFromSlot(MTilesPerAxis, ScopedEntry.slot, &ScopedSlotX, &ScopedSlotY))
    {
        return ScopedAddress;
    }
    ScopedAddress.layer = ScopedEntry.layer;
    ScopedAddress.slotX = ScopedSlotX;
    ScopedAddress.slotY = ScopedSlotY;
    ScopedAddress.resident = true;
    return ScopedAddress;
}
}
