// FemboyEngine2D - asset identity table + transactional .femeta registry.
#include "Engine/Assets/AssetDatabase.hpp"

#include "Engine/Assets/PakFormat.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace Fby::Assets
{
namespace
{
void SetError(std::string* InError, std::string_view InText)
{
    if (InError != nullptr)
    {
        *InError = InText;
    }
}

[[nodiscard]] bool ValidMeta(const AssetMeta& InMeta) noexcept
{
    return InMeta.valid && InMeta.kind != AssetKind::Unknown && InMeta.sourcePath[0] != '\0';
}
}

bool AssetDatabase::Register(AssetId InId, AssetKind InKind, std::string_view InPath, std::uint32_t InSize)
{
    if (!InId.IsValid() || InPath.empty() || InPath.size() >= kAssetDbPathCapacity)
    {
        return false;
    }
    if (MRecordIndex.contains(InId.value))
    {
        // Deterministic id already registered; first path wins
        return true;
    }
    const std::string ScopedNormalized = NormalizePakPath(InPath);
    if (ScopedNormalized.empty() || ScopedNormalized.size() >= kAssetDbPathCapacity)
    {
        return false;
    }
    AssetRecord ScopedRecord;
    ScopedRecord.id = InId;
    ScopedRecord.kind = InKind;
    ScopedRecord.size = InSize;
    std::memcpy(ScopedRecord.path, ScopedNormalized.data(), ScopedNormalized.size());
    ScopedRecord.path[ScopedNormalized.size()] = '\0';
    MRecordIndex.insert_or_assign(InId.value, static_cast<std::uint32_t>(MRecords.size()));
    MRecords.push_back(ScopedRecord);
    return true;
}

AssetId AssetDatabase::RegisterPath(std::string_view InPath, AssetKind InKind, std::uint32_t InSize)
{
    const AssetId ScopedId = AssetId::FromPath(InPath, InKind);
    if (!Register(ScopedId, InKind, InPath, InSize))
    {
        return AssetId{};
    }
    return ScopedId;
}

const AssetRecord* AssetDatabase::Find(AssetId InId) const noexcept
{
    const auto ScopedIt = MRecordIndex.find(InId.value);
    return ScopedIt != MRecordIndex.end() ? &MRecords[ScopedIt->second] : nullptr;
}

std::string_view AssetDatabase::Path(AssetId InId) const noexcept
{
    const AssetRecord* ScopedRecord = Find(InId);
    return ScopedRecord != nullptr ? std::string_view(ScopedRecord->path) : std::string_view{};
}

AssetKind AssetDatabase::Kind(AssetId InId) const noexcept
{
    const AssetRecord* ScopedRecord = Find(InId);
    return ScopedRecord != nullptr ? ScopedRecord->kind : AssetKind::Unknown;
}

// Metadata registry
std::uint32_t AssetDatabase::AllocateSlot(AssetGuid InGuid, std::string* InError)
{
    (void)InError;
    // Slots are append-only: a handle keeps its generation for the life of the
    // database, so the table grows with the project instead of rejecting it.
    const std::uint32_t ScopedSlot = static_cast<std::uint32_t>(MMetas.size());
    MMetas.push_back(AssetMeta{});
    MMetas.back().guid = InGuid;
    MMetaGenerations.push_back(1);
    MMetaUsed.push_back(1);
    MHandles.push_back(MakeAssetHandle(ScopedSlot, 1));
    MStaged.push_back(StagedRow{});
    return ScopedSlot;
}

AssetHandle AssetDatabase::RegisterMeta(const AssetMeta& InMeta, std::span<const AssetDependency> InDependencies,
                                        std::string* InError)
{
    if (!ValidMeta(InMeta))
    {
        SetError(InError, "cannot register invalid asset metadata");
        return {};
    }
    const AssetGuid ScopedGuid = InMeta.guid != 0 ? InMeta.guid : ComputeAssetGuid(InMeta.sourcePath, InMeta.kind);
    if (const AssetHandle ScopedExisting = FindHandleByGuid(ScopedGuid); ScopedExisting.IsValid())
    {
        // Idempotent import
        return ScopedExisting;
    }
    if (InDependencies.size() > kMaxAssetDependencies ||
        MDependencyCount + InDependencies.size() > kMaxAssetDependencyEntries)
    {
        SetError(InError, "asset dependency table is full");
        return {};
    }
    const std::uint32_t ScopedSlot = AllocateSlot(ScopedGuid, InError);
    if (ScopedSlot == AssetHandle::kInvalidIndex)
    {
        return {};
    }
    AssetMeta& ScopedStored = MMetas[ScopedSlot];
    ScopedStored = InMeta;
    ScopedStored.guid = ScopedGuid;
    if (!ScopedStored.id.IsValid())
    {
        ScopedStored.id = AssetId::FromPath(ScopedStored.sourcePath, ScopedStored.kind);
    }
    ScopedStored.valid = true;
    ScopedStored.dependencyFirst = static_cast<std::uint16_t>(MDependencyCount);
    ScopedStored.dependencyCount = static_cast<std::uint16_t>(InDependencies.size());
    for (const AssetDependency& ScopedDependency : InDependencies)
    {
        MDependencies.push_back(ScopedDependency);
        ++MDependencyCount;
    }
    MGuidIndex.insert_or_assign(ScopedGuid, ScopedSlot);
    MIdIndex.insert_or_assign(ScopedStored.id.value, ScopedSlot);
    MPathIndex.insert_or_assign(std::string(ScopedStored.sourcePath), ScopedSlot);
    // Keep the legacy id/path table in sync for the scene loader and VFS.
    (void)Register(ScopedStored.id, ScopedStored.kind, ScopedStored.sourcePath);
    return MHandles[ScopedSlot];
}

AssetHandle AssetDatabase::RegisterMetaFile(std::string_view InMetaText, std::string* InError)
{
    AssetMeta ScopedMeta;
    AssetDependency ScopedDependencies[kMaxAssetDependencies]{};
    std::uint32_t ScopedDependencyCount = 0;
    if (!ParseAssetMetaFile(InMetaText, &ScopedMeta, ScopedDependencies, kMaxAssetDependencies, &ScopedDependencyCount,
                            InError))
    {
        return {};
    }
    return RegisterMeta(ScopedMeta, std::span<const AssetDependency>(ScopedDependencies, ScopedDependencyCount),
                        InError);
}

AssetHandle AssetDatabase::EnsureMeta(AssetId InId, AssetKind InKind, std::string_view InSourcePath,
                                      std::string* InError)
{
    if (const AssetHandle ScopedExisting = FindHandleByAssetId(InId); ScopedExisting.IsValid())
    {
        return ScopedExisting;
    }
    AssetMeta ScopedMeta;
    ScopedMeta.guid = ComputeAssetGuid(InSourcePath, InKind);
    ScopedMeta.id = InId;
    ScopedMeta.kind = InKind;
    if (InSourcePath.empty() || InSourcePath.size() >= kAssetPathCapacity)
    {
        SetError(InError, "EnsureMeta: invalid source path");
        return {};
    }
    std::memcpy(ScopedMeta.sourcePath, InSourcePath.data(), InSourcePath.size());
    ScopedMeta.sourcePath[InSourcePath.size()] = '\0';
    ScopedMeta.valid = true;
    return RegisterMeta(ScopedMeta, {}, InError);
}

AssetHandle AssetDatabase::FindHandleByGuid(AssetGuid InGuid) const noexcept
{
    if (InGuid == 0)
    {
        return {};
    }
    const auto ScopedIt = MGuidIndex.find(InGuid);
    return ScopedIt != MGuidIndex.end() ? MHandles[ScopedIt->second] : AssetHandle{};
}

AssetHandle AssetDatabase::FindHandleByAssetId(AssetId InId) const noexcept
{
    if (!InId.IsValid())
    {
        return {};
    }
    const auto ScopedIt = MIdIndex.find(InId.value);
    return ScopedIt != MIdIndex.end() ? MHandles[ScopedIt->second] : AssetHandle{};
}

AssetHandle AssetDatabase::FindHandleBySourcePath(std::string_view InSourcePath) const noexcept
{
    if (InSourcePath.empty())
    {
        return {};
    }
    const std::string ScopedNormalized = NormalizePakPath(InSourcePath);
    const auto ScopedIt = MPathIndex.find(ScopedNormalized);
    return ScopedIt != MPathIndex.end() ? MHandles[ScopedIt->second] : AssetHandle{};
}

const AssetMeta* AssetDatabase::Meta(AssetHandle InHandle) const noexcept
{
    if (!InHandle.IsValid() || InHandle.index >= MMetas.size() || MMetaUsed[InHandle.index] == 0 ||
        !(MHandles[InHandle.index] == InHandle))
    {
        return nullptr;
    }
    return &MMetas[InHandle.index];
}

AssetId AssetDatabase::Id(AssetHandle InHandle) const noexcept
{
    const AssetMeta* ScopedMeta = Meta(InHandle);
    return ScopedMeta != nullptr ? ScopedMeta->id : AssetId{};
}

std::string_view AssetDatabase::SourcePath(AssetHandle InHandle) const noexcept
{
    const AssetMeta* ScopedMeta = Meta(InHandle);
    return ScopedMeta != nullptr ? std::string_view(ScopedMeta->sourcePath) : std::string_view{};
}

std::string_view AssetDatabase::CookedPath(AssetHandle InHandle) const noexcept
{
    const AssetMeta* ScopedMeta = Meta(InHandle);
    return ScopedMeta != nullptr ? std::string_view(ScopedMeta->cookedPath) : std::string_view{};
}

std::span<const AssetDependency> AssetDatabase::Dependencies(AssetHandle InHandle) const noexcept
{
    const AssetMeta* ScopedMeta = Meta(InHandle);
    if (ScopedMeta == nullptr || ScopedMeta->dependencyCount == 0)
    {
        return {};
    }
    return {MDependencies.data() + ScopedMeta->dependencyFirst, ScopedMeta->dependencyCount};
}

std::uint32_t AssetDatabase::CollectDependents(AssetGuid InGuid, std::span<AssetHandle> Out) const noexcept
{
    if (InGuid == kInvalidAssetGuid)
    {
        return 0;
    }
    std::uint32_t ScopedCount = 0;
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < MMetas.size(); ++ScopedSlot)
    {
        if (MMetaUsed[ScopedSlot] == 0)
        {
            continue;
        }
        const AssetMeta& ScopedMeta = MMetas[ScopedSlot];
        if (ScopedMeta.dependencyCount == 0)
        {
            continue;
        }
        for (std::uint16_t ScopedDependency = 0; ScopedDependency < ScopedMeta.dependencyCount; ++ScopedDependency)
        {
            const AssetDependency& ScopedEntry = MDependencies[ScopedMeta.dependencyFirst + ScopedDependency];
            if (ScopedEntry.guid != InGuid)
            {
                continue;
            }
            if (ScopedCount < Out.size())
            {
                Out[ScopedCount] = MHandles[ScopedSlot];
            }
            ++ScopedCount;
            break;
        }
    }
    return ScopedCount;
}

