// FemboyEngine2D - storefront ECS systems implementation.
#include "Engine/Platform/PlatformSystems.hpp"

#include "Engine/ECS/SystemGraph.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Fby::Platform
{
namespace
{

[[nodiscard]] std::uint32_t Load32(const std::byte* InData) noexcept
{
    return static_cast<std::uint32_t>(InData[0]) | (static_cast<std::uint32_t>(InData[1]) << 8) |
           (static_cast<std::uint32_t>(InData[2]) << 16) | (static_cast<std::uint32_t>(InData[3]) << 24);
}

[[nodiscard]] std::uint64_t Load64(const std::byte* InData) noexcept
{
    return static_cast<std::uint64_t>(Load32(InData)) | (static_cast<std::uint64_t>(Load32(InData + 4)) << 32);
}

void Store32(std::byte* OutData, std::uint32_t InValue) noexcept
{
    OutData[0] = static_cast<std::byte>(InValue & 0xFFu);
    OutData[1] = static_cast<std::byte>((InValue >> 8) & 0xFFu);
    OutData[2] = static_cast<std::byte>((InValue >> 16) & 0xFFu);
    OutData[3] = static_cast<std::byte>((InValue >> 24) & 0xFFu);
}

void Store64(std::byte* OutData, std::uint64_t InValue) noexcept
{
    Store32(OutData, static_cast<std::uint32_t>(InValue));
    Store32(OutData + 4, static_cast<std::uint32_t>(InValue >> 32));
}

[[nodiscard]] std::uint64_t Fnv1a(std::span<const std::byte> InBytes, std::uint64_t InSeed = 14695981039346656037ull) noexcept
{
    std::uint64_t ScopedHash = InSeed;
    for (const std::byte ScopedByte : InBytes)
    {
        ScopedHash ^= static_cast<std::uint64_t>(ScopedByte);
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

[[nodiscard]] std::string_view BoundedString(const char* InText, std::size_t InCapacity) noexcept
{
    std::size_t ScopedLength = 0;
    while (ScopedLength < InCapacity && InText[ScopedLength] != '\0')
    {
        ++ScopedLength;
    }
    return std::string_view(InText, ScopedLength);
}
}

void PlatformCommandLog::Clear() noexcept
{
    MStorage.fill(std::byte{0});
    std::memcpy(MStorage.data(), kPlatformLogMagic, 8);
    Store32(MStorage.data() + 8, kPlatformLogVersion);
    MWriteOffset = kHeaderBytes;
    MCount = 0;
    MFirstTick = 0;
    MLastTick = 0;
    MChecksum = 0;
}

bool PlatformCommandLog::Append(PlatformLogType InType, std::uint64_t InTick, std::uint64_t InId, std::string_view InName,
                                std::span<const std::byte> InData) noexcept
{
    if (InName.size() > 0xFFFFu || InData.size() > 4096u)
    {
        return false;
    }
    const std::uint64_t ScopedNeeded = kRecordBytes + InName.size() + InData.size();
    if (MWriteOffset + ScopedNeeded > MStorage.size() || MCount >= kPlatformLogMaxRecords)
    {
        return false;
    }
    std::byte* ScopedRecord = MStorage.data() + MWriteOffset;
    std::memset(ScopedRecord, 0, kRecordBytes);
    ScopedRecord[0] = static_cast<std::byte>(InType);
    ScopedRecord[1] = std::byte{0};
    ScopedRecord[2] = static_cast<std::byte>(InName.size() & 0xFFu);
    ScopedRecord[3] = static_cast<std::byte>((InName.size() >> 8) & 0xFFu);
    Store32(ScopedRecord + 4, MCount);
    Store64(ScopedRecord + 8, InTick);
    Store64(ScopedRecord + 16, InId);
    Store32(ScopedRecord + 24, static_cast<std::uint32_t>(InData.size()));
    Store32(ScopedRecord + 28, 0);
    std::memcpy(ScopedRecord + kRecordBytes, InName.data(), InName.size());
    if (!InData.empty())
    {
        std::memcpy(ScopedRecord + kRecordBytes + InName.size(), InData.data(), InData.size());
    }
    if (MCount == 0)
    {
        MFirstTick = InTick;
    }
    MLastTick = InTick;
    MWriteOffset += static_cast<std::uint32_t>(ScopedNeeded);
    ++MCount;
    return true;
}

void PlatformCommandLog::Seal() noexcept
{
    Store32(MStorage.data() + 8, kPlatformLogVersion);
    Store32(MStorage.data() + 12, MCount);
    Store32(MStorage.data() + 16, MWriteOffset);
    Store32(MStorage.data() + 20, 0);
    Store64(MStorage.data() + 24, MFirstTick);
    Store64(MStorage.data() + 32, MLastTick);
    Store64(MStorage.data() + 40, 0);
    MChecksum = Fnv1a(std::span<const std::byte>(MStorage.data(), MWriteOffset));
    Store64(MStorage.data() + 40, MChecksum);
}

bool PlatformCommandLog::Replay(PlatformLogSink InSink, void* InUser) const noexcept
{
    if (InSink == nullptr || std::memcmp(MStorage.data(), kPlatformLogMagic, 8) != 0)
    {
        return false;
    }
    if (Load32(MStorage.data() + 8) != kPlatformLogVersion)
    {
        return false;
    }
    const std::uint32_t ScopedCount = Load32(MStorage.data() + 12);
    const std::uint32_t ScopedBytes = Load32(MStorage.data() + 16);
    if (ScopedCount > kPlatformLogMaxRecords || ScopedBytes > MStorage.size() || ScopedBytes < kHeaderBytes)
    {
        return false;
    }
    // The checksum field itself is zero during Seal, so feed eight zero bytes
    const std::array<std::byte, 8> ScopedZeroChecksum{};
    std::uint64_t ScopedHash = Fnv1a(std::span<const std::byte>(MStorage.data(), 40));
    ScopedHash = Fnv1a(ScopedZeroChecksum, ScopedHash);
    ScopedHash = Fnv1a(std::span<const std::byte>(MStorage.data() + 48, ScopedBytes - 48), ScopedHash);
    if (Load64(MStorage.data() + 40) != ScopedHash)
    {
        return false;
    }
    std::uint32_t ScopedOffset = kHeaderBytes;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        if (ScopedOffset + kRecordBytes > ScopedBytes)
        {
            return false;
        }
        const std::byte* ScopedRecord = MStorage.data() + ScopedOffset;
        const std::uint16_t ScopedNameSize =
            static_cast<std::uint16_t>(static_cast<std::uint8_t>(ScopedRecord[2]) |
                                       (static_cast<std::uint16_t>(static_cast<std::uint8_t>(ScopedRecord[3])) << 8));
        const std::uint32_t ScopedDataSize = Load32(ScopedRecord + 24);
        if (ScopedOffset + kRecordBytes + ScopedNameSize + ScopedDataSize > ScopedBytes)
        {
            return false;
        }
        PlatformLogEvent ScopedEvent{};
        ScopedEvent.type = static_cast<PlatformLogType>(static_cast<std::uint8_t>(ScopedRecord[0]));
        ScopedEvent.tick = Load64(ScopedRecord + 8);
        ScopedEvent.id = Load64(ScopedRecord + 16);
        ScopedEvent.name = std::string_view(
            reinterpret_cast<const char*>(MStorage.data() + ScopedOffset + kRecordBytes), ScopedNameSize);
        ScopedEvent.data = std::span<const std::byte>(MStorage.data() + ScopedOffset + kRecordBytes + ScopedNameSize,
                                                      ScopedDataSize);
        InSink(InUser, ScopedEvent);
        ScopedOffset += kRecordBytes + ScopedNameSize + ScopedDataSize;
    }
    return ScopedOffset == ScopedBytes;
}

std::span<const std::byte> PlatformCommandLog::Bytes() const noexcept
{
    return std::span<const std::byte>(MStorage.data(), MWriteOffset);
}

void SteamAchievementSystem::Initialize(PlatformService& InService, PlatformCommandLog& InLog,
                                        std::uint64_t InStartTick) noexcept
{
    MService = &InService;
    MLog = &InLog;
    MNextTick = InStartTick;
    MCommitted = 0;
    MFailed = 0;
}

void SteamAchievementSystem::Update(Ecs::SystemContext& InContext)
{
    if (MService == nullptr)
    {
        return;
    }
    std::array<Entity, 64> ScopedEntities{};
    std::uint32_t ScopedCount = 0;
    auto ScopedView = InContext.registry.view<AchievementRequest>();
    for (const Entity ScopedEntity : ScopedView)
    {
        if (ScopedCount >= ScopedEntities.size())
        {
            break;
        }
        const AchievementRequest& ScopedRequest = ScopedView.get<AchievementRequest>(ScopedEntity);
        if ((ScopedRequest.flags & AchievementPending) != 0)
        {
            ScopedEntities[ScopedCount++] = ScopedEntity;
        }
    }
    // Deterministic order: id first, then entity. A random ECS iteration order
    for (std::uint32_t ScopedI = 1; ScopedI < ScopedCount; ++ScopedI)
    {
        const Entity ScopedValue = ScopedEntities[ScopedI];
        const AchievementRequest& ScopedValueRequest = ScopedView.get<AchievementRequest>(ScopedValue);
        std::uint32_t ScopedJ = ScopedI;
        while (ScopedJ > 0)
        {
            const Entity ScopedPrev = ScopedEntities[ScopedJ - 1];
            const AchievementRequest& ScopedPrevRequest = ScopedView.get<AchievementRequest>(ScopedPrev);
            const bool ScopedGreater = ScopedPrevRequest.achievementId > ScopedValueRequest.achievementId ||
                                       (ScopedPrevRequest.achievementId == ScopedValueRequest.achievementId &&
                                        static_cast<std::uint32_t>(ScopedPrev) > static_cast<std::uint32_t>(ScopedValue));
            if (!ScopedGreater)
            {
                break;
            }
            ScopedEntities[ScopedJ] = ScopedPrev;
            --ScopedJ;
        }
        ScopedEntities[ScopedJ] = ScopedValue;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        AchievementRequest& ScopedRequest = ScopedView.get<AchievementRequest>(ScopedEntities[ScopedI]);
        const std::string_view ScopedName = BoundedString(ScopedRequest.apiName, kAchievementNameBytes);
        const bool ScopedQueued = MService->QueueAchievement(ScopedRequest.achievementId, ScopedName);
        ScopedRequest.flags &= ~AchievementPending;
        if (ScopedQueued)
        {
            ScopedRequest.flags = (ScopedRequest.flags & ~AchievementFailed) | AchievementCommitted;
            ++MCommitted;
            if (MLog != nullptr)
            {
                (void)MLog->Append(PlatformLogType::Achievement, MNextTick, ScopedRequest.achievementId, ScopedName,
                                   std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ScopedRequest.points),
                                                              sizeof(ScopedRequest.points)));
            }
        }
        else
        {
            ScopedRequest.flags = (ScopedRequest.flags & ~AchievementCommitted) | AchievementFailed;
            ++MFailed;
        }
        ++MNextTick;
    }
}

