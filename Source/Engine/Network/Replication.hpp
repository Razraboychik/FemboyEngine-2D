// FemboyEngine2D - snapshot replication: baselines, delta chunks and client interpolation.
#pragma once

#include "Engine/Network/NetLink.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby::Net
{

inline constexpr std::uint32_t kReplicationMagic = 0x50455246u;
inline constexpr std::uint16_t kReplicationVersion = 1;
inline constexpr std::uint32_t kReplicationMaxStateBytes = 16384;
inline constexpr std::uint32_t kReplicationChunkBytes = 448;
inline constexpr std::uint32_t kReplicationMaxChunks = 48;
inline constexpr std::uint32_t kReplicationQueueDepth = 16;
inline constexpr std::size_t kReplicationStateHeaderBytes = 40;
inline constexpr std::size_t kReplicationAckBytes = 26;
inline constexpr std::uint8_t kReplicationFlagBaseline = 1u << 0;

// FNV-1a 64: cheap, deterministic integrity check for a state payload. The
// authority computes it once per published state.
[[nodiscard]] std::uint64_t ReplicationChecksum(std::span<const std::byte> InBytes) noexcept;

enum class ReplicationStatus : std::uint8_t
{
    Ok = 0,
    NotStarted,
    BadSize,
    BadMagic,
    BadVersion,
    ChecksumMismatch,
    NotExpected,
    BudgetDeferred,
    SendFailed,
    QueueOverflow,
};

[[nodiscard]] constexpr const char* ReplicationStatusName(ReplicationStatus InStatus) noexcept
{
    switch (InStatus)
    {
        case ReplicationStatus::Ok:
            return "Ok";
        case ReplicationStatus::NotStarted:
            return "NotStarted";
        case ReplicationStatus::BadSize:
            return "BadSize";
        case ReplicationStatus::BadMagic:
            return "BadMagic";
        case ReplicationStatus::BadVersion:
            return "BadVersion";
        case ReplicationStatus::ChecksumMismatch:
            return "ChecksumMismatch";
        case ReplicationStatus::NotExpected:
            return "NotExpected";
        case ReplicationStatus::BudgetDeferred:
            return "BudgetDeferred";
        case ReplicationStatus::SendFailed:
            return "SendFailed";
        case ReplicationStatus::QueueOverflow:
            return "QueueOverflow";
    }
    return "?";
}

struct SnapshotSenderConfig
{
    std::uint32_t MaxStateBytes = kReplicationMaxStateBytes;
    std::uint32_t ChunkBytes = kReplicationChunkBytes;
    std::uint32_t RetransmitMs = 50;
    // 0 = unlimited. Baselines bypass the budget; deltas are throttled.
    std::uint32_t BytesPerSecond = 0;
    std::uint32_t MaxAttempts = 16;
};

struct SnapshotSenderStats
{
    std::uint64_t StatesPublished = 0;
    std::uint64_t ChunksSent = 0;
    std::uint64_t Retransmits = 0;
    std::uint64_t Acks = 0;
    std::uint64_t Dropped = 0;
    std::uint64_t Deferred = 0;
    std::uint64_t BytesSent = 0;
};

// Authority side: one sender per client. States are queued, chunked, resent on
// timer and acknowledged by the receiver; an overflow asks the game for a fresh
// baseline instead of silently losing a delta chain.
class SnapshotSender
{
public:
    SnapshotSender() = default;

    [[nodiscard]] bool Start(const NetLink& InLink, const NetAddress& InRemote,
                             const SnapshotSenderConfig& InConfig) noexcept;
    void Stop() noexcept;

    [[nodiscard]] bool PublishBaseline(std::uint64_t InTick, std::span<const std::byte> InState,
                                       std::uint64_t InNowMs) noexcept;
    [[nodiscard]] bool PublishDelta(std::uint64_t InTick, std::uint64_t InBaselineTick,
                                    std::span<const std::byte> InState, std::uint64_t InNowMs) noexcept;

    // Polls acknowledgements, refills the budget and retransmits due states.
    void Tick(std::uint64_t InNowMs) noexcept;

    [[nodiscard]] bool NeedsBaseline() const noexcept
    {
        return MNeedsBaseline;
    }
    [[nodiscard]] std::uint64_t LastAckedTick() const noexcept
    {
        return MLastAckedTick;
    }
    [[nodiscard]] std::uint32_t PendingStates() const noexcept
    {
        return MQueueCount;
    }
    [[nodiscard]] const SnapshotSenderStats& Stats() const noexcept
    {
        return MStats;
    }

protected:
    struct PendingState
    {
        std::uint64_t Tick = 0;
        std::uint64_t BaselineTick = 0;
        std::uint32_t Bytes = 0;
        std::uint32_t ChunkCount = 0;
        std::uint32_t Attempts = 0;
        std::uint64_t Checksum = 0;
        std::uint64_t LastSentMs = 0;
        bool Baseline = false;
        std::array<std::byte, kReplicationMaxStateBytes> Data{};
    };

    static void OnMessageThunk(void* InUser, const NetAddress& InFrom,
                               std::span<const std::byte> InMessage) noexcept;
    void HandleAck(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;
    [[nodiscard]] bool Enqueue(std::uint64_t InTick, std::uint64_t InBaselineTick, std::span<const std::byte> InState,
                               bool InBaseline, std::uint64_t InNowMs) noexcept;
    void SendState(PendingState& InState, std::uint64_t InNowMs, bool InForce) noexcept;
    void DropFront() noexcept;
    void RefillBudget(std::uint64_t InNowMs) noexcept;

    NetLink MLink{};
    NetAddress MRemote{};
    SnapshotSenderConfig MConfig{};
    SnapshotSenderStats MStats{};
    std::array<PendingState, kReplicationQueueDepth> MQueue{};
    std::uint32_t MHead = 0;
    std::uint32_t MQueueCount = 0;
    std::uint64_t MLastAckedTick = 0;
    std::int64_t MBudgetTokens = 0;
    std::uint64_t MLastRefillMs = 0;
    bool MNeedsBaseline = false;
};

using ApplyStateFn = void (*)(void* InUser, std::uint64_t InTick, std::span<const std::byte> InState) noexcept;

struct SnapshotReceiverConfig
{
    std::uint32_t MaxStateBytes = kReplicationMaxStateBytes;
    std::uint32_t ChunkBytes = kReplicationChunkBytes;
    // Fixed simulation step, used to derive the render interpolation alpha.
    std::uint32_t FixedDtMs = 8;
};

// Client side: reassembles chunks for the next expected tick, verifies the
// checksum, applies the state and acknowledges it. A baseline may jump ahead
// after a resync request.
class SnapshotReceiver
{
public:
    SnapshotReceiver() = default;

    [[nodiscard]] bool Start(const NetLink& InLink, const NetAddress& InRemote,
                             const SnapshotReceiverConfig& InConfig, ApplyStateFn InApply,
                             void* InUser) noexcept;
    void Stop() noexcept;

    // Polls the link and processes every complete state.
    void Tick(std::uint64_t InNowMs) noexcept;

    [[nodiscard]] std::uint64_t AppliedTick() const noexcept
    {
        return MAppliedTick;
    }
    [[nodiscard]] std::uint64_t ExpectedTick() const noexcept
    {
        return MExpectedTick;
    }
    [[nodiscard]] std::uint64_t LastApplyMs() const noexcept
    {
        return MLastApplyMs;
    }
    [[nodiscard]] bool HasState() const noexcept
    {
        return MHasApplied;
    }
    [[nodiscard]] ReplicationStatus LastStatus() const noexcept
    {
        return MLastStatus;
    }
    [[nodiscard]] std::uint32_t AppliedStates() const noexcept
    {
        return MAppliedStates;
    }
    [[nodiscard]] std::uint32_t ChecksumFailures() const noexcept
    {
        return MChecksumFailures;
    }
    // 0 before the first state, then 0..1 inside the fixed step and clamped to 1
    // when the next state is late.
    [[nodiscard]] float InterpolationAlpha(std::uint64_t InNowMs) const noexcept;

protected:
    static void OnMessageThunk(void* InUser, const NetAddress& InFrom,
                               std::span<const std::byte> InMessage) noexcept;
    void HandleState(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;
    void HandleChunk(std::span<const std::byte> InMessage) noexcept;
    void CompleteState(std::uint64_t InNowMs) noexcept;
    void SendAck(const NetAddress& InTo, std::uint8_t InStatus) noexcept;
    void ResetAssembly() noexcept;

    NetLink MLink{};
    NetAddress MRemote{};
    SnapshotReceiverConfig MConfig{};
    ApplyStateFn MApply = nullptr;
    void* MApplyUser = nullptr;

    std::uint64_t MAppliedTick = 0;
    std::uint64_t MExpectedTick = 1;
    std::uint64_t MLastApplyMs = 0;
    std::uint64_t MNowMs = 0;
    std::uint32_t MAppliedStates = 0;
    std::uint32_t MChecksumFailures = 0;
    bool MHasApplied = false;
    ReplicationStatus MLastStatus = ReplicationStatus::NotStarted;

    bool MAssembling = false;
    std::uint64_t MAssembleTick = 0;
    std::uint64_t MAssembleChecksum = 0;
    std::uint32_t MAssembleBytes = 0;
    std::uint32_t MAssembleChunkCount = 0;
    std::uint32_t MAssembleReceived = 0;
    std::array<std::byte, kReplicationMaxStateBytes> MAssembleData{};
    std::uint32_t MAssembleMask = 0;
};
}
