// FemboyEngine2D - transactional command stack implementation.
#include "Editor/Infrastructure/CommandStack.hpp"

#include "Engine/Core/Mirror.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <new>

namespace Fby::Editor
{
namespace
{
constexpr std::uint32_t AlignUp(std::uint32_t InValue, std::uint32_t InAlignment) noexcept
{
    return (InValue + InAlignment - 1u) & ~(InAlignment - 1u);
}

[[nodiscard]] std::uint32_t HashLabel(std::string_view InLabel) noexcept
{
    return static_cast<std::uint32_t>(Reflect::Fnv1a(InLabel));
}

// Coalesces adjacent/near-adjacent changed bytes so a struct edit stores tens of
template <typename Consumer>
void ForEachChangedRange(std::span<const std::byte> InOldBytes, std::span<const std::byte> InNewBytes,
                         Consumer&& InConsumer) noexcept
{
    const std::size_t ScopedSize = std::min(InOldBytes.size(), InNewBytes.size());
    std::size_t ScopedIndex = 0;
    while (ScopedIndex < ScopedSize)
    {
        if (InOldBytes[ScopedIndex] == InNewBytes[ScopedIndex])
        {
            ++ScopedIndex;
            continue;
        }
        const std::size_t ScopedStart = ScopedIndex;
        std::size_t ScopedGap = 0;
        while (ScopedIndex < ScopedSize)
        {
            if (InOldBytes[ScopedIndex] != InNewBytes[ScopedIndex])
            {
                ++ScopedIndex;
                ScopedGap = 0;
                continue;
            }
            if (ScopedGap < 3 && ScopedIndex + 1 < ScopedSize)
            {
                ++ScopedGap;
                ++ScopedIndex;
                continue;
            }
            break;
        }
        InConsumer(static_cast<std::uint32_t>(ScopedStart),
                   static_cast<std::uint32_t>((ScopedIndex - ScopedGap) - ScopedStart));
    }
}
}

bool CommandStack::Initialize(const Config& InConfig)
{
    Shutdown();
    MPayloadCapacity = InConfig.payloadBytes < (64u << 10) ? (64u << 10) : InConfig.payloadBytes;
    MPayload = new (std::nothrow) std::byte[MPayloadCapacity];
    if (MPayload == nullptr)
    {
        MPayloadCapacity = 0;
        return false;
    }
    MCommands.fill(Command{});
    MCodecs.fill(Codec{});
    MPending.fill(PendingEdit{});
    MPendingSpawns.fill(PendingSpawn{});
    MCodecCount = 0;
    MCommandCount = 0;
    MCursor = 0;
    MArenaUsed = 0;
    MPendingCount = 0;
    MPendingSpawnCount = 0;
    MTransactionOpen = false;
    MStats = {};
    MStats.bytesCapacity = static_cast<std::uint32_t>(MPayloadCapacity);
    return true;
}

void CommandStack::Shutdown() noexcept
{
    delete[] MPayload;
    MPayload = nullptr;
    MPayloadCapacity = 0;
    MCommandCount = 0;
    MCursor = 0;
    MArenaUsed = 0;
    MTransactionOpen = false;
    MPendingCount = 0;
    MPendingSpawnCount = 0;
    MRegistry = nullptr;
    MComponents = nullptr;
}

void CommandStack::Bind(entt::registry* InRegistry, const Scene::ComponentRegistry* InComponents) noexcept
{
    MRegistry = InRegistry;
    MComponents = InComponents;
}

std::uint16_t CommandStack::RegisterComponentOps(const Scene::ComponentOps& InOps) noexcept
{
    // Empty marker components (Hidden, EditorSelected-like tags) are valid
    if (MCodecCount >= kMaxCommandCodecs || (InOps.size == 0 && !InOps.empty) ||
        InOps.size > kMaxTrackedComponentBytes || InOps.get == nullptr || InOps.getMutable == nullptr)
    {
        return 0xFFFFu;
    }
    Codec ScopedCodec;
    ScopedCodec.name = InOps.name;
    ScopedCodec.layoutHash = InOps.layoutHash;
    ScopedCodec.size = InOps.size;
    ScopedCodec.get = InOps.get;
    ScopedCodec.getMutable = InOps.getMutable;
    ScopedCodec.has = InOps.has;
    ScopedCodec.emplaceBytes = InOps.emplaceBytes;
    ScopedCodec.remove = InOps.remove;
    const auto ScopedIndex = static_cast<std::uint16_t>(MCodecCount);
    MCodecs[MCodecCount++] = ScopedCodec;
    return ScopedIndex;
}

std::uint16_t CommandStack::RegisterExternalBuffer(const char* InName, void* InData, std::uint32_t InSize) noexcept
{
    if (MCodecCount >= kMaxCommandCodecs || InName == nullptr || InData == nullptr || InSize == 0 ||
        InSize > kMaxTrackedComponentBytes)
    {
        return 0xFFFFu;
    }
    Codec ScopedCodec;
    ScopedCodec.name = InName;
    ScopedCodec.layoutHash = Reflect::Fnv1a(InName);
    ScopedCodec.size = InSize;
    ScopedCodec.external = InData;
    const auto ScopedIndex = static_cast<std::uint16_t>(MCodecCount);
    MCodecs[MCodecCount++] = ScopedCodec;
    return ScopedIndex;
}

const CommandStack::Codec* CommandStack::CodecAt(std::uint16_t InCodec) const noexcept
{
    return InCodec < MCodecCount ? &MCodecs[InCodec] : nullptr;
}

const char* CommandStack::CodecName(std::uint16_t InCodec) const noexcept
{
    const Codec* ScopedEntry = CodecAt(InCodec);
    return ScopedEntry != nullptr && !ScopedEntry->name.empty() ? ScopedEntry->name.data() : "?";
}

std::uint32_t CommandStack::CodecSize(std::uint16_t InCodec) const noexcept
{
    const Codec* ScopedEntry = CodecAt(InCodec);
    return ScopedEntry != nullptr ? ScopedEntry->size : 0u;
}

std::uint16_t CommandStack::CodecForLayoutHash(std::uint64_t InLayoutHash) const noexcept
{
    for (std::uint16_t ScopedI = 0; ScopedI < MCodecCount; ++ScopedI)
    {
        if (MCodecs[ScopedI].layoutHash == InLayoutHash)
        {
            return ScopedI;
        }
    }
    return 0xFFFFu;
}

CommandStack::Result CommandStack::CaptureComponent(Entity InEntity, std::uint16_t InCodec,
                                                    std::span<std::byte> Out) const noexcept
{
    const Codec* ScopedEntry = CodecAt(InCodec);
    if (ScopedEntry == nullptr)
    {
        return Result::UnknownCodec;
    }
    if (Out.size() < ScopedEntry->size)
    {
        return Result::InvalidArgument;
    }
    if (ScopedEntry->external != nullptr)
    {
        std::memcpy(Out.data(), ScopedEntry->external, ScopedEntry->size);
        return Result::Ok;
    }
    if (MRegistry == nullptr)
    {
        return Result::NotBound;
    }
    if (!MRegistry->valid(InEntity))
    {
        return Result::InvalidArgument;
    }
    const void* ScopedComponent = ScopedEntry->get(*MRegistry, InEntity);
    if (ScopedComponent == nullptr)
    {
        return Result::StaleEntity;
    }
    std::memcpy(Out.data(), ScopedComponent, ScopedEntry->size);
    return Result::Ok;
}

CommandStack::Result CommandStack::ApplyComponentBytes(Entity InEntity, std::uint16_t InCodec,
                                                       std::span<const std::byte> InBytes) noexcept
{
    const Codec* ScopedEntry = CodecAt(InCodec);
    if (ScopedEntry == nullptr)
    {
        return Result::UnknownCodec;
    }
    if (InBytes.size() != ScopedEntry->size)
    {
        return Result::InvalidArgument;
    }
    if (ScopedEntry->external != nullptr)
    {
        std::memcpy(ScopedEntry->external, InBytes.data(), ScopedEntry->size);
        return Result::Ok;
    }
    if (MRegistry == nullptr)
    {
        return Result::NotBound;
    }
    if (!MRegistry->valid(InEntity))
    {
        return Result::InvalidArgument;
    }
    void* ScopedComponent = ScopedEntry->getMutable(*MRegistry, InEntity);
    if (ScopedComponent == nullptr)
    {
        return Result::StaleEntity;
    }
    std::memcpy(ScopedComponent, InBytes.data(), ScopedEntry->size);
    return Result::Ok;
}

CommandStack::Result CommandStack::BeginTransaction(std::string_view InLabel) noexcept
{
    if (!Initialized())
    {
        return Result::NotInitialized;
    }
    if (MTransactionOpen)
    {
        return Result::TransactionOpen;
    }
    MTransactionOpen = true;
    // Starting a new edit after Undo discards the redo tail and its payload
    if (MCursor < MCommandCount)
    {
        MCommandCount = MCursor;
        MStats.commands = MCommandCount;
        MStats.redoDepth = 0;
    }
    MTransactionStart = MCursor > 0 ? static_cast<std::size_t>(MCommands[MCursor - 1].payloadOffset) +
                                          MCommands[MCursor - 1].payloadSize
                                    : 0;
    MArenaUsed = MTransactionStart;
    MPendingCount = 0;
    MPendingSpawnCount = 0;
    MPendingDestroyCount = 0;
    const std::size_t ScopedLength = std::min<std::size_t>(InLabel.size(), kMaxCommandLabel - 1);
    std::memset(MTransactionLabel, 0, sizeof(MTransactionLabel));
    std::memcpy(MTransactionLabel, InLabel.data(), ScopedLength);
    return Result::Ok;
}

CommandStack::Result CommandStack::Record(Entity InEntity, std::uint16_t InCodec, std::span<const std::byte> InOldBytes,
                                          std::span<const std::byte> InNewBytes) noexcept
{
    if (!MTransactionOpen)
    {
        return Result::NoOpenTransaction;
    }
    const Codec* ScopedEntry = CodecAt(InCodec);
    if (ScopedEntry == nullptr)
    {
        return Result::UnknownCodec;
    }
    if (InOldBytes.size() != ScopedEntry->size || InNewBytes.size() != ScopedEntry->size)
    {
        return Result::InvalidArgument;
    }
    if (ScopedEntry->external == nullptr)
    {
        if (MRegistry == nullptr || !MRegistry->valid(InEntity))
        {
            return Result::StaleEntity;
        }
    }

    PendingEdit* ScopedPending = nullptr;
    for (std::uint32_t ScopedI = 0; ScopedI < MPendingCount; ++ScopedI)
    {
        if (MPending[ScopedI].active && MPending[ScopedI].entity == InEntity && MPending[ScopedI].codec == InCodec)
        {
            ScopedPending = &MPending[ScopedI];
            break;
        }
    }
    const bool ScopedFirstEdit = ScopedPending == nullptr;
    if (ScopedFirstEdit)
    {
        if (MPendingCount >= kMaxTransactionEdits)
        {
            return Result::StackFull;
        }
        ScopedPending = &MPending[MPendingCount++];
        ScopedPending->entity = InEntity;
        ScopedPending->codec = InCodec;
        ScopedPending->active = true;
        ScopedPending->oldOffset = AlignUp(static_cast<std::uint32_t>(MArenaUsed), 16);
        ScopedPending->newOffset = ScopedPending->oldOffset + ScopedEntry->size;
        if (static_cast<std::size_t>(ScopedPending->newOffset) + ScopedEntry->size > MPayloadCapacity)
        {
            ScopedPending->active = false;
            --MPendingCount;
            return Result::PayloadFull;
        }
        MArenaUsed = ScopedPending->newOffset + ScopedEntry->size;
        std::memcpy(MPayload + ScopedPending->oldOffset, InOldBytes.data(), ScopedEntry->size);
    }
    // Repeated edits during one drag overwrite the "new" side: the transaction
    std::memcpy(MPayload + ScopedPending->newOffset, InNewBytes.data(), ScopedEntry->size);
    return Result::Ok;
}

CommandStack::Result CommandStack::RecordAfterMutation(Entity InEntity, std::uint16_t InCodec,
                                                       std::span<const std::byte> InOldBytes) noexcept
{
    const Codec* ScopedEntry = CodecAt(InCodec);
    if (ScopedEntry == nullptr)
    {
        return Result::UnknownCodec;
    }
    std::array<std::byte, kMaxTrackedComponentBytes> ScopedCurrent{};
    const Result ScopedCaptured =
        CaptureComponent(InEntity, InCodec, std::span<std::byte>(ScopedCurrent.data(), ScopedEntry->size));
    if (ScopedCaptured != Result::Ok)
    {
        return ScopedCaptured;
    }
    return Record(InEntity, InCodec, InOldBytes, std::span<const std::byte>(ScopedCurrent.data(), ScopedEntry->size));
}

CommandStack::Result CommandStack::RecordSpawn(Entity InEntity) noexcept
{
    if (!MTransactionOpen)
    {
        return Result::NoOpenTransaction;
    }
    if (MRegistry == nullptr)
    {
        return Result::NotBound;
    }
    if (!MRegistry->valid(InEntity))
    {
        return Result::InvalidArgument;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MPendingSpawnCount; ++ScopedI)
    {
        if (MPendingSpawns[ScopedI].active && MPendingSpawns[ScopedI].entity == InEntity)
        {
            return Result::Ok;
        }
    }
    if (MPendingSpawnCount >= MPendingSpawns.size())
    {
        return Result::StackFull;
    }
    MPendingSpawns[MPendingSpawnCount++] = PendingSpawn{InEntity, true};
    return Result::Ok;
}

CommandStack::Result CommandStack::RecordDestroy(Entity InEntity) noexcept
{
    if (!MTransactionOpen)
    {
        return Result::NoOpenTransaction;
    }
    if (MRegistry == nullptr)
    {
        return Result::NotBound;
    }
    if (!MRegistry->valid(InEntity))
    {
        return Result::InvalidArgument;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MPendingDestroyCount; ++ScopedI)
    {
        if (MPendingDestroys[ScopedI].active && MPendingDestroys[ScopedI].entity == InEntity)
        {
            return Result::Ok;
        }
    }
    if (MPendingDestroyCount >= MPendingDestroys.size())
    {
        return Result::StackFull;
    }
    Command ScopedBase;
    ScopedBase.labelHash = HashLabel(MTransactionLabel);
    std::memcpy(ScopedBase.label, MTransactionLabel, sizeof(ScopedBase.label));
    Command ScopedCommand;
    const Result ScopedBuilt = BuildSpawn(InEntity, ScopedBase, &ScopedCommand);
    if (ScopedBuilt != Result::Ok)
    {
        return ScopedBuilt;
    }
    ScopedCommand.kind = CommandKind::Destroy;
    MPendingDestroyCommands[MPendingDestroyCount] = ScopedCommand;
    MPendingDestroys[MPendingDestroyCount] = PendingSpawn{InEntity, true};
    ++MPendingDestroyCount;
    return Result::Ok;
}

std::size_t CommandStack::MeasureSpawn(Entity InEntity, bool* InValid) const noexcept
{
    if (InValid != nullptr)
    {
        *InValid = false;
    }
    if (MRegistry == nullptr || !MRegistry->valid(InEntity))
    {
        return 0;
    }
    std::uint32_t ScopedComponentCount = 0;
    std::size_t ScopedByteCount = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MCodecCount; ++ScopedI)
    {
        const Codec& ScopedCodec = MCodecs[ScopedI];
        if (ScopedCodec.external != nullptr || ScopedCodec.has == nullptr || ScopedCodec.emplaceBytes == nullptr)
        {
            continue;
        }
        if (!ScopedCodec.has(*MRegistry, InEntity))
        {
            continue;
        }
        ++ScopedComponentCount;
        ScopedByteCount += ScopedCodec.size;
    }
    const std::size_t ScopedTotal =
        sizeof(SpawnHeader) + static_cast<std::size_t>(ScopedComponentCount) * sizeof(SpawnComponent) + ScopedByteCount;
    if (InValid != nullptr)
    {
        *InValid = ScopedTotal <= kMaxSpawnSnapshotBytes;
    }
    return ScopedTotal;
}