bool SteamAchievementSystem::Register(Ecs::SystemGraph& InGraph, SteamAchievementSystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::FixedUpdate, "SteamAchievementSystem", &SteamAchievementSystem::Tick, &InSystem);
}

void SteamAchievementSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    static_cast<SteamAchievementSystem*>(InUser)->Update(InContext);
}

void EgsStoreSystem::Initialize(PlatformService& InService, PlatformCommandLog& InLog,
                                std::uint64_t InStartTick) noexcept
{
    MService = &InService;
    MLog = &InLog;
    MNextTick = InStartTick;
    MUploaded = 0;
    MFailed = 0;
    for (StagedSave& ScopedSave : MStaged)
    {
        ScopedSave = StagedSave{};
    }
}

EgsStoreSystem::StagedSave* EgsStoreSystem::Find(std::uint64_t InSaveId) noexcept
{
    for (StagedSave& ScopedSave : MStaged)
    {
        if (ScopedSave.used && ScopedSave.saveId == InSaveId)
        {
            return &ScopedSave;
        }
    }
    return nullptr;
}

bool EgsStoreSystem::StageWrite(std::uint64_t InSaveId, std::string_view InFileName,
                                std::span<const std::byte> InData) noexcept
{
    if (InData.size() > kStagedSaveBytes || InFileName.empty() || InFileName.size() >= kCloudNameBytes)
    {
        return false;
    }
    StagedSave* ScopedSlot = Find(InSaveId);
    if (ScopedSlot == nullptr)
    {
        for (StagedSave& ScopedCandidate : MStaged)
        {
            if (!ScopedCandidate.used)
            {
                ScopedSlot = &ScopedCandidate;
                break;
            }
        }
    }
    if (ScopedSlot == nullptr)
    {
        return false;
    }
    ScopedSlot->used = true;
    ScopedSlot->saveId = InSaveId;
    ScopedSlot->size = static_cast<std::uint32_t>(InData.size());
    if (!InData.empty())
    {
        std::memcpy(ScopedSlot->data.data(), InData.data(), InData.size());
    }
    (void)InFileName;
    return true;
}

