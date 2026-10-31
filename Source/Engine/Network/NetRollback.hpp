// FemboyEngine2D - deterministic network rollback session.
#pragma once

#include "Engine/Core/Definitions.hpp"
#include "Engine/Core/DeterministicRng.hpp"
#include "Engine/ECS/CommandLog.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/Snapshot.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace Fby
{
class PhysicsSystem;
}

namespace Fby::Net
{

inline constexpr std::uint32_t kInvalidNetEntityId = 0xFFFFFFFFu;
inline constexpr std::uint32_t kMaxNetPlayers = 8;
inline constexpr std::uint32_t kMaxRollbackTicks = 256;
inline constexpr std::uint32_t kMaxNetEntities = 4096;
inline constexpr std::uint32_t kInputRingCapacity = 256;
inline constexpr std::uint64_t kInvalidNetTick = 0xFFFFFFFFFFFFFFFFull;

// Relevance grid integration. The grid itself lives in RelevanceGrid.hpp; the
struct RelevanceConfig;
struct RelevancePlan;
class RelevanceGrid;

// Stable identity of a replicated object. It is assigned by the authority (or
struct NetEntityId
{
    std::uint32_t value = kInvalidNetEntityId;

    constexpr NetEntityId() = default;
    constexpr explicit NetEntityId(std::uint32_t InId) : value(InId)
    {
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value != kInvalidNetEntityId;
    }
    [[nodiscard]] constexpr explicit operator bool() const noexcept
    {
        return IsValid();
    }
    [[nodiscard]] friend constexpr bool operator==(NetEntityId, NetEntityId) = default;
};

struct NetPlayerId
{
    std::uint8_t value = 0xFF;
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value < kMaxNetPlayers;
    }
    [[nodiscard]] friend constexpr bool operator==(NetPlayerId, NetPlayerId) = default;
};

// One player's input for one tick. Axes are signed 16-bit fixed point, not
struct NetInputFrame
{
    std::uint64_t tick = 0;
    std::int16_t moveX = 0;
    std::int16_t moveY = 0;
    std::int16_t aimX = 0;
    std::int16_t aimY = 0;
    std::uint32_t buttons = 0;
    std::uint16_t sequence = 0;
    std::uint8_t player = 0xFF;
    std::uint8_t reserved = 0;

    [[nodiscard]] constexpr float AxisX() const noexcept
    {
        return static_cast<float>(moveX) * (1.0f / 32767.0f);
    }
    [[nodiscard]] constexpr float AxisY() const noexcept
    {
        return static_cast<float>(moveY) * (1.0f / 32767.0f);
    }
    [[nodiscard]] constexpr bool Button(std::uint32_t InMask) const noexcept
    {
        return (buttons & InMask) != 0;
    }
};

static_assert(std::is_trivially_copyable_v<NetInputFrame>);
static_assert(sizeof(NetInputFrame) == 24);

enum NetInputStatus : std::uint8_t
{
    // The real input for this tick is in the ring
    NetInputConfirmed = 0,
    // Repeated from the newest known input
    NetInputPredicted = 1,
    // No input from this player yet (neutral frame)
    NetInputMissing = 2,
};

// What the simulation callback receives for one tick. Fixed size; no span into
struct NetTickInputs
{
    std::array<NetInputFrame, kMaxNetPlayers> frames{};
    std::array<std::uint8_t, kMaxNetPlayers> status{};
    std::uint32_t count = 0;

    [[nodiscard]] const NetInputFrame& Player(std::uint32_t InIndex) const noexcept
    {
        return frames[InIndex];
    }
};

// Fixed-capacity tick -> input ring. Late input overwrites the prediction in
class InputRingBuffer
{
public:
    InputRingBuffer() noexcept
    {
        Reset();
    }

    void Reset() noexcept;
    [[nodiscard]] bool Push(const NetInputFrame& InFrame) noexcept;
    [[nodiscard]] const NetInputFrame* Find(std::uint64_t InTick) const noexcept;
    [[nodiscard]] bool Has(std::uint64_t InTick) const noexcept
    {
        return Find(InTick) != nullptr;
    }
    // Newest sample at or before `tick`; the basis for prediction.
    [[nodiscard]] const NetInputFrame* LatestAtOrBefore(std::uint64_t InTick) const noexcept;