CommandStack::Result CommandStack::BuildSpawn(Entity InEntity, const Command& InBase, Command* Out) noexcept
{
    bool ScopedValid = false;
    const std::size_t ScopedMeasured = MeasureSpawn(InEntity, &ScopedValid);
    if (!ScopedValid)
    {
        return Result::InvalidArgument;
    }
    if (MArenaUsed + ScopedMeasured > MPayloadCapacity)
    {
        return Result::PayloadFull;
    }
    auto* ScopedHeader = reinterpret_cast<SpawnHeader*>(MPayload + MArenaUsed);
    auto* ScopedEntries = reinterpret_cast<SpawnComponent*>(MPayload + MArenaUsed + sizeof(SpawnHeader));
    std::uint16_t ScopedIndex = 0;
    std::uint32_t ScopedByteOffset = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < MCodecCount; ++ScopedI)
    {
        const Codec& ScopedCodec = MCodecs[ScopedI];
        if (ScopedCodec.external != nullptr || ScopedCodec.has == nullptr || ScopedCodec.emplaceBytes == nullptr ||
            !ScopedCodec.has(*MRegistry, InEntity))
        {
            continue;
        }
        ScopedEntries[ScopedIndex] =
            SpawnComponent{static_cast<std::uint16_t>(ScopedI), 0, ScopedByteOffset, ScopedCodec.size};
        ++ScopedIndex;
        ScopedByteOffset += ScopedCodec.size;
    }
    ScopedHeader->componentCount = ScopedIndex;
    ScopedHeader->byteCount = ScopedByteOffset;
    std::byte* ScopedBytes =
        MPayload + MArenaUsed + sizeof(SpawnHeader) + static_cast<std::size_t>(ScopedIndex) * sizeof(SpawnComponent);
    std::uint32_t ScopedCursor = 0;
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedIndex; ++ScopedI)
    {
        const Codec& ScopedCodec = MCodecs[ScopedEntries[ScopedI].codec];
        if (ScopedCodec.size == 0)
        {
            // Marker component: emplaceBytes() creates it without data
            continue;
        }
        const void* ScopedComponent = ScopedCodec.get(*MRegistry, InEntity);
        if (ScopedComponent == nullptr)
        {
            return Result::StaleEntity;
        }
        std::memcpy(ScopedBytes + ScopedCursor, ScopedComponent, ScopedCodec.size);
        ScopedCursor += ScopedCodec.size;
    }
    Command ScopedCommand = InBase;
    ScopedCommand.entity = InEntity;
    ScopedCommand.kind = CommandKind::Spawn;
    ScopedCommand.codec = 0xFFFFu;
    ScopedCommand.rangeCount = 0;
    ScopedCommand.payloadOffset = static_cast<std::uint32_t>(MArenaUsed);
    ScopedCommand.payloadSize = static_cast<std::uint32_t>(ScopedMeasured);
    ScopedCommand.groupSize = 0;
    *Out = ScopedCommand;
    MArenaUsed += ScopedMeasured;
    return Result::Ok;
}

