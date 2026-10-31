#include "Engine/ECS/CommandLog.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string_view>

namespace Fby::Ecs
{
namespace
{
[[nodiscard]] std::uint64_t HashBytes(std::span<const std::byte> InBytes,
                                      std::uint64_t InHash = 1469598103934665603ull) noexcept
{
    for (const std::byte ScopedByte : InBytes)
    {
        InHash ^= static_cast<std::uint64_t>(std::to_integer<unsigned char>(ScopedByte));
        InHash *= 1099511628211ull;
    }
    return InHash;
}
}

void CommandLog::Clear(std::uint64_t InTick) noexcept
{
    MWriteOffset = sizeof(CommandLogHeader);
    MCommandCount = 0;
    MFirstTick = InTick;
    MLastTick = InTick;
    MSealed = false;
}

bool CommandLog::CanAppend(std::uint32_t InTotalBytes) const noexcept
{
    return MCommandCount < kMaxCommandLogCommands && MWriteOffset + InTotalBytes <= MStorage.size();
}

bool CommandLog::BeginRecord(CommandType InType, Entity InEntity, std::uint32_t InPayloadBytes,
                             std::uint64_t InTick) noexcept
{
    if (!CanAppend(sizeof(CommandRecord) + InPayloadBytes) || InTick < MLastTick || InEntity == entt::null)
    {
        return false;
    }
    CommandRecord ScopedRecord;
    ScopedRecord.type = InType;
    ScopedRecord.payloadBytes = InPayloadBytes;
    ScopedRecord.sequence = MCommandCount;
    ScopedRecord.entity = InEntity;
    ScopedRecord.tick = InTick;
    std::memcpy(MStorage.data() + MWriteOffset, &ScopedRecord, sizeof(ScopedRecord));
    MWriteOffset += sizeof(ScopedRecord);
    if (MCommandCount == 0)
    {
        MFirstTick = InTick;
    }
    MLastTick = InTick;
    ++MCommandCount;
    MSealed = false;
    return true;
}

bool CommandLog::Spawn(Entity InEntity, std::uint64_t InTick) noexcept
{
    return BeginRecord(CommandType::Spawn, InEntity, 0, InTick);
}

bool CommandLog::Despawn(Entity InEntity, std::uint64_t InTick) noexcept
{
    return BeginRecord(CommandType::Despawn, InEntity, 0, InTick);
}

bool CommandLog::AddComponentBytes(Entity InEntity, std::uint64_t InTypeId, std::span<const std::byte> InData,
                                   std::uint64_t InTick) noexcept
{
    const std::uint32_t ScopedPayloadBytes =
        static_cast<std::uint32_t>(sizeof(InTypeId) + sizeof(std::uint32_t) + InData.size());
    if (!CanAppend(sizeof(CommandRecord) + ScopedPayloadBytes))
    {
        return false;
    }
    if (!BeginRecord(CommandType::AddComponent, InEntity, ScopedPayloadBytes, InTick))
    {
        return false;
    }
    const std::uint32_t ScopedSize = static_cast<std::uint32_t>(InData.size());
    std::memcpy(MStorage.data() + MWriteOffset, &InTypeId, sizeof(InTypeId));
    MWriteOffset += sizeof(InTypeId);
    std::memcpy(MStorage.data() + MWriteOffset, &ScopedSize, sizeof(ScopedSize));
    MWriteOffset += sizeof(ScopedSize);
    if (!InData.empty())
    {
        std::memcpy(MStorage.data() + MWriteOffset, InData.data(), InData.size());
        MWriteOffset += ScopedSize;
    }
    return true;
}

bool CommandLog::RemoveComponent(Entity InEntity, std::uint64_t InTypeId, std::uint64_t InTick) noexcept
{
    constexpr std::uint32_t ScopedPayloadBytes = sizeof(InTypeId);
    if (!BeginRecord(CommandType::RemoveComponent, InEntity, ScopedPayloadBytes, InTick))
    {
        return false;
    }
    std::memcpy(MStorage.data() + MWriteOffset, &InTypeId, sizeof(InTypeId));
    MWriteOffset += sizeof(InTypeId);
    return true;
}

void CommandLog::Seal() const noexcept
{
    if (MSealed)
    {
        return;
    }
    CommandLogHeader ScopedHeader;
    std::memcpy(ScopedHeader.magic, kCommandLogMagic, sizeof(kCommandLogMagic));
    ScopedHeader.version = kCommandLogVersion;
    ScopedHeader.commandCount = MCommandCount;
    ScopedHeader.payloadBytes = MWriteOffset - sizeof(CommandLogHeader);
    ScopedHeader.firstTick = MFirstTick;
    ScopedHeader.lastTick = MLastTick;
    ScopedHeader.checksum =
        HashBytes(std::span<const std::byte>(MStorage.data() + sizeof(CommandLogHeader), ScopedHeader.payloadBytes));
    std::memcpy(MStorage.data(), &ScopedHeader, sizeof(ScopedHeader));
    MSealed = true;
}

std::span<const std::byte> CommandLog::Bytes() const noexcept
{
    Seal();
    return {MStorage.data(), MWriteOffset};
}

CommandLog::View CommandLog::View::Open(std::span<const std::byte> InBytes, std::string_view* InError)
{
    const auto ScopedFail = [InError](std::string_view InMessage)
    {
        if (InError != nullptr)
        {
            *InError = InMessage;
        }
        return View{};
    };

    if (InBytes.size() < sizeof(CommandLogHeader))
    {
        return ScopedFail("command log is smaller than its header");
    }
    CommandLogHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kCommandLogMagic, sizeof(kCommandLogMagic)) != 0)
    {
        return ScopedFail("bad command log magic");
    }
    if (ScopedHeader.version != kCommandLogVersion)
    {
        return ScopedFail("unsupported command log version");
    }
    if (ScopedHeader.commandCount > kMaxCommandLogCommands)
    {
        return ScopedFail("command log command count exceeds the limit");
    }
    const std::size_t ScopedExpected = sizeof(CommandLogHeader) + static_cast<std::size_t>(ScopedHeader.payloadBytes);
    if (InBytes.size() != ScopedExpected)
    {
        return ScopedFail("command log size does not match its header");
    }
    const std::span<const std::byte> ScopedPayload =
        InBytes.subspan(sizeof(CommandLogHeader), ScopedHeader.payloadBytes);
    if (HashBytes(ScopedPayload) != ScopedHeader.checksum)
    {
        return ScopedFail("command log checksum mismatch");
    }
    View ScopedView;
    ScopedView.header = ScopedHeader;
    ScopedView.payload = ScopedPayload;
    return ScopedView;
}