    [[nodiscard]] std::uint64_t OldestTick() const noexcept
    {
        return MCount == 0 ? kInvalidNetTick : MOldestTick;
    }
    [[nodiscard]] std::uint64_t NewestTick() const noexcept
    {
        return MCount == 0 ? kInvalidNetTick : MNewestTick;
    }
    [[nodiscard]] std::uint32_t Count() const noexcept
    {
        return MCount;
    }

private:
    struct Slot
    {
        std::uint64_t tick = kInvalidNetTick;
        NetInputFrame frame{};
    };
    std::array<Slot, kInputRingCapacity> MSlots{};
    std::uint64_t MOldestTick = kInvalidNetTick;
    std::uint64_t MNewestTick = kInvalidNetTick;
    std::uint32_t MCount = 0;
};

// Stable bidirectional map. Open addressing with tombstones, fixed capacity,
class NetEntityMap
{
public:
    NetEntityMap() noexcept
    {
        Clear();
    }

    void Clear() noexcept;
    [[nodiscard]] bool Register(NetEntityId InNet, Entity InLocal) noexcept;
    void UnregisterNet(NetEntityId InNet) noexcept;
    void UnregisterLocal(Entity InLocal) noexcept;

    [[nodiscard]] Entity Resolve(NetEntityId InNet) const noexcept;
    [[nodiscard]] NetEntityId IdOf(Entity InLocal) const noexcept;
    [[nodiscard]] std::uint32_t Size() const noexcept
    {
        return MCount;
    }

    template <typename Consumer> void ForEach(Consumer&& InConsumer) const
    {
        for (const NetEntry& ScopedEntry : MByNet)
        {
            if (ScopedEntry.net != kInvalidNetEntityId && ScopedEntry.net != kTombstone)
            {
                InConsumer(NetEntityId{ScopedEntry.net}, static_cast<Entity>(ScopedEntry.local));
            }
        }
    }

private:
    static constexpr std::uint32_t kTombstone = 0xFFFFFFFEu;
    struct NetEntry
    {
        std::uint32_t net = kInvalidNetEntityId;
        std::uint32_t local = kInvalidNetEntityId;
    };
    struct LocalEntry
    {
        std::uint32_t local = kInvalidNetEntityId;
        std::uint32_t net = kInvalidNetEntityId;
    };

    [[nodiscard]] static std::uint32_t Hash(std::uint32_t InValue) noexcept;
    [[nodiscard]] std::uint32_t FindNetSlot(std::uint32_t InNet) const noexcept;
    [[nodiscard]] std::uint32_t FindLocalSlot(std::uint32_t InLocal) const noexcept;

    std::array<NetEntry, kMaxNetEntities> MByNet{};
    std::array<LocalEntry, kMaxNetEntities> MByLocal{};
    std::uint32_t MCount = 0;
};

// The frame checksums are complementary:
struct FrameInfo
{
    std::uint64_t tick = 0;
    std::uint64_t snapshotChecksum = 0;
    std::uint64_t stateChecksum = 0;
    std::uint32_t snapshotBytes = 0;
};

struct RollbackReport
{
    std::uint64_t fromTick = 0;
    std::uint64_t toTick = 0;
    std::uint32_t replayedTicks = 0;
    std::uint32_t changedTicks = 0;
    std::uint64_t firstChangedTick = kInvalidNetTick;
    std::uint64_t finalSnapshotChecksum = 0;
    std::uint64_t finalStateChecksum = 0;
};

class NetRollbackSession
{
public:
    enum class Result : std::uint8_t
    {
        Ok = 0,
        NotInitialized,
        AlreadyInitialized,
        InvalidArgument,
        UnknownPlayer,
        MappingConflict,
        EntityLimit,
        BufferFull,
        FrameTooOld,
        SnapshotTooSmall,
        NoSnapshot,
        InvalidTick,
        RestoreFailed,
        CaptureFailed,
        CommandReplayFailed,
        NoRollbackPending,
        RollbackTooOld,
        ChecksumMismatch,
        RelevanceNotConfigured,
    };