CommandStack::Result CommandStack::PushCommand(const Command& InCommand, std::span<const std::byte> InPayload) noexcept
{
    // A new command discards the redo tail.
    MCommandCount = MCursor;
    if (MCommandCount >= MCommands.size())
    {
        return Result::StackFull;
    }
    Command ScopedStored = InCommand;
    if (!InPayload.empty())
    {
        if (MArenaUsed + InPayload.size() > MPayloadCapacity)
        {
            return Result::PayloadFull;
        }
        ScopedStored.payloadOffset = static_cast<std::uint32_t>(MArenaUsed);
        ScopedStored.payloadSize = static_cast<std::uint32_t>(InPayload.size());
        std::memcpy(MPayload + ScopedStored.payloadOffset, InPayload.data(), InPayload.size());
        MArenaUsed += InPayload.size();
    }
    MCommands[MCursor] = ScopedStored;
    ++MCursor;
    ++MCommandCount;
    return Result::Ok;
}

void CommandStack::ResetTransactionBuffer() noexcept
{
    // Old/new scratch regions are transient; roll the arena back to the point
    MArenaUsed = MTransactionStart;
    MPendingCount = 0;
    MPendingSpawnCount = 0;
    MPendingDestroyCount = 0;
    MTransactionOpen = false;
}

