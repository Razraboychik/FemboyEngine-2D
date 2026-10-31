// FemboyEngine2D - snapshot replication channel implementation.
#include "Engine/Network/Replication.hpp"

#include "Engine/Network/ByteOrder.hpp"

#include <cstring>

namespace Fby::Net
{
namespace
{
constexpr std::uint8_t kTypeState = 1;
constexpr std::uint8_t kTypeAck = 2;
constexpr std::uint8_t kAckStatusOk = 0;
constexpr std::uint8_t kAckStatusChecksum = 1;

[[nodiscard]] std::uint8_t* BytesOf(std::span<std::byte> InBytes) noexcept
{
    return reinterpret_cast<std::uint8_t*>(InBytes.data());
}

[[nodiscard]] const std::uint8_t* BytesOf(std::span<const std::byte> InBytes) noexcept
{
    return reinterpret_cast<const std::uint8_t*>(InBytes.data());
}

bool WriteHeader(std::span<std::byte> OutBytes, std::uint8_t InType, std::uint8_t InFlags) noexcept
{
    if (OutBytes.size() < 8)
    {
        return false;
    }
    std::uint8_t* ScopedOut = BytesOf(OutBytes);
    Detail::StoreU32(ScopedOut, kReplicationMagic);
    Detail::StoreU16(ScopedOut + 4, kReplicationVersion);
    ScopedOut[6] = InType;
    ScopedOut[7] = InFlags;
    return true;
}

[[nodiscard]] bool ReadHeader(std::span<const std::byte> InBytes, std::uint8_t* OutType,
                              std::uint8_t* OutFlags) noexcept
{
    if (InBytes.size() < 8)
    {
        return false;
    }
    const std::uint8_t* ScopedIn = BytesOf(InBytes);
    if (Detail::LoadU32(ScopedIn) != kReplicationMagic || Detail::LoadU16(ScopedIn + 4) != kReplicationVersion)
    {
        return false;
    }
    if (OutType != nullptr)
    {
        *OutType = ScopedIn[6];
    }
    if (OutFlags != nullptr)
    {
        *OutFlags = ScopedIn[7];
    }
    return true;
}
}

std::uint64_t ReplicationChecksum(std::span<const std::byte> InBytes) noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (const std::byte ScopedByte : InBytes)
    {
        ScopedHash ^= static_cast<std::uint8_t>(ScopedByte);
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

bool SnapshotSender::Start(const NetLink& InLink, const NetAddress& InRemote,
                           const SnapshotSenderConfig& InConfig) noexcept
{
    Stop();
    if (!InLink.Valid() || InConfig.ChunkBytes == 0 || InConfig.ChunkBytes > 472 ||
        InConfig.MaxStateBytes == 0 || InConfig.MaxStateBytes > kReplicationMaxStateBytes ||
        InConfig.RetransmitMs == 0 || InConfig.MaxAttempts == 0)
    {
        return false;
    }
    MLink = InLink;
    MRemote = InRemote;
    MConfig = InConfig;
    MBudgetTokens = 0;
    return true;
}

void SnapshotSender::Stop() noexcept
{
    MLink = NetLink{};
    MRemote = NetAddress{};
    MConfig = SnapshotSenderConfig{};
    MStats = SnapshotSenderStats{};
    MHead = 0;
    MQueueCount = 0;
    MLastAckedTick = 0;
    MBudgetTokens = 0;
    MLastRefillMs = 0;
    MNeedsBaseline = false;
}

bool SnapshotSender::PublishBaseline(std::uint64_t InTick, std::span<const std::byte> InState,
                                     std::uint64_t InNowMs) noexcept
{
    return Enqueue(InTick, InTick, InState, true, InNowMs);
}

bool SnapshotSender::PublishDelta(std::uint64_t InTick, std::uint64_t InBaselineTick,
                                  std::span<const std::byte> InState, std::uint64_t InNowMs) noexcept
{
    return Enqueue(InTick, InBaselineTick, InState, false, InNowMs);
}

bool SnapshotSender::Enqueue(std::uint64_t InTick, std::uint64_t InBaselineTick, std::span<const std::byte> InState,
                             bool InBaseline, std::uint64_t InNowMs) noexcept
{
    if (MConfig.ChunkBytes == 0 || InState.empty() || InState.size() > MConfig.MaxStateBytes ||
        InState.size() > kReplicationMaxStateBytes)
    {
        return false;
    }
    const std::uint32_t ScopedChunks =
        static_cast<std::uint32_t>((InState.size() + MConfig.ChunkBytes - 1) / MConfig.ChunkBytes);
    if (ScopedChunks == 0 || ScopedChunks > kReplicationMaxChunks)
    {
        return false;
    }

    if (MQueueCount >= kReplicationQueueDepth)
    {
        DropFront();
        MNeedsBaseline = true;
    }

    PendingState& ScopedState = MQueue[(MHead + MQueueCount) % kReplicationQueueDepth];
    ScopedState = PendingState{};
    ScopedState.Tick = InTick;
    ScopedState.BaselineTick = InBaselineTick;
    ScopedState.Bytes = static_cast<std::uint32_t>(InState.size());
    ScopedState.ChunkCount = ScopedChunks;
    ScopedState.Checksum = ReplicationChecksum(InState);
    ScopedState.Baseline = InBaseline;
    std::memcpy(ScopedState.Data.data(), InState.data(), InState.size());
    ++MQueueCount;
    ++MStats.StatesPublished;

    RefillBudget(InNowMs);
    SendState(ScopedState, InNowMs, InBaseline);
    return true;
}

void SnapshotSender::DropFront() noexcept
{
    if (MQueueCount == 0)
    {
        return;
    }
    MQueue[MHead] = PendingState{};
    MHead = (MHead + 1) % kReplicationQueueDepth;
    --MQueueCount;
    ++MStats.Dropped;
}

void SnapshotSender::RefillBudget(std::uint64_t InNowMs) noexcept
{
    if (MConfig.BytesPerSecond == 0)
    {
        return;
    }
    if (MLastRefillMs == 0)
    {
        MLastRefillMs = InNowMs;
        return;
    }
    const std::uint64_t ScopedElapsed = InNowMs > MLastRefillMs ? InNowMs - MLastRefillMs : 0;
    MLastRefillMs = InNowMs;
    MBudgetTokens += static_cast<std::int64_t>(ScopedElapsed) * MConfig.BytesPerSecond / 1000;
    const std::int64_t ScopedBurst = static_cast<std::int64_t>(MConfig.BytesPerSecond);
    if (MBudgetTokens > ScopedBurst)
    {
        MBudgetTokens = ScopedBurst;
    }
}

void SnapshotSender::SendState(PendingState& InState, std::uint64_t InNowMs, bool InForce) noexcept
{
    const std::uint64_t ScopedTotal =
        static_cast<std::uint64_t>(InState.Bytes) + kReplicationStateHeaderBytes * InState.ChunkCount;
    if (!InForce && MConfig.BytesPerSecond != 0)
    {
        if (MBudgetTokens < static_cast<std::int64_t>(ScopedTotal))
        {
            ++MStats.Deferred;
            return;
        }
        MBudgetTokens -= static_cast<std::int64_t>(ScopedTotal);
    }

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InState.ChunkCount; ++ScopedIndex)
    {
        const std::uint32_t ScopedOffset = ScopedIndex * MConfig.ChunkBytes;
        const std::uint32_t ScopedPayload = InState.Bytes - ScopedOffset < MConfig.ChunkBytes
                                                ? InState.Bytes - ScopedOffset
                                                : MConfig.ChunkBytes;
        std::array<std::byte, 512> ScopedMessage{};
        const std::size_t ScopedBytes = kReplicationStateHeaderBytes + ScopedPayload;
        if (!WriteHeader(ScopedMessage, kTypeState, InState.Baseline ? kReplicationFlagBaseline : 0))
        {
            return;
        }
        std::uint8_t* ScopedOut = BytesOf(std::span<std::byte>(ScopedMessage)) + 8;
        Detail::StoreU64(ScopedOut, InState.Tick);
        Detail::StoreU64(ScopedOut + 8, InState.BaselineTick);
        Detail::StoreU32(ScopedOut + 16, InState.Bytes);
        Detail::StoreU16(ScopedOut + 20, static_cast<std::uint16_t>(ScopedIndex));
        Detail::StoreU16(ScopedOut + 22, static_cast<std::uint16_t>(InState.ChunkCount));
        Detail::StoreU64(ScopedOut + 24, InState.Checksum);
        std::memcpy(ScopedOut + 32, InState.Data.data() + ScopedOffset, ScopedPayload);

        if (!MLink.Send(MRemote, std::span<const std::byte>(ScopedMessage.data(), ScopedBytes)))
        {
            return;
        }
        ++MStats.ChunksSent;
        MStats.BytesSent += ScopedBytes;
    }

    if (InState.Attempts > 0)
    {
        ++MStats.Retransmits;
    }
    ++InState.Attempts;
    InState.LastSentMs = InNowMs;
}

void SnapshotSender::OnMessageThunk(void* InUser, const NetAddress& InFrom,
                                    std::span<const std::byte> InMessage) noexcept
{
    auto* ScopedSender = static_cast<SnapshotSender*>(InUser);
    if (ScopedSender != nullptr)
    {
        ScopedSender->HandleAck(InFrom, InMessage);
    }
}

void SnapshotSender::HandleAck(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept
{
    if (!(InFrom == MRemote) || InMessage.size() != kReplicationAckBytes)
    {
        return;
    }
    std::uint8_t ScopedType = 0;
    if (!ReadHeader(InMessage, &ScopedType, nullptr) || ScopedType != kTypeAck)
    {
        return;
    }
    const std::uint8_t* ScopedIn = BytesOf(InMessage) + 8;
    const std::uint64_t ScopedTick = Detail::LoadU64(ScopedIn);
    const std::uint8_t ScopedStatus = ScopedIn[16];
    ++MStats.Acks;

    if (ScopedStatus != kAckStatusOk)
    {
        // The receiver rejected the payload; a baseline is the only safe reset.
        MNeedsBaseline = true;
        return;
    }

    while (MQueueCount > 0)
    {
        const PendingState& ScopedFront = MQueue[MHead];
        if (ScopedFront.Tick > ScopedTick)
        {
            break;
        }
        MLastAckedTick = ScopedFront.Tick;
        MQueue[MHead] = PendingState{};
        MHead = (MHead + 1) % kReplicationQueueDepth;
        --MQueueCount;
    }
}

void SnapshotSender::Tick(std::uint64_t InNowMs) noexcept
{
    if (!MLink.Valid())
    {
        return;
    }
    RefillBudget(InNowMs);
    (void)MLink.Poll(&SnapshotSender::OnMessageThunk, this);

    if (MQueueCount == 0)
    {
        return;
    }
    PendingState& ScopedFront = MQueue[MHead];
    if (ScopedFront.Attempts >= MConfig.MaxAttempts)
    {
        DropFront();
        MNeedsBaseline = true;
        return;
    }
    if (InNowMs < ScopedFront.LastSentMs + MConfig.RetransmitMs)
    {
        return;
    }
    SendState(ScopedFront, InNowMs, false);
}

bool SnapshotReceiver::Start(const NetLink& InLink, const NetAddress& InRemote,
                             const SnapshotReceiverConfig& InConfig, ApplyStateFn InApply,
                             void* InUser) noexcept
{
    Stop();
    if (!InLink.Valid() || InApply == nullptr || InConfig.ChunkBytes == 0 || InConfig.ChunkBytes > 472 ||
        InConfig.MaxStateBytes == 0 || InConfig.MaxStateBytes > kReplicationMaxStateBytes)
    {
        return false;
    }
    MLink = InLink;
    MRemote = InRemote;
    MConfig = InConfig;
    MApply = InApply;
    MApplyUser = InUser;
    MLastStatus = ReplicationStatus::Ok;
    return true;
}

void SnapshotReceiver::Stop() noexcept
{
    MLink = NetLink{};
    MRemote = NetAddress{};
    MConfig = SnapshotReceiverConfig{};
    MApply = nullptr;
    MApplyUser = nullptr;
    MAppliedTick = 0;
    MExpectedTick = 1;
    MLastApplyMs = 0;
    MAppliedStates = 0;
    MChecksumFailures = 0;
    MHasApplied = false;
    MLastStatus = ReplicationStatus::NotStarted;
    MNowMs = 0;
    ResetAssembly();
}

void SnapshotReceiver::ResetAssembly() noexcept
{
    MAssembling = false;
    MAssembleTick = 0;
    MAssembleChecksum = 0;
    MAssembleBytes = 0;
    MAssembleChunkCount = 0;
    MAssembleReceived = 0;
    MAssembleMask = 0;
}

void SnapshotReceiver::OnMessageThunk(void* InUser, const NetAddress& InFrom,
                                      std::span<const std::byte> InMessage) noexcept
{
    auto* ScopedReceiver = static_cast<SnapshotReceiver*>(InUser);
    if (ScopedReceiver != nullptr)
    {
        ScopedReceiver->HandleState(InFrom, InMessage);
    }
}

void SnapshotReceiver::Tick(std::uint64_t InNowMs) noexcept
{
    MNowMs = InNowMs;
    if (MLink.Valid())
    {
        (void)MLink.Poll(&SnapshotReceiver::OnMessageThunk, this);
    }
}

void SnapshotReceiver::SendAck(const NetAddress& InTo, std::uint8_t InStatus) noexcept
{
    std::array<std::byte, 64> ScopedMessage{};
    if (!WriteHeader(ScopedMessage, kTypeAck, 0))
    {
        return;
    }
    std::uint8_t* ScopedOut = BytesOf(std::span<std::byte>(ScopedMessage)) + 8;
    Detail::StoreU64(ScopedOut, MAppliedTick);
    Detail::StoreU64(ScopedOut + 8, 0);
    ScopedOut[16] = InStatus;
    ScopedOut[17] = 0;
    (void)MLink.Send(InTo, std::span<const std::byte>(ScopedMessage.data(), kReplicationAckBytes));
}

void SnapshotReceiver::HandleState(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept
{
    if (!(InFrom == MRemote))
    {
        return;
    }
    std::uint8_t ScopedType = 0;
    std::uint8_t ScopedFlags = 0;
    if (!ReadHeader(InMessage, &ScopedType, &ScopedFlags) || ScopedType != kTypeState ||
        InMessage.size() < kReplicationStateHeaderBytes)
    {
        return;
    }

    const std::uint8_t* ScopedIn = BytesOf(InMessage) + 8;
    const std::uint64_t ScopedTick = Detail::LoadU64(ScopedIn);
    // The baseline tick stays in the wire header; the receiver only needs the
    // tick and the chunk metadata it stores below.
    (void)Detail::LoadU64(ScopedIn + 8);
    const std::uint32_t ScopedTotal = Detail::LoadU32(ScopedIn + 16);
    const std::uint16_t ScopedIndex = Detail::LoadU16(ScopedIn + 20);
    const std::uint16_t ScopedChunkCount = Detail::LoadU16(ScopedIn + 22);
    const std::uint64_t ScopedChecksum = Detail::LoadU64(ScopedIn + 24);
    const std::span<const std::byte> ScopedPayload = InMessage.subspan(kReplicationStateHeaderBytes);

    if (ScopedTotal == 0 || ScopedTotal > MConfig.MaxStateBytes || ScopedTotal > kReplicationMaxStateBytes ||
        ScopedChunkCount == 0 || ScopedChunkCount > kReplicationMaxChunks || ScopedIndex >= ScopedChunkCount)
    {
        return;
    }
    const std::uint32_t ScopedOffset = static_cast<std::uint32_t>(ScopedIndex) * MConfig.ChunkBytes;
    if (ScopedOffset >= ScopedTotal || ScopedOffset + ScopedPayload.size() > ScopedTotal)
    {
        return;
    }
    const bool ScopedIsBaseline = (ScopedFlags & kReplicationFlagBaseline) != 0;

    if (ScopedTick < MExpectedTick)
    {
        SendAck(InFrom, kAckStatusOk);
        return;
    }
    if (ScopedIsBaseline && (!MHasApplied || ScopedTick >= MExpectedTick))
    {
        if (!MAssembling || MAssembleTick != ScopedTick)
        {
            ResetAssembly();
        }
    }
    else if (ScopedTick > MExpectedTick)
    {
        // The missing tick comes first; the sender retransmits it.
        SendAck(InFrom, kAckStatusOk);
        return;
    }
    if (!MHasApplied && !ScopedIsBaseline)
    {
        return;
    }

    if (!MAssembling || MAssembleTick != ScopedTick)
    {
        ResetAssembly();
        MAssembling = true;
        MAssembleTick = ScopedTick;
        MAssembleChecksum = ScopedChecksum;
        MAssembleBytes = ScopedTotal;
        MAssembleChunkCount = ScopedChunkCount;
    }
    if (MAssembleChunkCount != ScopedChunkCount || MAssembleBytes != ScopedTotal ||
        MAssembleChecksum != ScopedChecksum)
    {
        ResetAssembly();
        return;
    }

    const std::uint32_t ScopedBit = 1u << ScopedIndex;
    if ((MAssembleMask & ScopedBit) == 0)
    {
        MAssembleMask |= ScopedBit;
        ++MAssembleReceived;
        std::memcpy(MAssembleData.data() + ScopedOffset, ScopedPayload.data(), ScopedPayload.size());
    }
    if (MAssembleReceived == MAssembleChunkCount)
    {
        const std::span<const std::byte> ScopedState(MAssembleData.data(), MAssembleBytes);
        if (ReplicationChecksum(ScopedState) != MAssembleChecksum)
        {
            ++MChecksumFailures;
            MLastStatus = ReplicationStatus::ChecksumMismatch;
            SendAck(InFrom, kAckStatusChecksum);
            ResetAssembly();
            return;
        }
        MAppliedTick = MAssembleTick;
        MLastApplyMs = MNowMs;
        MHasApplied = true;
        ++MAppliedStates;
        MExpectedTick = MAssembleTick + 1;
        MLastStatus = ReplicationStatus::Ok;
        if (MApply != nullptr)
        {
            MApply(MApplyUser, MAssembleTick, ScopedState);
        }
        SendAck(InFrom, kAckStatusOk);
        ResetAssembly();
    }
}

float SnapshotReceiver::InterpolationAlpha(std::uint64_t InNowMs) const noexcept
{
    if (!MHasApplied || MConfig.FixedDtMs == 0)
    {
        return MHasApplied ? 1.0f : 0.0f;
    }
    if (InNowMs <= MLastApplyMs)
    {
        return 0.0f;
    }
    const float ScopedElapsed = static_cast<float>(InNowMs - MLastApplyMs);
    const float ScopedAlpha = ScopedElapsed / static_cast<float>(MConfig.FixedDtMs);
    return ScopedAlpha > 1.0f ? 1.0f : ScopedAlpha;
}
}