    [[nodiscard]] static constexpr const char* ResultName(Result InResult) noexcept
    {
        switch (InResult)
        {
            case Result::Ok:
                return "Ok";
            case Result::NotInitialized:
                return "NotInitialized";
            case Result::AlreadyInitialized:
                return "AlreadyInitialized";
            case Result::InvalidArgument:
                return "InvalidArgument";
            case Result::UnknownPlayer:
                return "UnknownPlayer";
            case Result::MappingConflict:
                return "MappingConflict";
            case Result::EntityLimit:
                return "EntityLimit";
            case Result::BufferFull:
                return "BufferFull";
            case Result::FrameTooOld:
                return "FrameTooOld";
            case Result::SnapshotTooSmall:
                return "SnapshotTooSmall";
            case Result::NoSnapshot:
                return "NoSnapshot";
            case Result::InvalidTick:
                return "InvalidTick";
            case Result::RestoreFailed:
                return "RestoreFailed";
            case Result::CaptureFailed:
                return "CaptureFailed";
            case Result::CommandReplayFailed:
                return "CommandReplayFailed";
            case Result::NoRollbackPending:
                return "NoRollbackPending";
            case Result::RollbackTooOld:
                return "RollbackTooOld";
            case Result::ChecksumMismatch:
                return "ChecksumMismatch";
            case Result::RelevanceNotConfigured:
                return "RelevanceNotConfigured";
        }
        return "?";
    }

    // The simulation is a pure function of the registry, the tick inputs and the
    using SimulateFn = void (*)(void* InUser, Ecs::Registry& InRegistry, std::uint64_t InTick,
                                const NetTickInputs& InInputs, DeterministicRng& InRng) noexcept;

    struct Config
    {
        std::uint32_t playerCount = 2;
        std::uint32_t localPlayer = 0;
        // 2..kMaxRollbackTicks
        std::uint32_t rollbackTicks = 64;
        // 0 => SnapshotSize(registry) + 64 KiB of headroom, rounded to 64 bytes.
        std::uint32_t snapshotBytes = 0;
        std::uint64_t startTick = 0;

        // World-extension state (SnapshotExtras) travels in every rollback
        // slot. The capture hook writes the encoded world bytes; the restore
        // hook decodes them back into the world systems. Both hooks run on the
        // session thread during capture/restore.
        using ExtensionCaptureFn = bool (*)(void* InUser, std::span<std::byte> OutBytes,
                                            std::size_t* OutSize) noexcept;
        using ExtensionRestoreFn = void (*)(void* InUser, std::span<const std::byte> InBytes) noexcept;
        std::uint32_t extensionBytes = 0;
        ExtensionCaptureFn extensionCapture = nullptr;
        ExtensionRestoreFn extensionRestore = nullptr;
        void* extensionUser = nullptr;
    };

    NetRollbackSession();
    ~NetRollbackSession();

    NetRollbackSession(const NetRollbackSession&) = delete;
    NetRollbackSession& operator=(const NetRollbackSession&) = delete;

    // Allocation-free variant: the caller supplies the snapshot arena. It must
    [[nodiscard]] Result Initialize(Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                                    const Config& InConfig, std::span<std::byte> InSnapshotArena) noexcept;
    // Convenience variant: allocates one arena here (the only allocation the
    [[nodiscard]] Result Initialize(Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                                    const Config& InConfig);
    void Shutdown() noexcept;
    [[nodiscard]] bool Initialized() const noexcept
    {
        return MInitialized;
    }

    void SetSimulation(SimulateFn InFunction, void* InUser) noexcept
    {
        MSimulate = InFunction;
        MSimulateUser = InUser;
    }
    void SetCommandLog(Ecs::CommandLog* InLog) noexcept
    {
        MCommandLog = InLog;
    }
    // Optional physics world owned by the caller. When set, every rollback
    void SetPhysics(PhysicsSystem* InPhysics) noexcept
    {
        MPhysics = InPhysics;
    }