void CommandStack::CancelTransaction() noexcept
{
    if (!MTransactionOpen)
    {
        return;
    }
    ResetTransactionBuffer();
}

CommandStack::Result CommandStack::CommitTransaction() noexcept
{
    if (!MTransactionOpen)
    {
        return Result::NoOpenTransaction;
    }
    if (MPendingCount == 0 && MPendingSpawnCount == 0 && MPendingDestroyCount == 0)
    {
        CancelTransaction();
        return Result::Ok;
    }

    // Build each command payload after the scratch region, then compact the
    std::array<Command, kMaxTransactionEdits + kMaxSpawnedEntitiesPerTransaction * 2> ScopedBuilt{};
    std::uint32_t ScopedBuiltCount = 0;
    std::size_t ScopedFirstPayload = MArenaUsed;
    // Destroy payloads were captured when RecordDestroy ran, so they sit before
    // everything the commit builds. Keeping them first preserves the ascending
    // source order the compaction below relies on.
    for (std::uint32_t ScopedI = 0; ScopedI < MPendingDestroyCount; ++ScopedI)
    {
        if (!MPendingDestroys[ScopedI].active)
        {
            continue;
        }
        ScopedBuilt[ScopedBuiltCount++] = MPendingDestroyCommands[ScopedI];
    }
    Command ScopedSpawnBase;
    ScopedSpawnBase.groupSize = 0;
    ScopedSpawnBase.labelHash = HashLabel(MTransactionLabel);
    std::memcpy(ScopedSpawnBase.label, MTransactionLabel, sizeof(ScopedSpawnBase.label));
    for (std::uint32_t ScopedI = 0; ScopedI < MPendingSpawnCount; ++ScopedI)
    {
        if (!MPendingSpawns[ScopedI].active)
        {
            continue;
        }
        if (ScopedBuiltCount >= ScopedBuilt.size())
        {
            CancelTransaction();
            return Result::StackFull;
        }
        const Result ScopedSpawned =
            BuildSpawn(MPendingSpawns[ScopedI].entity, ScopedSpawnBase, &ScopedBuilt[ScopedBuiltCount]);
        if (ScopedSpawned != Result::Ok)
        {
            CancelTransaction();
            return ScopedSpawned;
        }
        ++ScopedBuiltCount;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MPendingCount; ++ScopedI)
    {
        PendingEdit& ScopedPending = MPending[ScopedI];
        if (!ScopedPending.active)
        {
            continue;
        }
        const Codec& ScopedCodec = MCodecs[ScopedPending.codec];
        const std::span<const std::byte> ScopedOldBytes(MPayload + ScopedPending.oldOffset, ScopedCodec.size);
        const std::span<const std::byte> ScopedNewBytes(MPayload + ScopedPending.newOffset, ScopedCodec.size);

        std::uint32_t ScopedRangeCount = 0;
        std::uint32_t ScopedByteCount = 0;
        ForEachChangedRange(ScopedOldBytes, ScopedNewBytes,
                            [&](std::uint32_t, std::uint32_t InLength)
                            {
                                ++ScopedRangeCount;
                                ScopedByteCount += InLength;
                            });
        if (ScopedRangeCount == 0)
        {
            continue;
        }
        const std::size_t ScopedPayloadSize =
            static_cast<std::size_t>(ScopedRangeCount) * sizeof(Range) + static_cast<std::size_t>(ScopedByteCount) * 2u;
        if (MArenaUsed + ScopedPayloadSize > MPayloadCapacity || ScopedBuiltCount >= ScopedBuilt.size())
        {
            CancelTransaction();
            return Result::PayloadFull;
        }
        auto* ScopedRanges = reinterpret_cast<Range*>(MPayload + MArenaUsed);
        std::byte* ScopedOldRegion = MPayload + MArenaUsed + static_cast<std::size_t>(ScopedRangeCount) * sizeof(Range);
        std::byte* ScopedNewRegion = ScopedOldRegion + ScopedByteCount;
        std::uint32_t ScopedRangeIndex = 0;
        std::uint32_t ScopedCursor = 0;
        ForEachChangedRange(ScopedOldBytes, ScopedNewBytes,
                            [&](std::uint32_t InStart, std::uint32_t InLength)
                            {
                                ScopedRanges[ScopedRangeIndex++] =
                                    Range{static_cast<std::uint16_t>(InStart), static_cast<std::uint16_t>(InLength)};
                                std::memcpy(ScopedOldRegion + ScopedCursor, ScopedOldBytes.data() + InStart, InLength);
                                std::memcpy(ScopedNewRegion + ScopedCursor, ScopedNewBytes.data() + InStart, InLength);
                                ScopedCursor += InLength;
                            });

        Command& ScopedCommand = ScopedBuilt[ScopedBuiltCount++];
        ScopedCommand.entity = ScopedPending.entity;
        ScopedCommand.codec = ScopedPending.codec;
        ScopedCommand.rangeCount = static_cast<std::uint16_t>(ScopedRangeCount);
        ScopedCommand.payloadOffset = static_cast<std::uint32_t>(MArenaUsed);
        ScopedCommand.payloadSize = static_cast<std::uint32_t>(ScopedPayloadSize);
        ScopedCommand.groupSize = 0;
        ScopedCommand.labelHash = HashLabel(MTransactionLabel);
        std::memcpy(ScopedCommand.label, MTransactionLabel, sizeof(ScopedCommand.label));
        MArenaUsed += ScopedPayloadSize;
    }

    if (ScopedBuiltCount == 0)
    {
        CancelTransaction();
        return Result::Ok;
    }
    // One committed transaction is one undo step: the first command records the
    ScopedBuilt[0].groupSize = static_cast<std::uint16_t>(ScopedBuiltCount);
    // Compact command payloads from firstPayload down to transactionStart_.
    std::size_t ScopedCompact = MTransactionStart;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedBuiltCount; ++ScopedI)
    {
        Command& ScopedCommand = ScopedBuilt[ScopedI];
        if (ScopedCommand.payloadOffset != ScopedCompact)
        {
            std::memmove(MPayload + ScopedCompact, MPayload + ScopedCommand.payloadOffset, ScopedCommand.payloadSize);
        }
        ScopedCommand.payloadOffset = static_cast<std::uint32_t>(ScopedCompact);
        ScopedCompact += ScopedCommand.payloadSize;
    }
    MArenaUsed = ScopedCompact;
    (void)ScopedFirstPayload;

    for (std::uint32_t ScopedI = 0; ScopedI < ScopedBuiltCount; ++ScopedI)
    {
        const Result ScopedResult = PushCommand(ScopedBuilt[ScopedI], {});
        if (ScopedResult != Result::Ok)
        {
            MStats.rejectedCommands += ScopedBuiltCount - ScopedI;
            MCommandCount = MCursor;
            MArenaUsed = ScopedCompact;
            MTransactionOpen = false;
            MPendingCount = 0;
            MPendingSpawnCount = 0;
            MPendingDestroyCount = 0;
            return ScopedResult;
        }
    }
    // PushCommand truncated the redo tail; payloads were already copied in place,
    MArenaUsed = ScopedCompact;
    MPendingCount = 0;
    MPendingSpawnCount = 0;
    MPendingDestroyCount = 0;
    MTransactionOpen = false;
    ++MStats.committedTransactions;
    MStats.commands = MCommandCount;
    MStats.undoDepth = MCursor;
    MStats.redoDepth = 0;
    MStats.bytesUsed = static_cast<std::uint32_t>(MArenaUsed);
    return Result::Ok;
}