// Transactions
bool AssetDatabase::BeginTransaction() noexcept
{
    if (MInTransaction)
    {
        return false;
    }
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < MStaged.size(); ++ScopedSlot)
    {
        MStaged[ScopedSlot].staged = false;
        MStaged[ScopedSlot].meta = {};
    }
    MStagedCount = 0;
    MInTransaction = true;
    return true;
}

bool AssetDatabase::StageMeta(AssetHandle InHandle, const AssetMeta& InMeta, std::string* InError)
{
    if (!MInTransaction)
    {
        SetError(InError, "StageMeta requires an open transaction");
        return false;
    }
    if (Meta(InHandle) == nullptr)
    {
        SetError(InError, "StageMeta: stale asset handle");
        return false;
    }
    if (!ValidMeta(InMeta))
    {
        SetError(InError, "StageMeta: invalid metadata");
        return false;
    }
    const std::uint32_t ScopedSlot = InHandle.index;
    if (!MStaged[ScopedSlot].staged)
    {
        ++MStagedCount;
    }
    MStaged[ScopedSlot].meta = InMeta;
    MStaged[ScopedSlot].staged = true;
    return true;
}

bool AssetDatabase::CommitTransaction() noexcept
{
    if (!MInTransaction)
    {
        return false;
    }
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < MStaged.size(); ++ScopedSlot)
    {
        if (!MStaged[ScopedSlot].staged)
        {
            continue;
        }
        AssetMeta& ScopedLive = MMetas[ScopedSlot];
        const AssetMeta ScopedUpdated = MStaged[ScopedSlot].meta;
        // Identity and dependency layout are immutable across a transaction;
        ScopedLive.sourceHash = ScopedUpdated.sourceHash;
        ScopedLive.cookedHash = ScopedUpdated.cookedHash;
        ScopedLive.import = ScopedUpdated.import;
        ScopedLive.flags = ScopedUpdated.flags;
        if (ScopedUpdated.cookedPath[0] != '\0')
        {
            std::memcpy(ScopedLive.cookedPath, ScopedUpdated.cookedPath, sizeof(ScopedLive.cookedPath));
        }
        ++ScopedLive.contentGeneration;
        PushReload(MHandles[ScopedSlot], ScopedLive);
        MStaged[ScopedSlot].staged = false;
        MStaged[ScopedSlot].meta = {};
    }
    MStagedCount = 0;
    MInTransaction = false;
    return true;
}