bool CommandLog::PushWorldCommand(CommandType InType, Entity InEntity, std::span<const std::byte> InPayload,
                                  std::uint64_t InTick) noexcept
{
    if (InPayload.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    const auto ScopedBytes = static_cast<std::uint32_t>(InPayload.size());
    if (!BeginRecord(InType, InEntity, ScopedBytes, InTick))
    {
        return false;
    }
    if (!InPayload.empty())
    {
        std::memcpy(MStorage.data() + MWriteOffset, InPayload.data(), InPayload.size());
        MWriteOffset += ScopedBytes;
    }
    return true;
}

bool CommandLog::Replay(Registry& InRegistry, const SnapshotSchema& InSchema,
                        const CommandWorldCallbacks* InWorld) const
{
    Cursor ScopedCursor;
    if (!ApplyUntil(InRegistry, InSchema, std::numeric_limits<std::uint64_t>::max(), ScopedCursor, InWorld))
    {
        return false;
    }
    std::string_view ScopedError;
    const View ScopedView = View::Open(Bytes(), &ScopedError);
    return ScopedView.header.version != 0 && ScopedCursor.commandIndex == ScopedView.header.commandCount &&
           ScopedCursor.payloadOffset == ScopedView.payload.size();
}

bool CommandLog::ApplyUntil(Registry& InRegistry, const SnapshotSchema& InSchema, std::uint64_t InTick,
                            Cursor& InCursor, const CommandWorldCallbacks* InWorld) const
{
    std::string_view ScopedError;
    const View ScopedView = View::Open(Bytes(), &ScopedError);
    if (ScopedView.header.version == 0)
    {
        return false;
    }
    if (InCursor.commandIndex > ScopedView.header.commandCount || InCursor.payloadOffset > ScopedView.payload.size())
    {
        return false;
    }

    std::size_t ScopedOffset = InCursor.payloadOffset;
    std::uint32_t ScopedIndex = InCursor.commandIndex;
    std::uint64_t ScopedPreviousTick = InCursor.lastTick;
    while (ScopedIndex < ScopedView.header.commandCount)
    {
        if (ScopedOffset + sizeof(CommandRecord) > ScopedView.payload.size())
        {
            return false;
        }
        CommandRecord ScopedRecord{};
        std::memcpy(&ScopedRecord, ScopedView.payload.data() + ScopedOffset, sizeof(ScopedRecord));
        if (ScopedRecord.tick < ScopedPreviousTick || ScopedRecord.sequence != ScopedIndex)
        {
            return false;
        }
        if (ScopedRecord.tick > InTick)
        {
            break;
        }
        const std::size_t ScopedRecordOffset = ScopedOffset;
        ScopedOffset += sizeof(ScopedRecord);
        if (ScopedOffset + ScopedRecord.payloadBytes > ScopedView.payload.size() || ScopedRecord.entity == entt::null)
        {
            return false;
        }
        ScopedPreviousTick = ScopedRecord.tick;
        const std::span<const std::byte> ScopedPayload(ScopedView.payload.data() + ScopedOffset,
                                                       ScopedRecord.payloadBytes);
        ScopedOffset += ScopedRecord.payloadBytes;

        switch (ScopedRecord.type)
        {
            case CommandType::Spawn:
                if (ScopedRecord.payloadBytes != 0)
                {
                    return false;
                }
                if (!InRegistry.valid(ScopedRecord.entity))
                {
                    (void)InRegistry.create(ScopedRecord.entity);
                }
                break;
            case CommandType::Despawn:
                if (ScopedRecord.payloadBytes != 0)
                {
                    return false;
                }
                if (InRegistry.valid(ScopedRecord.entity))
                {
                    InRegistry.destroy(ScopedRecord.entity);
                }
                break;
            case CommandType::AddComponent:
            {
                if (ScopedPayload.size() < sizeof(std::uint64_t) + sizeof(std::uint32_t))
                {
                    return false;
                }
                std::uint64_t ScopedTypeId = 0;
                std::uint32_t ScopedSize = 0;
                std::memcpy(&ScopedTypeId, ScopedPayload.data(), sizeof(ScopedTypeId));
                std::memcpy(&ScopedSize, ScopedPayload.data() + sizeof(ScopedTypeId), sizeof(ScopedSize));
                if (ScopedPayload.size() != sizeof(ScopedTypeId) + sizeof(ScopedSize) + ScopedSize)
                {
                    return false;
                }
                const SnapshotComponent* ScopedCodec = InSchema.Find(ScopedTypeId);
                if (ScopedCodec == nullptr || ScopedCodec->size != ScopedSize)
                {
                    return false;
                }
                if (!InRegistry.valid(ScopedRecord.entity))
                {
                    (void)InRegistry.create(ScopedRecord.entity);
                }
                if (!ScopedCodec->restore(InRegistry, ScopedRecord.entity,
                                          ScopedPayload.subspan(sizeof(ScopedTypeId) + sizeof(ScopedSize), ScopedSize)))
                {
                    return false;
                }
                break;
            }
            case CommandType::RemoveComponent:
            {
                if (ScopedPayload.size() != sizeof(std::uint64_t))
                {
                    return false;
                }
                std::uint64_t ScopedTypeId = 0;
                std::memcpy(&ScopedTypeId, ScopedPayload.data(), sizeof(ScopedTypeId));
                const SnapshotComponent* ScopedCodec = InSchema.Find(ScopedTypeId);
                if (ScopedCodec == nullptr)
                {
                    return false;
                }
                ScopedCodec->remove(InRegistry, ScopedRecord.entity);
                break;
            }
            case CommandType::CreateChain:
                if (InWorld == nullptr || InWorld->createChain == nullptr ||
                    !InWorld->createChain(InWorld->user, ScopedRecord.entity, ScopedPayload))
                {
                    return false;
                }
                break;
            case CommandType::DestroyChain:
                if (InWorld == nullptr || InWorld->destroyChain == nullptr ||
                    !InWorld->destroyChain(InWorld->user, ScopedRecord.entity, ScopedPayload))
                {
                    return false;
                }
                break;
            case CommandType::CreateCloth:
                if (InWorld == nullptr || InWorld->createCloth == nullptr ||
                    !InWorld->createCloth(InWorld->user, ScopedRecord.entity, ScopedPayload))
                {
                    return false;
                }
                break;
            case CommandType::DestroyCloth:
                if (InWorld == nullptr || InWorld->destroyCloth == nullptr ||
                    !InWorld->destroyCloth(InWorld->user, ScopedRecord.entity, ScopedPayload))
                {
                    return false;
                }
                break;
            case CommandType::EmitParticles:
                if (InWorld == nullptr || InWorld->emitParticles == nullptr ||
                    !InWorld->emitParticles(InWorld->user, ScopedRecord.entity, ScopedPayload))
                {
                    return false;
                }
                break;
            default:
                return false;
        }
        ++ScopedIndex;
        InCursor.payloadOffset = ScopedOffset;
        InCursor.commandIndex = ScopedIndex;
        InCursor.lastTick = ScopedPreviousTick;
        (void)ScopedRecordOffset;
    }
    return true;
}
}