    // Captures tick 0 (or Config::startTick) as the rollback base and stores the
    [[nodiscard]] Result Start(const DeterministicRng& InRng) noexcept;

    // Simulates exactly the next tick. The local input is pushed into the local
    [[nodiscard]] Result Step(const NetInputFrame& InLocalInput) noexcept;

    [[nodiscard]] NetEntityId RegisterEntity(Entity InLocal, NetEntityId InPreferred = {}) noexcept;
    [[nodiscard]] Result UnregisterEntity(NetEntityId InNet) noexcept;
    [[nodiscard]] Entity ResolveEntity(NetEntityId InNet) const noexcept;
    [[nodiscard]] NetEntityId NetIdOf(Entity InLocal) const noexcept;
    [[nodiscard]] const NetEntityMap& Entities() const noexcept
    {
        return MEntities;
    }
    // Drops mappings whose local entity no longer exists (e.g. after a scene
    void PruneInvalidMappings() noexcept;

    // Allocates the mdspan cell storage once. Every later per-tick call
    [[nodiscard]] Result ConfigureRelevanceGrid(const RelevanceConfig& InConfig);
    [[nodiscard]] Result SetRelevancePlayerPosition(NetPlayerId InPlayer, Vector2D InPosition) noexcept;
    // Rebuilds the cell lists from Transform2D. Call once after the fixed-step
    [[nodiscard]] Result RefreshRelevanceGrid() noexcept;
    [[nodiscard]] Result BuildRelevancePlan(NetPlayerId InPlayer, std::uint64_t InTick,
                                            RelevancePlan& OutPlan) const noexcept;
    // Copies only the delta records whose local entity maps to a NetEntityId in
    [[nodiscard]] Result FilterDeltaForPlayer(const RelevancePlan& InPlan, std::span<const std::byte> InDelta,
                                              std::span<std::byte> OutFiltered, std::size_t* OutBytes = nullptr,
                                              bool InKeepRemovals = true) const noexcept;
    [[nodiscard]] const RelevanceGrid* Relevance() const noexcept
    {
        return MRelevance.get();
    }

    [[nodiscard]] Result SubmitLocalInput(const NetInputFrame& InInput) noexcept;
    [[nodiscard]] Result SubmitRemoteInput(NetPlayerId InPlayer, const NetInputFrame& InInput) noexcept;
    [[nodiscard]] const InputRingBuffer& Inputs(NetPlayerId InPlayer) const noexcept;
    [[nodiscard]] bool RollbackPending() const noexcept
    {
        return MPendingRollbackTick != kInvalidNetTick;
    }
    [[nodiscard]] std::uint64_t PendingRollbackTick() const noexcept
    {
        return MPendingRollbackTick;
    }

    // Executes the pending rollback. Restores the snapshot before the earliest
    [[nodiscard]] Result ReplayPending(RollbackReport* InReport = nullptr) noexcept;

    // The single restore entry point used by ReplayPending(). Destroys every
    [[nodiscard]] Result RestoreSnapshot(std::span<const std::byte> InSnapshot) noexcept;
    // Restores one captured frame from this session's arena (diagnostics and
    // tests; the replay path uses the same restore internally).
    [[nodiscard]] Result RestoreFrame(std::uint64_t InTick) noexcept;
    // Captures one frame into its rollback slot (Start uses this for the base).
    [[nodiscard]] Result CaptureFrame(std::uint64_t InTick) noexcept;

    [[nodiscard]] std::uint64_t CurrentTick() const noexcept
    {
        return MCurrentTick;
    }
    [[nodiscard]] std::uint64_t StartTick() const noexcept
    {
        return MStartTick;
    }
    [[nodiscard]] std::uint32_t RollbackWindow() const noexcept
    {
        return MRollbackTicks;
    }
    [[nodiscard]] std::size_t SnapshotArenaBytes() const noexcept
    {
        return MArena.size();
    }
    [[nodiscard]] std::size_t SnapshotSlotBytes() const noexcept
    {
        return MSlotBytes;
    }
    [[nodiscard]] bool FrameInfoAt(std::uint64_t InTick, FrameInfo& Out) const noexcept;
    [[nodiscard]] std::uint64_t SnapshotChecksumAt(std::uint64_t InTick) const noexcept
    {
        FrameInfo ScopedInfo;
        return FrameInfoAt(InTick, ScopedInfo) ? ScopedInfo.snapshotChecksum : 0;
    }
    [[nodiscard]] std::uint64_t StateChecksum() const noexcept;

