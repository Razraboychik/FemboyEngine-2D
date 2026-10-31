// FemboyEngine2D - fixed-capacity asset identity table + metadata registry.
#pragma once

#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Core/AssetId.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace Fby::Assets
{

inline constexpr std::uint32_t kMaxAssetRecords = 1024;
inline constexpr std::uint32_t kAssetDbPathCapacity = kAssetPathCapacity;
inline constexpr std::uint32_t kMaxAssetReloadNotices = 64;

struct AssetRecord
{
    AssetId id{};
    AssetKind kind = AssetKind::Unknown;
    char path[kAssetDbPathCapacity]{};
    std::uint32_t size = 0;
    std::uint32_t generation = 0;
};

class AssetDatabase
{
public:
    AssetDatabase() = default;

    [[nodiscard]] bool Register(AssetId InId, AssetKind InKind, std::string_view InPath, std::uint32_t InSize = 0);
    [[nodiscard]] AssetId RegisterPath(std::string_view InPath, AssetKind InKind, std::uint32_t InSize = 0);

    [[nodiscard]] const AssetRecord* Find(AssetId InId) const noexcept;
    [[nodiscard]] std::string_view Path(AssetId InId) const noexcept;
    [[nodiscard]] AssetKind Kind(AssetId InId) const noexcept;

    [[nodiscard]] std::span<const AssetRecord> Records() const noexcept
    {
        return {MRecords, MCount};
    }
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }

    // Registers a new asset row and returns a stable handle. The id/path legacy
    [[nodiscard]] AssetHandle RegisterMeta(const AssetMeta& InMeta, std::span<const AssetDependency> InDependencies,
                                           std::string* InError = nullptr);

    // Loads a parsed .femeta (see ParseAssetMetaFile) into the table.
    [[nodiscard]] AssetHandle RegisterMetaFile(std::string_view InMetaText, std::string* InError = nullptr);

    // Migration helper: returns the metadata row for `id`, creating a default
    [[nodiscard]] AssetHandle EnsureMeta(AssetId InId, AssetKind InKind, std::string_view InSourcePath,
                                         std::string* InError = nullptr);

    [[nodiscard]] AssetHandle FindHandleByGuid(AssetGuid InGuid) const noexcept;
    [[nodiscard]] AssetHandle FindHandleByAssetId(AssetId InId) const noexcept;
    [[nodiscard]] AssetHandle FindHandleBySourcePath(std::string_view InSourcePath) const noexcept;

    // Validates the handle generation. Returns nullptr for a stale handle.
    [[nodiscard]] const AssetMeta* Meta(AssetHandle InHandle) const noexcept;
    [[nodiscard]] AssetId Id(AssetHandle InHandle) const noexcept;
    [[nodiscard]] std::string_view SourcePath(AssetHandle InHandle) const noexcept;
    [[nodiscard]] std::string_view CookedPath(AssetHandle InHandle) const noexcept;
    [[nodiscard]] std::span<const AssetDependency> Dependencies(AssetHandle InHandle) const noexcept;

    // Handles whose recorded dependencies contain `InGuid`. Used by the watch
    // system to schedule a dependent re-cook after its source asset changed.
    [[nodiscard]] std::uint32_t CollectDependents(AssetGuid InGuid, std::span<AssetHandle> Out) const noexcept;

    // A transaction only affects staged rows; live rows are untouched until
    [[nodiscard]] bool BeginTransaction() noexcept;
    [[nodiscard]] bool StageMeta(AssetHandle InHandle, const AssetMeta& InMeta, std::string* InError = nullptr);
    [[nodiscard]] bool CommitTransaction() noexcept;
    void RollbackTransaction() noexcept;
    [[nodiscard]] bool InTransaction() const noexcept
    {
        return MInTransaction;
    }

    // Pushed on commit for content that has a GPU/CPU representation. A consumer
    struct ReloadNotice
    {
        AssetHandle handle{};
        std::uint32_t contentGeneration = 0;
        AssetKind kind = AssetKind::Unknown;
    };

    [[nodiscard]] bool PopReload(ReloadNotice* Out) noexcept;
    [[nodiscard]] std::uint32_t PendingReloadCount() const noexcept
    {
        return static_cast<std::uint32_t>((MReloadHead + kMaxAssetReloadNotices - MReloadTail) %
                                          kMaxAssetReloadNotices);
    }

    // Test/tool seam: bypasses the OS watcher and stages a reload directly.
    [[nodiscard]] bool MarkSourceChanged(AssetHandle InHandle, std::uint64_t InSourceHash,
                                         std::string* InError = nullptr);

    // Editor/import startup helper: recursively loads every *.femeta below
    [[nodiscard]] bool LoadMetaDirectory(std::string_view InDirectory, std::string* InError = nullptr);

private:
    struct StagedRow
    {
        AssetMeta meta{};
        bool staged = false;
    };

    [[nodiscard]] std::uint32_t AllocateSlot(AssetGuid InGuid, std::string* InError);
    void PushReload(AssetHandle InHandle, const AssetMeta& InMeta) noexcept;

    AssetRecord MRecords[kMaxAssetRecords]{};
    std::uint32_t MCount = 0;

    AssetMeta MMetas[kMaxAssetMeta]{};
    // Handle generation per slot
    std::uint32_t MMetaGenerations[kMaxAssetMeta]{};
    AssetHandle MHandles[kMaxAssetMeta]{};
    bool MMetaUsed[kMaxAssetMeta]{};
    AssetDependency MDependencies[kMaxAssetMeta * kMaxAssetDependencies]{};
    std::uint32_t MDependencyCount = 0;

    StagedRow MStaged[kMaxAssetMeta]{};
    bool MInTransaction = false;
    std::uint32_t MStagedCount = 0;

    ReloadNotice MReloadQueue[kMaxAssetReloadNotices]{};
    std::uint32_t MReloadHead = 0;
    std::uint32_t MReloadTail = 0;
};
}