void AssetDatabase::RollbackTransaction() noexcept
{
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < MStaged.size(); ++ScopedSlot)
    {
        MStaged[ScopedSlot].staged = false;
        MStaged[ScopedSlot].meta = {};
    }
    MStagedCount = 0;
    MInTransaction = false;
}

bool AssetDatabase::MarkSourceChanged(AssetHandle InHandle, std::uint64_t InSourceHash, std::string* InError)
{
    const AssetMeta* ScopedLive = Meta(InHandle);
    if (ScopedLive == nullptr)
    {
        SetError(InError, "MarkSourceChanged: stale asset handle");
        return false;
    }
    const bool ScopedOwnedTransaction = !MInTransaction;
    if (ScopedOwnedTransaction && !BeginTransaction())
    {
        SetError(InError, "MarkSourceChanged: cannot open transaction");
        return false;
    }
    AssetMeta ScopedStaged = *ScopedLive;
    ScopedStaged.sourceHash = InSourceHash;
    if (!StageMeta(InHandle, ScopedStaged, InError))
    {
        if (ScopedOwnedTransaction)
        {
            RollbackTransaction();
        }
        return false;
    }
    if (ScopedOwnedTransaction)
    {
        return CommitTransaction();
    }
    return true;
}

bool AssetDatabase::LoadMetaDirectory(std::string_view InDirectory, std::string* InError)
{
    const std::filesystem::path ScopedRoot(InDirectory);
    std::error_code ScopedCode;
    if (!std::filesystem::is_directory(ScopedRoot, ScopedCode))
    {
        SetError(InError, "asset metadata directory does not exist");
        return false;
    }
    bool ScopedOk = true;
    for (const std::filesystem::directory_entry& ScopedEntry :
         std::filesystem::recursive_directory_iterator(ScopedRoot, ScopedCode))
    {
        if (ScopedCode || !ScopedEntry.is_regular_file())
        {
            continue;
        }
        if (!IsAssetMetaPath(ScopedEntry.path().string()))
        {
            continue;
        }
        std::ifstream ScopedStream(ScopedEntry.path(), std::ios::binary);
        if (!ScopedStream)
        {
            ScopedOk = false;
            continue;
        }
        const std::string ScopedText((std::istreambuf_iterator<char>(ScopedStream)), std::istreambuf_iterator<char>());
        if (!RegisterMetaFile(ScopedText, InError).IsValid())
        {
            ScopedOk = false;
        }
    }
    return ScopedOk;
}

void AssetDatabase::PushReload(AssetHandle InHandle, const AssetMeta& InMeta) noexcept
{
    const std::uint32_t ScopedNext = (MReloadHead + 1u) % kMaxAssetReloadNotices;
    if (ScopedNext == MReloadTail)
    {
        // Ring full: drop newest rather than overwrite an unseen notice
        return;
    }
    MReloadQueue[MReloadHead] = ReloadNotice{InHandle, InMeta.contentGeneration, InMeta.kind};
    MReloadHead = ScopedNext;
}

bool AssetDatabase::PopReload(ReloadNotice* Out) noexcept
{
    if (Out == nullptr || MReloadTail == MReloadHead)
    {
        return false;
    }
    *Out = MReloadQueue[MReloadTail];
    MReloadTail = (MReloadTail + 1u) % kMaxAssetReloadNotices;
    return true;
}
}