void EgsStoreSystem::Update(Ecs::SystemContext& InContext)
{
    if (MService == nullptr)
    {
        return;
    }
    std::array<Entity, 64> ScopedEntities{};
    std::uint32_t ScopedCount = 0;
    auto ScopedView = InContext.registry.view<CloudSaveComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        if (ScopedCount >= ScopedEntities.size())
        {
            break;
        }
        const CloudSaveComponent& ScopedSave = ScopedView.get<CloudSaveComponent>(ScopedEntity);
        if ((ScopedSave.flags & (CloudSaveDirty | CloudSaveReadRequested)) != 0)
        {
            ScopedEntities[ScopedCount++] = ScopedEntity;
        }
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        CloudSaveComponent& ScopedSave = ScopedView.get<CloudSaveComponent>(ScopedEntities[ScopedI]);
        const std::string_view ScopedName = BoundedString(ScopedSave.fileName, kCloudNameBytes);
        if ((ScopedSave.flags & CloudSaveDirty) != 0)
        {
            const StagedSave* ScopedStaged = Find(ScopedSave.saveId);
            if (ScopedStaged != nullptr &&
                MService->QueueCloudWrite(ScopedSave.saveId, ScopedName,
                                          std::span<const std::byte>(ScopedStaged->data.data(), ScopedStaged->size)))
            {
                ScopedSave.flags &= ~(CloudSaveDirty | CloudSaveFailed);
                ScopedSave.bytes = ScopedStaged->size;
                ++MUploaded;
                if (MLog != nullptr)
                {
                    (void)MLog->Append(PlatformLogType::CloudWrite, MNextTick, ScopedSave.saveId, ScopedName,
                                       std::span<const std::byte>(ScopedStaged->data.data(), ScopedStaged->size));
                }
            }
            else
            {
                ScopedSave.flags = (ScopedSave.flags & ~CloudSaveDirty) | CloudSaveFailed;
                ++MFailed;
            }
        }
        else if ((ScopedSave.flags & CloudSaveReadRequested) != 0)
        {
            if (MService->QueueCloudRead(ScopedSave.saveId, ScopedName))
            {
                ScopedSave.flags &= ~(CloudSaveReadRequested | CloudSaveFailed);
                if (MLog != nullptr)
                {
                    (void)MLog->Append(PlatformLogType::CloudRead, MNextTick, ScopedSave.saveId, ScopedName);
                }
            }
            else
            {
                ScopedSave.flags = (ScopedSave.flags & ~CloudSaveReadRequested) | CloudSaveFailed;
                ++MFailed;
            }
        }
        ++MNextTick;
    }
}