bool CommandStack::ApplySpawn(const Command& InCommand, bool InUseNewBytes) noexcept
{
    if (MRegistry == nullptr)
    {
        return false;
    }
    if (InUseNewBytes)
    {
        return ApplySpawnSnapshot(InCommand);
    }
    if (!MRegistry->valid(InCommand.entity))
    {
        return false;
    }
    MRegistry->destroy(InCommand.entity);
    if (MObserver != nullptr)
    {
        MObserver(MObserverUser, InCommand.entity, 0xFFFFu);
    }
    return true;
}

bool CommandStack::ApplyDestroy(const Command& InCommand, bool InUseNewBytes) noexcept
{
    if (MRegistry == nullptr)
    {
        return false;
    }
    if (!InUseNewBytes)
    {
        // Undoing a delete resurrects the entity from its recorded image.
        return ApplySpawnSnapshot(InCommand);
    }
    if (!MRegistry->valid(InCommand.entity))
    {
        return false;
    }
    MRegistry->destroy(InCommand.entity);
    if (MObserver != nullptr)
    {
        MObserver(MObserverUser, InCommand.entity, 0xFFFFu);
    }
    return true;
}

bool CommandStack::ApplySpawnSnapshot(const Command& InCommand) noexcept
{
    if (MRegistry == nullptr || InCommand.payloadSize < sizeof(SpawnHeader))
    {
        return false;
    }
    const auto* ScopedHeader = reinterpret_cast<const SpawnHeader*>(MPayload + InCommand.payloadOffset);
    const auto* ScopedComponents =
        reinterpret_cast<const SpawnComponent*>(MPayload + InCommand.payloadOffset + sizeof(SpawnHeader));
    const std::size_t ScopedTableBytes =
        static_cast<std::size_t>(ScopedHeader->componentCount) * sizeof(SpawnComponent);
    if (sizeof(SpawnHeader) + ScopedTableBytes + ScopedHeader->byteCount > InCommand.payloadSize)
    {
        return false;
    }
    const std::byte* ScopedBytes = MPayload + InCommand.payloadOffset + sizeof(SpawnHeader) + ScopedTableBytes;

    if (MRegistry->valid(InCommand.entity))
    {
        // A redo target that is still alive is a stale history record
        return false;
    }
    // EnTT's hinted create() reuses the exact identifier, including its version
    const Entity ScopedCreated = MRegistry->create(InCommand.entity);
    if (!MRegistry->valid(ScopedCreated))
    {
        return false;
    }
    for (std::uint16_t ScopedI = 0; ScopedI < ScopedHeader->componentCount; ++ScopedI)
    {
        const SpawnComponent& ScopedEntry = ScopedComponents[ScopedI];
        const Codec* ScopedCodec = CodecAt(ScopedEntry.codec);
        if (ScopedCodec == nullptr || ScopedCodec->emplaceBytes == nullptr ||
            static_cast<std::size_t>(ScopedEntry.offset) + ScopedEntry.size > ScopedHeader->byteCount ||
            ScopedEntry.size != ScopedCodec->size)
        {
            MRegistry->destroy(ScopedCreated);
            return false;
        }
        ScopedCodec->emplaceBytes(*MRegistry, ScopedCreated, ScopedBytes + ScopedEntry.offset);
    }
    if (MObserver != nullptr)
    {
        MObserver(MObserverUser, ScopedCreated, 0xFFFFu);
    }
    return true;
}