    // Compares a remote report with the locally stored frame. Returns false when
    struct ChecksumComparison
    {
        bool snapshotMatches = false;
        bool stateMatches = false;
        std::uint64_t localSnapshot = 0;
        std::uint64_t remoteSnapshot = 0;
        std::uint64_t localState = 0;
        std::uint64_t remoteState = 0;
    };
    [[nodiscard]] bool CompareChecksums(std::uint64_t InTick, std::uint64_t InRemoteSnapshot,
                                        std::uint64_t InRemoteState, ChecksumComparison* Out = nullptr) const noexcept;

    [[nodiscard]] static std::uint64_t ComputeGameplayStateChecksum(const Ecs::Registry& InRegistry,
                                                                    const NetEntityMap& InEntities) noexcept;

private:
    struct FrameMeta
    {
        std::uint64_t tick = kInvalidNetTick;
        std::uint32_t snapshotBytes = 0;
        std::uint32_t reserved = 0;
        std::uint64_t snapshotChecksum = 0;
        std::uint64_t stateChecksum = 0;
        RngState rng{};
        Ecs::CommandLog::Cursor commandCursor{};
    };

    [[nodiscard]] Result ValidateConfig(const Config& InConfig) const noexcept;
    [[nodiscard]] FrameMeta* FrameAt(std::uint64_t InTick) noexcept;
    [[nodiscard]] const FrameMeta* FrameAt(std::uint64_t InTick) const noexcept;
    [[nodiscard]] std::span<std::byte> SlotBytes(std::uint64_t InTick) noexcept;
    [[nodiscard]] std::span<const std::byte> SlotBytes(std::uint64_t InTick) const noexcept;
    [[nodiscard]] Result GatherInputs(std::uint64_t InTick, NetTickInputs& Out) const noexcept;
    [[nodiscard]] Result ApplyCommandsUntil(std::uint64_t InTick) noexcept;

    Ecs::Registry* MRegistry = nullptr;
    Ecs::SnapshotSchema MSchemaStorage{};
    const Ecs::SnapshotSchema* MSchema = nullptr;
    Ecs::CommandLog* MCommandLog = nullptr;
    PhysicsSystem* MPhysics = nullptr;
    SimulateFn MSimulate = nullptr;
    void* MSimulateUser = nullptr;

    Config MConfig{};
    std::uint32_t MRollbackTicks = 0;
    std::size_t MSlotBytes = 0;
    std::span<std::byte> MArena{};
    // Extension scratch, carved after the snapshot slots in the arena.
    std::span<std::byte> MExtensionScratch{};
    std::unique_ptr<std::byte[]> MOwnedArena{};

    std::array<FrameMeta, kMaxRollbackTicks> MFrames{};
    std::array<InputRingBuffer, kMaxNetPlayers> MInputs{};
    NetEntityMap MEntities{};
    // Owned, not inline: RelevanceGrid.hpp includes this header for NetEntityMap,
    std::unique_ptr<RelevanceGrid> MRelevance;
    Ecs::CommandLog::Cursor MCommandCursor{};
    DeterministicRng MRng{};

    std::uint32_t MNextNetId = 1;
    std::uint64_t MStartTick = 0;
    std::uint64_t MCurrentTick = 0;
    std::uint64_t MPendingRollbackTick = kInvalidNetTick;
    bool MInitialized = false;
    bool MStarted = false;
};

static_assert(std::is_trivially_copyable_v<NetEntityId>);
static_assert(sizeof(NetEntityId) == 4);
static_assert(sizeof(NetInputFrame) == 24);
}