bool EgsStoreSystem::Register(Ecs::SystemGraph& InGraph, EgsStoreSystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::PostUpdate, "EgsStoreSystem", &EgsStoreSystem::Tick, &InSystem);
}

void EgsStoreSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    static_cast<EgsStoreSystem*>(InUser)->Update(InContext);
}

// GOG Galaxy systems
void GogAchievementSystem::Initialize(PlatformService& InService, PlatformCommandLog& InLog,
                                      std::uint64_t InStartTick) noexcept
{
    MService = &InService;
    MLog = &InLog;
    MNextTick = InStartTick;
    MCommitted = 0;
    MFailed = 0;
    MDuplicates = 0;
}

void GogAchievementSystem::Update(Ecs::SystemContext& InContext)
{
    if (MService == nullptr)
    {
        return;
    }
    std::array<Entity, 64> ScopedEntities{};
    std::uint32_t ScopedCount = 0;
    auto ScopedView = InContext.registry.view<AchievementRequest>();
    for (const Entity ScopedEntity : ScopedView)
    {
        if (ScopedCount >= ScopedEntities.size())
        {
            break;
        }
        const AchievementRequest& ScopedRequest = ScopedView.get<AchievementRequest>(ScopedEntity);
        if ((ScopedRequest.flags & AchievementPending) != 0)
        {
            ScopedEntities[ScopedCount++] = ScopedEntity;
        }
    }
    // Same deterministic ordering rule as the Steam path: id, then entity. The
    for (std::uint32_t ScopedI = 1; ScopedI < ScopedCount; ++ScopedI)
    {
        const Entity ScopedValue = ScopedEntities[ScopedI];
        const AchievementRequest& ScopedValueRequest = ScopedView.get<AchievementRequest>(ScopedValue);
        std::uint32_t ScopedJ = ScopedI;
        while (ScopedJ > 0)
        {
            const Entity ScopedPrev = ScopedEntities[ScopedJ - 1];
            const AchievementRequest& ScopedPrevRequest = ScopedView.get<AchievementRequest>(ScopedPrev);
            const bool ScopedGreater = ScopedPrevRequest.achievementId > ScopedValueRequest.achievementId ||
                                       (ScopedPrevRequest.achievementId == ScopedValueRequest.achievementId &&
                                        static_cast<std::uint32_t>(ScopedPrev) > static_cast<std::uint32_t>(ScopedValue));
            if (!ScopedGreater)
            {
                break;
            }
            ScopedEntities[ScopedJ] = ScopedPrev;
            --ScopedJ;
        }
        ScopedEntities[ScopedJ] = ScopedValue;
    }

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        AchievementRequest& ScopedRequest = ScopedView.get<AchievementRequest>(ScopedEntities[ScopedI]);
        if (ScopedI > 0)
        {
            const AchievementRequest& ScopedPrevious = ScopedView.get<AchievementRequest>(ScopedEntities[ScopedI - 1]);
            if (ScopedPrevious.achievementId == ScopedRequest.achievementId)
            {
                // Two components requested the same achievement in one frame.
                ScopedRequest.flags &= ~AchievementPending;
                ++MDuplicates;
                continue;
            }
        }
        const std::string_view ScopedName = BoundedString(ScopedRequest.apiName, kAchievementNameBytes);
        const bool ScopedQueued = MService->QueueAchievement(ScopedRequest.achievementId, ScopedName);
        ScopedRequest.flags &= ~AchievementPending;
        if (ScopedQueued)
        {
            ScopedRequest.flags = (ScopedRequest.flags & ~AchievementFailed) | AchievementCommitted;
            ++MCommitted;
            if (MLog != nullptr)
            {
                (void)MLog->Append(PlatformLogType::Achievement, MNextTick, ScopedRequest.achievementId, ScopedName,
                                   std::span<const std::byte>(reinterpret_cast<const std::byte*>(&ScopedRequest.points),
                                                              sizeof(ScopedRequest.points)));
            }
        }
        else
        {
            ScopedRequest.flags = (ScopedRequest.flags & ~AchievementCommitted) | AchievementFailed;
            ++MFailed;
        }
        ++MNextTick;
    }
}