bool CommandStack::ApplyCommand(const Command& InCommand, bool InUseNewBytes) noexcept
{
    if (InCommand.kind == CommandKind::Spawn)
    {
        return ApplySpawn(InCommand, InUseNewBytes);
    }
    if (InCommand.kind == CommandKind::Destroy)
    {
        return ApplyDestroy(InCommand, InUseNewBytes);
    }
    const Codec* ScopedCodec = CodecAt(InCommand.codec);
    if (ScopedCodec == nullptr)
    {
        return false;
    }
    void* ScopedComponent = nullptr;
    if (ScopedCodec->external != nullptr)
    {
        ScopedComponent = ScopedCodec->external;
    }
    else
    {
        if (MRegistry == nullptr || !MRegistry->valid(InCommand.entity))
        {
            return false;
        }
        ScopedComponent = ScopedCodec->getMutable(*MRegistry, InCommand.entity);
    }
    if (ScopedComponent == nullptr)
    {
        return false;
    }
    const auto* ScopedRanges = reinterpret_cast<const Range*>(MPayload + InCommand.payloadOffset);
    const std::size_t ScopedRangeTableBytes = static_cast<std::size_t>(InCommand.rangeCount) * sizeof(Range);
    const auto* ScopedOldRegion =
        reinterpret_cast<const std::byte*>(MPayload + InCommand.payloadOffset + ScopedRangeTableBytes);
    std::size_t ScopedOldBytesCount = 0;
    for (std::uint16_t ScopedI = 0; ScopedI < InCommand.rangeCount; ++ScopedI)
    {
        ScopedOldBytesCount += ScopedRanges[ScopedI].length;
    }
    const auto* ScopedNewRegion = ScopedOldRegion + ScopedOldBytesCount;
    const std::byte* ScopedSource = InUseNewBytes ? ScopedNewRegion : ScopedOldRegion;
    auto* ScopedDestination = static_cast<std::byte*>(ScopedComponent);
    std::size_t ScopedSourceCursor = 0;
    for (std::uint16_t ScopedI = 0; ScopedI < InCommand.rangeCount; ++ScopedI)
    {
        const Range& ScopedRange = ScopedRanges[ScopedI];
        if (static_cast<std::uint32_t>(ScopedRange.offset) + ScopedRange.length > ScopedCodec->size)
        {
            return false;
        }
        std::memcpy(ScopedDestination + ScopedRange.offset, ScopedSource + ScopedSourceCursor, ScopedRange.length);
        ScopedSourceCursor += ScopedRange.length;
    }
    if (MObserver != nullptr)
    {
        MObserver(MObserverUser, InCommand.entity, InCommand.codec);
    }
    return true;
}

