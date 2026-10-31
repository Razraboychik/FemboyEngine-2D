// FemboyEngine2D - virtual texturing for unbounded 2D terrain.
#pragma once

#include "Engine/Core/Definitions.hpp"
#include "Engine/Core/Types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby
{
class VirtualFileSystem;

namespace Rhi
{

inline constexpr std::uint32_t kVirtualTextureTileSize = 64;
inline constexpr std::uint32_t kVirtualTexturePageSize = 2048;
inline constexpr std::uint32_t kVirtualTextureTilesPerAxis = kVirtualTexturePageSize / kVirtualTextureTileSize;
inline constexpr std::uint32_t kVirtualTextureTilesPerLayer = kVirtualTextureTilesPerAxis * kVirtualTextureTilesPerAxis;
inline constexpr std::uint32_t kVirtualTextureMaxLayers = 16;
inline constexpr std::uint32_t kVirtualTextureMaxVisibleTiles = 4096;
inline constexpr std::uint32_t kVirtualTextureChunkTiles = 4;
inline constexpr std::uint32_t kVirtualTextureMaxChunkRootBytes = 192;
inline constexpr std::uint32_t kVirtualTextureChunkHeaderBytes = 32;
inline constexpr char kVirtualTextureChunkMagic[8] = {'F', 'B', 'Y', 'V', 'T', 'C', '0', '1'};

// Default: eight pools, 128 MiB of VRAM, 8 * 1024 resident tiles. The default
struct VirtualTextureDesc
{
    std::uint32_t layerCount = 8;
    std::uint32_t pageSize = kVirtualTexturePageSize;
    std::uint32_t tileSize = kVirtualTextureTileSize;
    std::uint32_t chunkTiles = kVirtualTextureChunkTiles;
    // World units covered by one tile. 256 world units at 64 pixels is 4 px per
    float worldTileSize = 256.0f;
    // Extra rings of tiles loaded around the camera. 0 keeps the VRAM strictly
    std::uint32_t prefetchTiles = 0;
};

struct VirtualTextureStats
{
    std::uint32_t frame = 0;
    std::uint32_t visibleTiles = 0;
    std::uint32_t residentTiles = 0;
    std::uint32_t uploads = 0;
    std::uint32_t evictions = 0;
    std::uint32_t vfsMisses = 0;
    std::uint64_t uploadedBytes = 0;
    std::uint64_t vramBudgetBytes = 0;
    std::uint32_t spills = 0;
};

// Camera view in world units. Kept independent of Camera2D so the RHI stays
struct VirtualTextureCamera
{
    Vector2D center{};
    Vector2D viewSize{1920.0f, 1080.0f};
    float zoom = 1.0f;
    float rotation = 0.0f;
};

// Where a world tile currently lives. `resident == false` means the tile is
struct VirtualTextureAddress
{
    std::uint32_t layer = 0;
    std::uint32_t slotX = 0;
    std::uint32_t slotY = 0;
    bool resident = false;
};

// One tile upload. `pixels` points into the VFS mapping (or into the system's
struct VirtualTextureTileUpload
{
    std::uint32_t layer = 0;
    // Texel origin in the array layer: slotX * tileSize, slotY * tileSize.
    std::uint32_t originX = 0;
    std::uint32_t originY = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rowPitch = 0;
    std::span<const std::byte> pixels{};
};

using VirtualTextureUploadFn = bool (*)(void* InUser, const VirtualTextureTileUpload& InUpload) noexcept;

// One feedback request: the world tile the sprite path wants resident.
struct VirtualTextureTileId
{
    std::int32_t X = 0;
    std::int32_t Y = 0;
};

// A batched page-table change produced by the streaming loop. `Entry` is the
// packed layer/slot (see VirtualTextureFormat.hpp) or the missing sentinel for
// an evicted tile.
struct VirtualTextureTableUpdate
{
    std::int32_t TileX = 0;
    std::int32_t TileY = 0;
    std::uint32_t Entry = 0;
};

static_assert(std::is_trivially_copyable_v<VirtualTextureTileId>);
static_assert(std::is_trivially_copyable_v<VirtualTextureTableUpdate>);

class VirtualTextureSystem
{
public:
    VirtualTextureSystem() = default;

    VirtualTextureSystem(const VirtualTextureSystem&) = delete;
    VirtualTextureSystem& operator=(const VirtualTextureSystem&) = delete;

    // `InChunkRoot` is a VFS path prefix, copied into a fixed buffer. The VFS
    [[nodiscard]] bool Initialize(const VirtualTextureDesc& InDesc, VirtualFileSystem& InVfs, const char* InChunkRoot,
                                  VirtualTextureUploadFn InUpload = nullptr, void* InUploadUser = nullptr,
                                  std::string_view* InError = nullptr) noexcept;

    void Shutdown() noexcept;

    // Starts a streaming frame: advances the counter and resets per-frame stats.
    void BeginFrame() noexcept;

    // Feedback-driven streaming. Requests are deduplicated against residents,
    // ordered by distance to the last focus point (the camera centre) and loaded
    // within the per-frame upload budget; page-table changes are batched.
    void RequestTiles(std::span<const VirtualTextureTileId> InTiles) noexcept;
    void SetFocus(Vector2D InWorldCenter) noexcept
    {
        MFocusWorld = InWorldCenter;
    }
    void SetMaxLoadsPerUpdate(std::uint32_t InMax) noexcept
    {
        MMaxLoadsPerUpdate = InMax;
    }
    [[nodiscard]] std::uint32_t MaxLoadsPerUpdate() const noexcept
    {
        return MMaxLoadsPerUpdate;
    }
    // Extra rings of tiles loaded around the camera by the camera-driven pass.
    void SetPrefetchTiles(std::uint32_t InTiles) noexcept
    {
        MDesc.prefetchTiles = InTiles;
    }
    [[nodiscard]] std::span<const VirtualTextureTableUpdate> PendingTableUpdates() const noexcept
    {
        return {MTableUpdates.data(), MTableUpdateCount};
    }
    void ClearPendingTableUpdates() noexcept
    {
        MTableUpdateCount = 0;
    }

    // Residency pass for one camera. `InCamera` already carries rotation and
    void Update(const VirtualTextureCamera& InCamera) noexcept;
    void Update(Vector2D InCenter, Vector2D InViewSize, float InZoom, float InRotationRadians = 0.0f) noexcept;

    [[nodiscard]] VirtualTextureAddress Resolve(std::int64_t InTileX, std::int64_t InTileY) const noexcept;

    [[nodiscard]] bool Initialized() const noexcept
    {
        return MInitialized;
    }
    [[nodiscard]] const VirtualTextureDesc& Description() const noexcept
    {
        return MDesc;
    }
    [[nodiscard]] const VirtualTextureStats& Stats() const noexcept
    {
        return MStats;
    }
    [[nodiscard]] std::uint64_t VramBudgetBytes() const noexcept
    {
        return static_cast<std::uint64_t>(MDesc.layerCount) * MDesc.pageSize * MDesc.pageSize * 4ull;
    }
    [[nodiscard]] std::uint32_t ResidentTileCount() const noexcept
    {
        return MResidentCount;
    }
    [[nodiscard]] std::uint32_t LayerForTile(std::int64_t InTileX, std::int64_t InTileY) const noexcept;

private:
    struct PageSlot
    {
        std::int64_t tileX = 0;
        std::int64_t tileY = 0;
        std::uint32_t lastUsedFrame = 0;
        std::uint8_t occupied = 0;
        std::uint8_t reserved[3] = {};
    };

    struct HashEntry
    {
        std::int64_t tileX = 0;
        std::int64_t tileY = 0;
        std::uint32_t slot = 0;
        std::uint16_t layer = 0;
        // 0 empty, 1 occupied, 2 tombstone
        std::uint8_t state = 0;
        std::uint8_t reserved = 0;
    };

    static constexpr std::uint32_t kHashCapacity = 65536;
    static constexpr std::uint32_t kHashMask = kHashCapacity - 1u;
    static constexpr std::uint8_t kHashEmpty = 0;
    static constexpr std::uint8_t kHashOccupied = 1;
    static constexpr std::uint8_t kHashTombstone = 2;

    [[nodiscard]] static std::uint64_t HashTile(std::int64_t InTileX, std::int64_t InTileY) noexcept;
    [[nodiscard]] static std::int64_t FloorDiv(std::int64_t InValue, std::int64_t InDivisor) noexcept;
    [[nodiscard]] std::uint32_t FindHash(std::int64_t InTileX, std::int64_t InTileY) const noexcept;
    void InsertHash(std::int64_t InTileX, std::int64_t InTileY, std::uint32_t InSlot, std::uint32_t InLayer) noexcept;
    void RemoveHash(std::int64_t InTileX, std::int64_t InTileY) noexcept;

    [[nodiscard]] std::uint32_t ChooseVictim(std::uint32_t InLayer) noexcept;
    [[nodiscard]] bool LoadTile(std::int64_t InTileX, std::int64_t InTileY, std::uint32_t InLayer,
                                std::uint32_t* OutSlot) noexcept;
    void PushTableUpdate(std::int64_t InTileX, std::int64_t InTileY, std::uint32_t InEntry) noexcept;
    [[nodiscard]] bool BuildChunkPath(std::int64_t InTileX, std::int64_t InTileY, char* OutPath, std::size_t InCapacity,
                                      std::size_t* OutLength) const noexcept;

    VirtualTextureDesc MDesc{};
    VirtualFileSystem* MVfs = nullptr;
    VirtualTextureUploadFn MUpload = nullptr;
    void* MUploadUser = nullptr;
    char MChunkRoot[kVirtualTextureMaxChunkRootBytes]{};
    std::size_t MChunkRootLength = 0;
    bool MInitialized = false;

    std::array<PageSlot, kVirtualTextureMaxLayers * kVirtualTextureTilesPerLayer> MSlots{};
    std::array<HashEntry, kHashCapacity> MHash{};
    std::array<std::uint32_t, kVirtualTextureMaxLayers> MVictimCursor{};
    std::array<std::uint32_t, kVirtualTextureMaxLayers> MLayerResident{};
    std::uint32_t MResidentCount = 0;
    std::uint32_t MFrame = 0;
    std::uint32_t MTilesPerAxis = 0;
    std::uint32_t MSlotsPerLayer = 0;
    VirtualTextureStats MStats{};
    Vector2D MFocusWorld{};
    std::uint32_t MMaxLoadsPerUpdate = 8;
    std::array<VirtualTextureTileId, kVirtualTextureMaxVisibleTiles> MSortScratch{};
    // Chunk rows are strided; a tile upload is contiguous, so the load path packs
    // it here when the row pitch differs from tileSize * 4.
    std::array<std::byte, 128u * 128u * 4u> MTileScratch{};
    std::array<VirtualTextureTableUpdate, 512> MTableUpdates{};
    std::uint32_t MTableUpdateCount = 0;
};
}
}