bool GogAchievementSystem::Register(Ecs::SystemGraph& InGraph, GogAchievementSystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::PostUpdate, "GogAchievementSystem", &GogAchievementSystem::Tick, &InSystem);
}

void GogAchievementSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    static_cast<GogAchievementSystem*>(InUser)->Update(InContext);
}

void GogStorageSystem::Initialize(PlatformService& InService, PlatformCommandLog& InLog,
                                  std::uint64_t InStartTick) noexcept
{
    MService = &InService;
    MLog = &InLog;
    MNextTick = InStartTick;
    MUploaded = 0;
    MDownloaded = 0;
    MFailed = 0;
    MConflicts = 0;
    for (StagedSave& ScopedSave : MStaged)
    {
        ScopedSave = StagedSave{};
    }
}

GogStorageSystem::StagedSave* GogStorageSystem::Find(std::uint64_t InSaveId) noexcept
{
    for (StagedSave& ScopedSave : MStaged)
    {
        if (ScopedSave.used && ScopedSave.saveId == InSaveId)
        {
            return &ScopedSave;
        }
    }
    return nullptr;
}

bool GogStorageSystem::StageWrite(std::uint64_t InSaveId, std::string_view InFileName,
                                  std::span<const std::byte> InData) noexcept
{
    if (InData.size() > kStagedSaveBytes || InFileName.empty() || InFileName.size() >= kCloudNameBytes)
    {
        return false;
    }
    StagedSave* ScopedSlot = Find(InSaveId);
    if (ScopedSlot == nullptr)
    {
        for (StagedSave& ScopedCandidate : MStaged)
        {
            if (!ScopedCandidate.used)
            {
                ScopedSlot = &ScopedCandidate;
                break;
            }
        }
    }
    if (ScopedSlot == nullptr)
    {
        return false;
    }
    const bool ScopedOverwrite =
        ScopedSlot->used && (ScopedSlot->size != InData.size() ||
                             std::memcmp(ScopedSlot->data.data(), InData.data(), InData.size()) != 0);
    if (ScopedOverwrite)
    {
        // Two producers staged different bytes for the same save id before the
        ++MConflicts;
    }
    ScopedSlot->used = true;
    ScopedSlot->saveId = InSaveId;
    ScopedSlot->size = static_cast<std::uint32_t>(InData.size());
    if (!InData.empty())
    {
        std::memcpy(ScopedSlot->data.data(), InData.data(), InData.size());
    }
    (void)InFileName;
    return true;
}