bool CommandStack::Undo() noexcept
{
    if (MCursor == 0)
    {
        return false;
    }
    std::uint32_t ScopedGroup = MCommands[MCursor - 1].groupSize;
    if (ScopedGroup == 0)
    {
        // The group size is stored on the first command; the cursor sits on the
        std::uint32_t ScopedHead = MCursor - 1;
        while (ScopedHead > 0 && MCommands[ScopedHead].groupSize == 0)
        {
            --ScopedHead;
        }
        ScopedGroup = MCommands[ScopedHead].groupSize;
        if (ScopedGroup == 0 || ScopedGroup > MCursor)
        {
            ScopedGroup = 1;
        }
    }
    if (ScopedGroup > MCursor)
    {
        ScopedGroup = MCursor;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedGroup; ++ScopedI)
    {
        const Command& ScopedCommand = MCommands[MCursor - 1 - ScopedI];
        if (!ApplyCommand(ScopedCommand, false))
        {
            return false;
        }
    }
    MCursor -= ScopedGroup;
    MStats.undoDepth = MCursor;
    MStats.redoDepth = MCommandCount - MCursor;
    return true;
}

bool CommandStack::Redo() noexcept
{
    if (MCursor >= MCommandCount)
    {
        return false;
    }
    std::uint32_t ScopedGroup = MCommands[MCursor].groupSize;
    if (ScopedGroup == 0)
    {
        ScopedGroup = 1;
    }
    if (ScopedGroup > MCommandCount - MCursor)
    {
        ScopedGroup = MCommandCount - MCursor;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedGroup; ++ScopedI)
    {
        if (!ApplyCommand(MCommands[MCursor + ScopedI], true))
        {
            return false;
        }
    }
    MCursor += ScopedGroup;
    MStats.undoDepth = MCursor;
    MStats.redoDepth = MCommandCount - MCursor;
    return true;
}

void CommandStack::ClearHistory() noexcept
{
    MCommandCount = 0;
    MCursor = 0;
    MArenaUsed = 0;
    MPendingCount = 0;
    MPendingSpawnCount = 0;
    MTransactionOpen = false;
    MStats.undoDepth = 0;
    MStats.redoDepth = 0;
    MStats.commands = 0;
    MStats.bytesUsed = 0;
}

std::string_view CommandStack::UndoLabel() const noexcept
{
    if (MCursor == 0)
    {
        return {};
    }
    return MCommands[MCursor - 1].label;
}

std::string_view CommandStack::RedoLabel() const noexcept
{
    if (MCursor >= MCommandCount)
    {
        return {};
    }
    return MCommands[MCursor].label;
}
}