void GogStorageSystem::Update(Ecs::SystemContext& InContext)
{
    if (MService == nullptr)
    {
        return;
    }
    std::array<Entity, 64> ScopedEntities{};
    std::uint32_t ScopedCount = 0;
    auto ScopedView = InContext.registry.view<CloudSaveComponent>();
    for (const Entity ScopedEntity : ScopedView)
    {
        if (ScopedCount >= ScopedEntities.size())
        {
            break;
        }
        const CloudSaveComponent& ScopedSave = ScopedView.get<CloudSaveComponent>(ScopedEntity);
        if ((ScopedSave.flags & (CloudSaveDirty | CloudSaveReadRequested)) != 0)
        {
            ScopedEntities[ScopedCount++] = ScopedEntity;
        }
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
    {
        CloudSaveComponent& ScopedSave = ScopedView.get<CloudSaveComponent>(ScopedEntities[ScopedI]);
        const std::string_view ScopedName = BoundedString(ScopedSave.fileName, kCloudNameBytes);
        if ((ScopedSave.flags & CloudSaveDirty) != 0)
        {
            const StagedSave* ScopedStaged = Find(ScopedSave.saveId);
            if (ScopedStaged != nullptr &&
                MService->QueueCloudWrite(ScopedSave.saveId, ScopedName,
                                          std::span<const std::byte>(ScopedStaged->data.data(), ScopedStaged->size)))
            {
                ScopedSave.flags &= ~(CloudSaveDirty | CloudSaveFailed);
                ScopedSave.bytes = ScopedStaged->size;
                ++MUploaded;
                if (MLog != nullptr)
                {
                    (void)MLog->Append(PlatformLogType::CloudWrite, MNextTick, ScopedSave.saveId, ScopedName,
                                       std::span<const std::byte>(ScopedStaged->data.data(), ScopedStaged->size));
                }
            }
            else
            {
                ScopedSave.flags = (ScopedSave.flags & ~CloudSaveDirty) | CloudSaveFailed;
                ++MFailed;
            }
        }
        else if ((ScopedSave.flags & CloudSaveReadRequested) != 0)
        {
            if (MService->QueueCloudRead(ScopedSave.saveId, ScopedName))
            {
                ScopedSave.flags &= ~(CloudSaveReadRequested | CloudSaveFailed);
                ++MDownloaded;
                if (MLog != nullptr)
                {
                    (void)MLog->Append(PlatformLogType::CloudRead, MNextTick, ScopedSave.saveId, ScopedName);
                }
            }
            else
            {
                ScopedSave.flags = (ScopedSave.flags & ~CloudSaveReadRequested) | CloudSaveFailed;
                ++MFailed;
            }
        }
        ++MNextTick;
    }
}

bool GogStorageSystem::Register(Ecs::SystemGraph& InGraph, GogStorageSystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::PostUpdate, "GogStorageSystem", &GogStorageSystem::Tick, &InSystem);
}

void GogStorageSystem::Tick(Ecs::SystemContext& InContext, void* InUser)
{
    static_cast<GogStorageSystem*>(InUser)->Update(InContext);
}
}
