// FemboyEngine2D - network rollback session implementation.
#include "Engine/Network/NetRollback.hpp"

#include "Engine/Core/Mirror.hpp"
#include "Engine/Network/RelevanceGrid.hpp"
#include "Engine/Physics/Physics.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <new>

namespace Fby::Net
{
namespace
{
[[nodiscard]] constexpr std::size_t AlignUp(std::size_t InValue, std::size_t InAlignment) noexcept
{
    return (InValue + InAlignment - 1u) & ~(InAlignment - 1u);
}

[[nodiscard]] constexpr std::uint32_t RawEntity(Entity InEntity) noexcept
{
    return static_cast<std::uint32_t>(entt::to_integral(InEntity));
}

[[nodiscard]] constexpr std::uint64_t SaturatingAdd(std::uint64_t InLeft, std::uint64_t InRight) noexcept
{
    return InLeft > std::numeric_limits<std::uint64_t>::max() - InRight
               ? std::numeric_limits<std::uint64_t>::max()
               : InLeft + InRight;
}

[[nodiscard]] std::uint64_t HashFrameBytes(std::uint64_t InHash, const void* InData,
                                           std::size_t InBytes) noexcept
{
    const auto* ScopedBytes = static_cast<const unsigned char*>(InData);
    for (std::size_t ScopedI = 0; ScopedI < InBytes; ++ScopedI)
    {
        InHash ^= static_cast<std::uint64_t>(ScopedBytes[ScopedI]);
        InHash *= 1099511628211ull;
    }
    return InHash;
}

// One digest for the whole frame: payload, RNG, tick, schema and extension all
// participate, so two peers that diverge in any of them compare unequal.
[[nodiscard]] std::uint64_t ComputeFrameDigest(const Ecs::SnapshotHeader& InHeader) noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.tick, sizeof(InHeader.tick));
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.baseTick, sizeof(InHeader.baseTick));
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.checksum, sizeof(InHeader.checksum));
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.extensionChecksum, sizeof(InHeader.extensionChecksum));
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.schemaHash, sizeof(InHeader.schemaHash));
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.rng, sizeof(InHeader.rng));
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.flags, sizeof(InHeader.flags));
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.entityCount, sizeof(InHeader.entityCount));
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.componentCount, sizeof(InHeader.componentCount));
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.payloadBytes, sizeof(InHeader.payloadBytes));
    ScopedHash = HashFrameBytes(ScopedHash, &InHeader.extensionBytes, sizeof(InHeader.extensionBytes));
    return ScopedHash;
}

[[nodiscard]] bool SameInput(const NetInputFrame& InLeft, const NetInputFrame& InRight) noexcept
{
    return InLeft.tick == InRight.tick && InLeft.moveX == InRight.moveX && InLeft.moveY == InRight.moveY &&
           InLeft.aimX == InRight.aimX && InLeft.aimY == InRight.aimY && InLeft.buttons == InRight.buttons &&
           InLeft.player == InRight.player && InLeft.sequence == InRight.sequence;
}

[[nodiscard]] std::size_t DesiredSlotBytes(Ecs::Registry& InRegistry, const Ecs::SnapshotSchema& InSchema,
                                           std::uint32_t InConfigured) noexcept
{
    std::size_t ScopedBytes = InConfigured != 0 ? static_cast<std::size_t>(InConfigured)
                                                : Ecs::SnapshotSize(InRegistry, InSchema) + (64u * 1024u);
    return AlignUp(std::max<std::size_t>(ScopedBytes, sizeof(Ecs::SnapshotHeader) + 256u), 64u);
}
}

// InputRingBuffer
void InputRingBuffer::Reset() noexcept
{
    for (Slot& ScopedSlot : MSlots)
    {
        ScopedSlot.tick = kInvalidNetTick;
        ScopedSlot.frame = NetInputFrame{};
    }
    MOldestTick = kInvalidNetTick;
    MNewestTick = kInvalidNetTick;
    MCount = 0;
}

bool InputRingBuffer::Push(const NetInputFrame& InFrame) noexcept
{
    if (InFrame.tick == kInvalidNetTick || InFrame.tick >= kInvalidNetTick - kInputRingCapacity)
    {
        // Reject the sentinel and any tick whose window arithmetic could wrap.
        return false;
    }
    if (MCount != 0)
    {
        // Do not accept a sample so old that its slot already contains a newer
        if (SaturatingAdd(InFrame.tick, kInputRingCapacity) <= MNewestTick)
        {
            return false;
        }
        if (InFrame.tick > SaturatingAdd(MNewestTick, kInputRingCapacity))
        {
            return false;
        }
        if (MNewestTick >= InFrame.tick && MNewestTick - InFrame.tick >= kInputRingCapacity)
        {
            return false;
        }
    }

    Slot& ScopedSlot = MSlots[InFrame.tick % kInputRingCapacity];
    if (ScopedSlot.tick != kInvalidNetTick && ScopedSlot.tick != InFrame.tick && InFrame.tick < ScopedSlot.tick)
    {
        // The slot belongs to a newer tick exactly one ring length away.
        return false;
    }
    ScopedSlot.tick = InFrame.tick;
    ScopedSlot.frame = InFrame;
    ScopedSlot.frame.tick = InFrame.tick;

    // Recompute the window bounds. The ring is tiny (256 slots) and Push runs at
    MCount = 0;
    std::uint64_t ScopedOldest = kInvalidNetTick;
    std::uint64_t ScopedNewest = 0;
    for (const Slot& ScopedEntry : MSlots)
    {
        if (ScopedEntry.tick == kInvalidNetTick)
        {
            continue;
        }
        ++MCount;
        ScopedOldest = std::min(ScopedOldest, ScopedEntry.tick);
        ScopedNewest = std::max(ScopedNewest, ScopedEntry.tick);
    }
    MOldestTick = MCount != 0 ? ScopedOldest : kInvalidNetTick;
    MNewestTick = MCount != 0 ? ScopedNewest : kInvalidNetTick;
    return true;
}

const NetInputFrame* InputRingBuffer::Find(std::uint64_t InTick) const noexcept
{
    if (InTick == kInvalidNetTick)
    {
        return nullptr;
    }
    const Slot& ScopedSlot = MSlots[InTick % kInputRingCapacity];
    return ScopedSlot.tick == InTick ? &ScopedSlot.frame : nullptr;
}

const NetInputFrame* InputRingBuffer::LatestAtOrBefore(std::uint64_t InTick) const noexcept
{
    const NetInputFrame* ScopedBest = nullptr;
    std::uint64_t ScopedBestTick = kInvalidNetTick;
    for (const Slot& ScopedSlot : MSlots)
    {
        if (ScopedSlot.tick == kInvalidNetTick || ScopedSlot.tick > InTick)
        {
            continue;
        }
        if (ScopedBest == nullptr || ScopedSlot.tick > ScopedBestTick)
        {
            ScopedBest = &ScopedSlot.frame;
            ScopedBestTick = ScopedSlot.tick;
        }
    }
    return ScopedBest;
}

// NetEntityMap
std::uint32_t NetEntityMap::Hash(std::uint32_t InValue) noexcept
{
    InValue ^= InValue >> 16u;
    InValue *= 0x7FEB352Du;
    InValue ^= InValue >> 15u;
    InValue *= 0x846CA68Bu;
    InValue ^= InValue >> 16u;
    return InValue;
}

void NetEntityMap::Clear() noexcept
{
    for (NetEntry& ScopedEntry : MByNet)
    {
        ScopedEntry = NetEntry{};
    }
    for (LocalEntry& ScopedEntry : MByLocal)
    {
        ScopedEntry = LocalEntry{};
    }
    MCount = 0;
}

std::uint32_t NetEntityMap::FindNetSlot(std::uint32_t InNet) const noexcept
{
    constexpr std::uint32_t ScopedKMask = kMaxNetEntities - 1u;
    std::uint32_t ScopedIndex = Hash(InNet) & ScopedKMask;
    std::uint32_t ScopedFirstTombstone = kInvalidNetEntityId;
    for (std::uint32_t ScopedProbe = 0; ScopedProbe < kMaxNetEntities; ++ScopedProbe)
    {
        const NetEntry& ScopedEntry = MByNet[ScopedIndex];
        if (ScopedEntry.net == InNet)
        {
            return ScopedIndex;
        }
        if (ScopedEntry.net == kInvalidNetEntityId)
        {
            return ScopedFirstTombstone != kInvalidNetEntityId ? ScopedFirstTombstone : ScopedIndex;
        }
        if (ScopedEntry.net == kTombstone && ScopedFirstTombstone == kInvalidNetEntityId)
        {
            ScopedFirstTombstone = ScopedIndex;
        }
        ScopedIndex = (ScopedIndex + 1u) & ScopedKMask;
    }
    return ScopedFirstTombstone;
}

std::uint32_t NetEntityMap::FindLocalSlot(std::uint32_t InLocal) const noexcept
{
    constexpr std::uint32_t ScopedKMask = kMaxNetEntities - 1u;
    std::uint32_t ScopedIndex = Hash(InLocal) & ScopedKMask;
    std::uint32_t ScopedFirstTombstone = kInvalidNetEntityId;
    for (std::uint32_t ScopedProbe = 0; ScopedProbe < kMaxNetEntities; ++ScopedProbe)
    {
        const LocalEntry& ScopedEntry = MByLocal[ScopedIndex];
        if (ScopedEntry.local == InLocal)
        {
            return ScopedIndex;
        }
        if (ScopedEntry.local == kInvalidNetEntityId)
        {
            return ScopedFirstTombstone != kInvalidNetEntityId ? ScopedFirstTombstone : ScopedIndex;
        }
        if (ScopedEntry.local == kTombstone && ScopedFirstTombstone == kInvalidNetEntityId)
        {
            ScopedFirstTombstone = ScopedIndex;
        }
        ScopedIndex = (ScopedIndex + 1u) & ScopedKMask;
    }
    return ScopedFirstTombstone;
}

bool NetEntityMap::Register(NetEntityId InNet, Entity InLocal) noexcept
{
    if (!InNet.IsValid() || InLocal == entt::null)
    {
        return false;
    }
    const std::uint32_t ScopedLocalRaw = RawEntity(InLocal);
    if (ScopedLocalRaw == kInvalidNetEntityId || ScopedLocalRaw == kTombstone)
    {
        return false;
    }

    const std::uint32_t ScopedNetSlot = FindNetSlot(InNet.value);
    if (ScopedNetSlot == kInvalidNetEntityId)
    {
        return false;
    }
    if (MByNet[ScopedNetSlot].net == InNet.value)
    {
        return MByNet[ScopedNetSlot].local == ScopedLocalRaw;
    }

    const std::uint32_t ScopedLocalSlot = FindLocalSlot(ScopedLocalRaw);
    if (ScopedLocalSlot == kInvalidNetEntityId)
    {
        return false;
    }
    if (MByLocal[ScopedLocalSlot].local == ScopedLocalRaw)
    {
        return MByLocal[ScopedLocalSlot].net == InNet.value;
    }
    if (MCount >= kMaxNetEntities)
    {
        return false;
    }

    MByNet[ScopedNetSlot] = NetEntry{InNet.value, ScopedLocalRaw};
    MByLocal[ScopedLocalSlot] = LocalEntry{ScopedLocalRaw, InNet.value};
    ++MCount;
    return true;
}

void NetEntityMap::UnregisterNet(NetEntityId InNet) noexcept
{
    if (!InNet.IsValid())
    {
        return;
    }
    const std::uint32_t ScopedNetSlot = FindNetSlot(InNet.value);
    if (ScopedNetSlot == kInvalidNetEntityId || MByNet[ScopedNetSlot].net != InNet.value)
    {
        return;
    }
    const std::uint32_t ScopedLocalRaw = MByNet[ScopedNetSlot].local;
    const std::uint32_t ScopedLocalSlot = FindLocalSlot(ScopedLocalRaw);
    if (ScopedLocalSlot != kInvalidNetEntityId && MByLocal[ScopedLocalSlot].local == ScopedLocalRaw)
    {
        MByLocal[ScopedLocalSlot] = LocalEntry{kTombstone, kInvalidNetEntityId};
    }
    MByNet[ScopedNetSlot] = NetEntry{kTombstone, kInvalidNetEntityId};
    --MCount;
}

void NetEntityMap::UnregisterLocal(Entity InLocal) noexcept
{
    const NetEntityId ScopedNet = IdOf(InLocal);
    if (ScopedNet.IsValid())
    {
        UnregisterNet(ScopedNet);
    }
}

Entity NetEntityMap::Resolve(NetEntityId InNet) const noexcept
{
    if (!InNet.IsValid())
    {
        return entt::null;
    }
    const std::uint32_t ScopedSlot = FindNetSlot(InNet.value);
    if (ScopedSlot == kInvalidNetEntityId || MByNet[ScopedSlot].net != InNet.value)
    {
        return entt::null;
    }
    return static_cast<Entity>(MByNet[ScopedSlot].local);
}

NetEntityId NetEntityMap::IdOf(Entity InLocal) const noexcept
{
    if (InLocal == entt::null)
    {
        return {};
    }
    const std::uint32_t ScopedLocalRaw = RawEntity(InLocal);
    const std::uint32_t ScopedSlot = FindLocalSlot(ScopedLocalRaw);
    if (ScopedSlot == kInvalidNetEntityId || MByLocal[ScopedSlot].local != ScopedLocalRaw ||
        MByLocal[ScopedSlot].net == kInvalidNetEntityId)
    {
        return {};
    }
    return NetEntityId{MByLocal[ScopedSlot].net};
}

// NetRollbackSession lifecycle
NetRollbackSession::NetRollbackSession() = default;

NetRollbackSession::~NetRollbackSession()
{
    Shutdown();
}

NetRollbackSession::Result NetRollbackSession::ValidateConfig(const Config& InConfig) const noexcept
{
    if (InConfig.playerCount == 0 || InConfig.playerCount > kMaxNetPlayers)
    {
        return Result::InvalidArgument;
    }
    if (InConfig.localPlayer >= InConfig.playerCount)
    {
        return Result::InvalidArgument;
    }
    if (InConfig.rollbackTicks < 2 || InConfig.rollbackTicks > kMaxRollbackTicks)
    {
        return Result::InvalidArgument;
    }
    if (InConfig.snapshotBytes != 0 && InConfig.snapshotBytes < sizeof(Ecs::SnapshotHeader) + 256u)
    {
        return Result::InvalidArgument;
    }
    if (InConfig.extensionBytes > (1u << 20))
    {
        return Result::InvalidArgument;
    }
    if (InConfig.extensionBytes != 0 && InConfig.extensionCapture == nullptr)
    {
        return Result::InvalidArgument;
    }
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::Initialize(Ecs::Registry& InRegistry,
                                                          const Ecs::SnapshotSchema& InSchema, const Config& InConfig,
                                                          std::span<std::byte> InSnapshotArena) noexcept
{
    Shutdown();
    const Result ScopedValidation = ValidateConfig(InConfig);
    if (ScopedValidation != Result::Ok)
    {
        return ScopedValidation;
    }

    MConfig = InConfig;
    MRollbackTicks = InConfig.rollbackTicks;
    MSlotBytes = DesiredSlotBytes(InRegistry, InSchema, InConfig.snapshotBytes) + InConfig.extensionBytes;
    const std::size_t ScopedRequired = MSlotBytes * static_cast<std::size_t>(MRollbackTicks) +
                                       InConfig.extensionBytes;
    if (InSnapshotArena.size() < ScopedRequired)
    {
        return Result::BufferFull;
    }

    MRegistry = &InRegistry;
    MSchemaStorage = InSchema;
    MSchema = &MSchemaStorage;
    MArena = InSnapshotArena.first(ScopedRequired);
    MExtensionScratch = MArena.subspan(MSlotBytes * static_cast<std::size_t>(MRollbackTicks),
                                       InConfig.extensionBytes);
    MFrames.fill(FrameMeta{});
    for (InputRingBuffer& ScopedInput : MInputs)
    {
        ScopedInput.Reset();
    }
    MEntities.Clear();
    MCommandCursor = {};
    MNextNetId = 1;
    MStartTick = InConfig.startTick;
    MCurrentTick = InConfig.startTick;
    MPendingRollbackTick = kInvalidNetTick;
    MRng = DeterministicRng{};
    MInitialized = true;
    MStarted = false;
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::Initialize(Ecs::Registry& InRegistry,
                                                          const Ecs::SnapshotSchema& InSchema, const Config& InConfig)
{
    Shutdown();
    const Result ScopedValidation = ValidateConfig(InConfig);
    if (ScopedValidation != Result::Ok)
    {
        return ScopedValidation;
    }
    const std::size_t ScopedSlotBytes = DesiredSlotBytes(InRegistry, InSchema, InConfig.snapshotBytes);
    const std::size_t ScopedTotal =
        ScopedSlotBytes * static_cast<std::size_t>(InConfig.rollbackTicks) + InConfig.extensionBytes;
    auto ScopedOwned = std::unique_ptr<std::byte[]>(new (std::nothrow) std::byte[ScopedTotal]);
    if (!ScopedOwned)
    {
        return Result::BufferFull;
    }
    const Result ScopedResult =
        Initialize(InRegistry, InSchema, InConfig, std::span<std::byte>(ScopedOwned.get(), ScopedTotal));
    if (ScopedResult == Result::Ok)
    {
        MOwnedArena = std::move(ScopedOwned);
    }
    return ScopedResult;
}

void NetRollbackSession::Shutdown() noexcept
{
    MOwnedArena.reset();
    MArena = {};
    MFrames.fill(FrameMeta{});
    for (InputRingBuffer& ScopedInput : MInputs)
    {
        ScopedInput.Reset();
    }
    MEntities.Clear();
    MRegistry = nullptr;
    MSchema = nullptr;
    MSchemaStorage = Ecs::SnapshotSchema{};
    MCommandLog = nullptr;
    MSimulate = nullptr;
    MSimulateUser = nullptr;
    MConfig = Config{};
    MRollbackTicks = 0;
    MSlotBytes = 0;
    MExtensionScratch = {};
    MNextNetId = 1;
    MStartTick = 0;
    MCurrentTick = 0;
    MPendingRollbackTick = kInvalidNetTick;
    MCommandCursor = {};
    MInitialized = false;
    MStarted = false;
}

NetRollbackSession::FrameMeta* NetRollbackSession::FrameAt(std::uint64_t InTick) noexcept
{
    if (MRollbackTicks == 0)
    {
        return nullptr;
    }
    FrameMeta& ScopedFrame = MFrames[InTick % MRollbackTicks];
    return ScopedFrame.tick == InTick ? &ScopedFrame : nullptr;
}

const NetRollbackSession::FrameMeta* NetRollbackSession::FrameAt(std::uint64_t InTick) const noexcept
{
    if (MRollbackTicks == 0)
    {
        return nullptr;
    }
    const FrameMeta& ScopedFrame = MFrames[InTick % MRollbackTicks];
    return ScopedFrame.tick == InTick ? &ScopedFrame : nullptr;
}

std::span<std::byte> NetRollbackSession::SlotBytes(std::uint64_t InTick) noexcept
{
    const std::size_t ScopedIndex = static_cast<std::size_t>(InTick % MRollbackTicks);
    return MArena.subspan(ScopedIndex * MSlotBytes, MSlotBytes);
}

std::span<const std::byte> NetRollbackSession::SlotBytes(std::uint64_t InTick) const noexcept
{
    const std::size_t ScopedIndex = static_cast<std::size_t>(InTick % MRollbackTicks);
    return MArena.subspan(ScopedIndex * MSlotBytes, MSlotBytes);
}

NetRollbackSession::Result NetRollbackSession::CaptureFrame(std::uint64_t InTick) noexcept
{
    if (!MInitialized || MRegistry == nullptr || MSchema == nullptr)
    {
        return Result::NotInitialized;
    }
    const std::span<std::byte> ScopedBuffer = SlotBytes(InTick);
    // Encode the world extension before sizing, so the slot check includes it.
    Ecs::SnapshotExtras ScopedExtras{};
    if (MConfig.extensionBytes != 0)
    {
        if (MConfig.extensionCapture == nullptr)
        {
            return Result::CaptureFailed;
        }
        std::size_t ScopedExtensionBytes = 0;
        if (!MConfig.extensionCapture(MConfig.extensionUser, MExtensionScratch, &ScopedExtensionBytes) ||
            ScopedExtensionBytes > MExtensionScratch.size())
        {
            return Result::CaptureFailed;
        }
        ScopedExtras.extension = MExtensionScratch.first(ScopedExtensionBytes);
    }
    if (Ecs::SnapshotSize(*MRegistry, *MSchema, ScopedExtras.extension.size()) > ScopedBuffer.size())
    {
        return Result::SnapshotTooSmall;
    }
    std::size_t ScopedWritten = 0;
    if (!Ecs::CaptureSnapshot(*MRegistry, *MSchema, ScopedBuffer, InTick, MRng, &ScopedWritten, ScopedExtras))
    {
        return Result::CaptureFailed;
    }
    if (ScopedWritten < sizeof(Ecs::SnapshotHeader) || ScopedWritten > ScopedBuffer.size())
    {
        return Result::CaptureFailed;
    }

    Ecs::SnapshotHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, ScopedBuffer.data(), sizeof(ScopedHeader));
    FrameMeta& ScopedFrame = MFrames[InTick % MRollbackTicks];
    ScopedFrame = FrameMeta{};
    ScopedFrame.tick = InTick;
    ScopedFrame.snapshotBytes = static_cast<std::uint32_t>(ScopedWritten);
    ScopedFrame.snapshotChecksum = ComputeFrameDigest(ScopedHeader);
    ScopedFrame.stateChecksum = ComputeGameplayStateChecksum(*MRegistry, MEntities);
    ScopedFrame.rng = MRng.Save();
    ScopedFrame.commandCursor = MCommandCursor;
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::Start(const DeterministicRng& InRng) noexcept
{
    if (!MInitialized)
    {
        return Result::NotInitialized;
    }
    if (MStarted)
    {
        return Result::AlreadyInitialized;
    }
    MRng = InRng;
    MStartTick = MConfig.startTick;
    MCurrentTick = MStartTick;
    MCommandCursor = {};
    MPendingRollbackTick = kInvalidNetTick;
    const Result ScopedCaptured = CaptureFrame(MStartTick);
    if (ScopedCaptured != Result::Ok)
    {
        return ScopedCaptured;
    }
    MStarted = true;
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::Step(const NetInputFrame& InLocalInput) noexcept
{
    if (!MInitialized)
    {
        return Result::NotInitialized;
    }
    if (!MStarted)
    {
        return Result::InvalidTick;
    }
    if (MSimulate == nullptr)
    {
        return Result::InvalidArgument;
    }

    const std::uint64_t ScopedTick = MCurrentTick + 1u;
    NetInputFrame ScopedInput = InLocalInput;
    ScopedInput.tick = ScopedTick;
    ScopedInput.player = static_cast<std::uint8_t>(MConfig.localPlayer);
    const Result ScopedSubmitted = SubmitLocalInput(ScopedInput);
    if (ScopedSubmitted != Result::Ok)
    {
        return ScopedSubmitted;
    }

    const Result ScopedCommands = ApplyCommandsUntil(ScopedTick);
    if (ScopedCommands != Result::Ok)
    {
        return ScopedCommands;
    }

    NetTickInputs ScopedInputs;
    const Result ScopedGathered = GatherInputs(ScopedTick, ScopedInputs);
    if (ScopedGathered != Result::Ok)
    {
        return ScopedGathered;
    }

    MSimulate(MSimulateUser, *MRegistry, ScopedTick, ScopedInputs, MRng);

    const Result ScopedCaptured = CaptureFrame(ScopedTick);
    if (ScopedCaptured != Result::Ok)
    {
        return ScopedCaptured;
    }
    MCurrentTick = ScopedTick;
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::ApplyCommandsUntil(std::uint64_t InTick) noexcept
{
    if (MCommandLog == nullptr)
    {
        return Result::Ok;
    }
    if (!MCommandLog->ApplyUntil(*MRegistry, *MSchema, InTick, MCommandCursor))
    {
        return Result::CommandReplayFailed;
    }
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::GatherInputs(std::uint64_t InTick, NetTickInputs& Out) const noexcept
{
    Out = NetTickInputs{};
    Out.count = MConfig.playerCount;
    for (std::uint32_t ScopedPlayer = 0; ScopedPlayer < MConfig.playerCount; ++ScopedPlayer)
    {
        const InputRingBuffer& ScopedRing = MInputs[ScopedPlayer];
        if (const NetInputFrame* ScopedExact = ScopedRing.Find(InTick); ScopedExact != nullptr)
        {
            Out.frames[ScopedPlayer] = *ScopedExact;
            Out.status[ScopedPlayer] = NetInputConfirmed;
            continue;
        }
        if (const NetInputFrame* ScopedPrevious = ScopedRing.LatestAtOrBefore(InTick); ScopedPrevious != nullptr)
        {
            Out.frames[ScopedPlayer] = *ScopedPrevious;
            Out.frames[ScopedPlayer].tick = InTick;
            Out.frames[ScopedPlayer].player = static_cast<std::uint8_t>(ScopedPlayer);
            Out.status[ScopedPlayer] = NetInputPredicted;
            continue;
        }
        Out.frames[ScopedPlayer] = NetInputFrame{};
        Out.frames[ScopedPlayer].tick = InTick;
        Out.frames[ScopedPlayer].player = static_cast<std::uint8_t>(ScopedPlayer);
        Out.status[ScopedPlayer] = NetInputMissing;
    }
    return Result::Ok;
}

// Entity identity
NetEntityId NetRollbackSession::RegisterEntity(Entity InLocal, NetEntityId InPreferred) noexcept
{
    if (!MInitialized || MRegistry == nullptr || InLocal == entt::null || !MRegistry->valid(InLocal))
    {
        return {};
    }
    if (const NetEntityId ScopedExisting = MEntities.IdOf(InLocal); ScopedExisting.IsValid())
    {
        return ScopedExisting;
    }
    if (InPreferred.IsValid())
    {
        if (MEntities.Resolve(InPreferred) != entt::null)
        {
            return {};
        }
        return MEntities.Register(InPreferred, InLocal) ? InPreferred : NetEntityId{};
    }

    for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < kMaxNetEntities; ++ScopedAttempt)
    {
        std::uint32_t ScopedRaw = MNextNetId++;
        if (ScopedRaw == 0 || ScopedRaw == kInvalidNetEntityId || ScopedRaw == 0xFFFFFFFEu)
        {
            continue;
        }
        const NetEntityId ScopedCandidate{ScopedRaw};
        if (MEntities.Resolve(ScopedCandidate) != entt::null)
        {
            continue;
        }
        if (MEntities.Register(ScopedCandidate, InLocal))
        {
            return ScopedCandidate;
        }
    }
    return {};
}

NetRollbackSession::Result NetRollbackSession::UnregisterEntity(NetEntityId InNet) noexcept
{
    if (!MInitialized)
    {
        return Result::NotInitialized;
    }
    if (!InNet.IsValid() || MEntities.Resolve(InNet) == entt::null)
    {
        return Result::InvalidArgument;
    }
    MEntities.UnregisterNet(InNet);
    return Result::Ok;
}

Entity NetRollbackSession::ResolveEntity(NetEntityId InNet) const noexcept
{
    return MInitialized ? MEntities.Resolve(InNet) : entt::null;
}

NetEntityId NetRollbackSession::NetIdOf(Entity InLocal) const noexcept
{
    return MInitialized ? MEntities.IdOf(InLocal) : NetEntityId{};
}

void NetRollbackSession::PruneInvalidMappings() noexcept
{
    if (!MInitialized || MRegistry == nullptr)
    {
        return;
    }
    std::array<NetEntityId, kMaxNetEntities> ScopedStale{};
    std::uint32_t ScopedStaleCount = 0;
    MEntities.ForEach(
        [&](NetEntityId InNet, Entity InLocal)
        {
            if (ScopedStaleCount < ScopedStale.size() && !MRegistry->valid(InLocal))
            {
                ScopedStale[ScopedStaleCount++] = InNet;
            }
        });
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedStaleCount; ++ScopedI)
    {
        MEntities.UnregisterNet(ScopedStale[ScopedI]);
    }
}

// Input submission and rollback
NetRollbackSession::Result NetRollbackSession::SubmitLocalInput(const NetInputFrame& InInput) noexcept
{
    if (!MInitialized)
    {
        return Result::NotInitialized;
    }
    NetInputFrame ScopedFrame = InInput;
    ScopedFrame.player = static_cast<std::uint8_t>(MConfig.localPlayer);
    if (!MInputs[MConfig.localPlayer].Push(ScopedFrame))
    {
        return Result::FrameTooOld;
    }
    if (MStarted && ScopedFrame.tick <= MCurrentTick && ScopedFrame.tick >= MStartTick)
    {
        MPendingRollbackTick = std::min(MPendingRollbackTick, ScopedFrame.tick);
    }
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::SubmitRemoteInput(NetPlayerId InPlayer,
                                                                 const NetInputFrame& InInput) noexcept
{
    if (!MInitialized)
    {
        return Result::NotInitialized;
    }
    if (!InPlayer.IsValid() || InPlayer.value >= MConfig.playerCount || InPlayer.value == MConfig.localPlayer)
    {
        return Result::UnknownPlayer;
    }
    if (InInput.tick == kInvalidNetTick || InInput.tick < MStartTick)
    {
        return Result::InvalidTick;
    }
    if (MStarted && InInput.tick <= MCurrentTick)
    {
        // To correct tick T we need the snapshot at T-1. T-1 must still be in
        if (InInput.tick == MStartTick || InInput.tick + MRollbackTicks - 2u < MCurrentTick)
        {
            return Result::RollbackTooOld;
        }
    }

    InputRingBuffer& ScopedRing = MInputs[InPlayer.value];
    const NetInputFrame* ScopedExisting = ScopedRing.Find(InInput.tick);
    if (ScopedExisting != nullptr)
    {
        NetInputFrame ScopedNormalized = InInput;
        ScopedNormalized.player = InPlayer.value;
        if (SameInput(*ScopedExisting, ScopedNormalized))
        {
            // A duplicate retransmit must not cause a rollback
            return Result::Ok;
        }
    }

    NetInputFrame ScopedFrame = InInput;
    ScopedFrame.player = InPlayer.value;
    // A far-future first sample would anchor the ring and lock out current
    // ticks. The bound is the larger of the rollback window and the input ring:
    // a client may legitimately start a few ticks ahead of the server.
    const std::uint64_t ScopedAheadLimit =
        std::max<std::uint64_t>(MRollbackTicks, static_cast<std::uint64_t>(kInputRingCapacity));
    if (ScopedFrame.tick > SaturatingAdd(MCurrentTick, ScopedAheadLimit))
    {
        return Result::InvalidTick;
    }
    if (!ScopedRing.Push(ScopedFrame))
    {
        return Result::FrameTooOld;
    }
    if (MStarted && ScopedFrame.tick <= MCurrentTick)
    {
        MPendingRollbackTick = std::min(MPendingRollbackTick, ScopedFrame.tick);
    }
    return Result::Ok;
}

const InputRingBuffer& NetRollbackSession::Inputs(NetPlayerId InPlayer) const noexcept
{
    return MInputs[InPlayer.IsValid() && InPlayer.value < kMaxNetPlayers ? InPlayer.value : 0];
}

NetRollbackSession::Result NetRollbackSession::RestoreSnapshot(std::span<const std::byte> InSnapshot) noexcept
{
    if (!MInitialized || MRegistry == nullptr || MSchema == nullptr)
    {
        return Result::NotInitialized;
    }

    // Validate before destroying native state: a rejected snapshot must leave
    // the frame's bodies alive.
    std::string_view ScopedError;
    if (!Ecs::ValidateSnapshot(InSnapshot, *MSchema, &ScopedError))
    {
        return Result::RestoreFailed;
    }

    // Destroy every live native body *before* Registry::clear(). Clearing
    if (MPhysics != nullptr)
    {
        (void)MPhysics->DestroyAllBodies(MRegistry);
    }

    Ecs::SnapshotRestoreExtras ScopedRestoreExtras{};
    Ecs::SnapshotHeader ScopedHeader{};
    if (MConfig.extensionBytes != 0)
    {
        if (InSnapshot.size() < sizeof(ScopedHeader))
        {
            return Result::RestoreFailed;
        }
        std::memcpy(&ScopedHeader, InSnapshot.data(), sizeof(ScopedHeader));
        if (ScopedHeader.extensionBytes > MExtensionScratch.size())
        {
            return Result::RestoreFailed;
        }
        ScopedRestoreExtras.extension = MExtensionScratch.first(ScopedHeader.extensionBytes);
    }
    if (!Ecs::RestoreSnapshot(*MRegistry, *MSchema, InSnapshot, &MRng, ScopedRestoreExtras))
    {
        return Result::RestoreFailed;
    }

    // Snapshot bytes never carry a b2 handle: rebuild the native bodies now so
    if (MPhysics != nullptr)
    {
        MPhysics->EnsureBodies(*MRegistry);
    }
    // Hand the decoded world extension back to its owner.
    if (MConfig.extensionBytes != 0 && MConfig.extensionRestore != nullptr)
    {
        MConfig.extensionRestore(MConfig.extensionUser, MExtensionScratch.first(ScopedHeader.extensionBytes));
    }
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::RestoreFrame(std::uint64_t InTick) noexcept
{
    if (!MInitialized || MRegistry == nullptr || MSchema == nullptr)
    {
        return Result::NotInitialized;
    }
    const FrameMeta* ScopedMeta = FrameAt(InTick);
    if (ScopedMeta == nullptr || ScopedMeta->snapshotBytes == 0 || ScopedMeta->snapshotBytes > MSlotBytes)
    {
        return Result::NoSnapshot;
    }
    return RestoreSnapshot(SlotBytes(InTick).first(ScopedMeta->snapshotBytes));
}

NetRollbackSession::Result NetRollbackSession::ReplayPending(RollbackReport* InReport) noexcept
{
    if (!MInitialized)
    {
        return Result::NotInitialized;
    }
    // Any failure clears the pending tick: a single failed replay must not
    // wedge every later call.
    const auto ScopedFail = [this](Result InResult) noexcept
    {
        MPendingRollbackTick = kInvalidNetTick;
        return InResult;
    };
    if (!MStarted || MPendingRollbackTick == kInvalidNetTick)
    {
        return ScopedFail(Result::NoRollbackPending);
    }
    if (MSimulate == nullptr)
    {
        return ScopedFail(Result::InvalidArgument);
    }
    const std::uint64_t ScopedFromTick = MPendingRollbackTick;
    const std::uint64_t ScopedTargetTick = MCurrentTick;
    if (ScopedFromTick <= MStartTick)
    {
        return ScopedFail(Result::RollbackTooOld);
    }
    const FrameMeta* ScopedBaseMeta = FrameAt(ScopedFromTick - 1u);
    if (ScopedBaseMeta == nullptr)
    {
        return ScopedFail(Result::NoSnapshot);
    }
    const FrameMeta ScopedBase = *ScopedBaseMeta;
    if (ScopedBase.snapshotBytes == 0 || ScopedBase.snapshotBytes > MSlotBytes)
    {
        return ScopedFail(Result::NoSnapshot);
    }
    const std::span<const std::byte> ScopedBaseBytes = SlotBytes(ScopedFromTick - 1u).first(ScopedBase.snapshotBytes);
    if (RestoreSnapshot(ScopedBaseBytes) != Result::Ok)
    {
        return ScopedFail(Result::RestoreFailed);
    }
    MCommandCursor = ScopedBase.commandCursor;

    RollbackReport ScopedLocalReport;
    ScopedLocalReport.fromTick = ScopedFromTick;
    ScopedLocalReport.toTick = ScopedTargetTick;

    for (std::uint64_t ScopedTick = ScopedFromTick; ScopedTick <= ScopedTargetTick; ++ScopedTick)
    {
        const Result ScopedCommands = ApplyCommandsUntil(ScopedTick);
        if (ScopedCommands != Result::Ok)
        {
            return ScopedFail(ScopedCommands);
        }
        NetTickInputs ScopedInputs;
        const Result ScopedGathered = GatherInputs(ScopedTick, ScopedInputs);
        if (ScopedGathered != Result::Ok)
        {
            return ScopedFail(ScopedGathered);
        }

        const std::uint64_t ScopedPreviousChecksum = SnapshotChecksumAt(ScopedTick);
        MSimulate(MSimulateUser, *MRegistry, ScopedTick, ScopedInputs, MRng);
        const Result ScopedCaptured = CaptureFrame(ScopedTick);
        if (ScopedCaptured != Result::Ok)
        {
            return ScopedFail(ScopedCaptured);
        }
        const std::uint64_t ScopedNewChecksum = MFrames[ScopedTick % MRollbackTicks].snapshotChecksum;
        ++ScopedLocalReport.replayedTicks;
        if (ScopedNewChecksum != ScopedPreviousChecksum)
        {
            ++ScopedLocalReport.changedTicks;
            if (ScopedLocalReport.firstChangedTick == kInvalidNetTick)
            {
                ScopedLocalReport.firstChangedTick = ScopedTick;
            }
        }
    }

    MCurrentTick = ScopedTargetTick;
    MPendingRollbackTick = kInvalidNetTick;
    const FrameMeta* ScopedFinalFrame = FrameAt(ScopedTargetTick);
    if (ScopedFinalFrame != nullptr)
    {
        ScopedLocalReport.finalSnapshotChecksum = ScopedFinalFrame->snapshotChecksum;
        ScopedLocalReport.finalStateChecksum = ScopedFinalFrame->stateChecksum;
    }
    if (InReport != nullptr)
    {
        *InReport = ScopedLocalReport;
    }
    return Result::Ok;
}

// Inspection / checksums
bool NetRollbackSession::FrameInfoAt(std::uint64_t InTick, FrameInfo& Out) const noexcept
{
    const FrameMeta* ScopedFrame = FrameAt(InTick);
    if (ScopedFrame == nullptr)
    {
        return false;
    }
    Out.tick = ScopedFrame->tick;
    Out.snapshotChecksum = ScopedFrame->snapshotChecksum;
    Out.stateChecksum = ScopedFrame->stateChecksum;
    Out.snapshotBytes = ScopedFrame->snapshotBytes;
    return true;
}

std::uint64_t NetRollbackSession::StateChecksum() const noexcept
{
    if (!MInitialized || MRegistry == nullptr)
    {
        return 0;
    }
    return ComputeGameplayStateChecksum(*MRegistry, MEntities);
}

std::uint64_t NetRollbackSession::ComputeGameplayStateChecksum(const Ecs::Registry& InRegistry,
                                                               const NetEntityMap& InEntities) noexcept
{
    // Order-independent fold: h1 accumulates, h2 xors rotated mixes. Snapshot
    std::uint64_t ScopedSum = 0x243F6A8885A308D3ull;
    std::uint64_t ScopedXored = 0xC2B2AE3D27D4EB4Full;
    std::uint64_t ScopedCount = 0;
    auto ScopedView = InRegistry.view<GameplayState>();
    for (const Entity ScopedEntity : ScopedView)
    {
        const GameplayState& ScopedState = ScopedView.get<GameplayState>(ScopedEntity);
        const NetEntityId ScopedNet = InEntities.IdOf(ScopedEntity);
        if (!ScopedNet.IsValid())
        {
            // Local-only entities are not part of the network state: a peer
            // cannot derive the same local ids, so folding them would make
            // every cross-peer comparison fail.
            continue;
        }
        const std::uint64_t ScopedIdentity = (static_cast<std::uint64_t>(ScopedNet.value) << 1u) | 1u;
        const std::uint64_t ScopedMixed = Reflect::MixHash(ScopedState.Bits, ScopedIdentity);
        ScopedSum += ScopedMixed;
        ScopedXored ^= std::rotl(ScopedMixed, static_cast<int>(ScopedState.Bits & 63u));
        ++ScopedCount;
    }
    return Reflect::MixHash(Reflect::MixHash(ScopedSum, ScopedXored), ScopedCount);
}

bool NetRollbackSession::CompareChecksums(std::uint64_t InTick, std::uint64_t InRemoteSnapshot,
                                          std::uint64_t InRemoteState, ChecksumComparison* Out) const noexcept
{
    const FrameMeta* ScopedFrame = FrameAt(InTick);
    if (ScopedFrame == nullptr)
    {
        if (Out != nullptr)
        {
            *Out = ChecksumComparison{};
        }
        return false;
    }
    const bool ScopedSnapshotMatches = ScopedFrame->snapshotChecksum == InRemoteSnapshot;
    const bool ScopedStateMatches = ScopedFrame->stateChecksum == InRemoteState;
    if (Out != nullptr)
    {
        Out->snapshotMatches = ScopedSnapshotMatches;
        Out->stateMatches = ScopedStateMatches;
        Out->localSnapshot = ScopedFrame->snapshotChecksum;
        Out->remoteSnapshot = InRemoteSnapshot;
        Out->localState = ScopedFrame->stateChecksum;
        Out->remoteState = InRemoteState;
    }
    return ScopedSnapshotMatches && ScopedStateMatches;
}

// Spatial relevance integration
namespace
{
struct FilteredRecordHeader
{
    Entity entity = entt::null;
    std::uint16_t flags = 0;
    std::uint16_t componentCount = 0;
    std::uint32_t payloadBytes = 0;
};

static_assert(sizeof(FilteredRecordHeader) == 12);

[[nodiscard]] std::uint64_t HashFilteredBytes(std::span<const std::byte> InBytes,
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

NetRollbackSession::Result NetRollbackSession::ConfigureRelevanceGrid(const RelevanceConfig& InConfig)
{
    auto ScopedGrid = std::make_unique<RelevanceGrid>();
    if (!ScopedGrid->Initialize(InConfig))
    {
        return Result::InvalidArgument;
    }
    MRelevance = std::move(ScopedGrid);
    if (MRegistry != nullptr)
    {
        MRelevance->Rebuild(*MRegistry, MEntities);
    }
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::SetRelevancePlayerPosition(NetPlayerId InPlayer,
                                                                          Vector2D InPosition) noexcept
{
    if (MRelevance == nullptr)
    {
        return Result::RelevanceNotConfigured;
    }
    if (!InPlayer.IsValid())
    {
        return Result::InvalidArgument;
    }
    MRelevance->SetPlayerPosition(InPlayer, InPosition);
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::RefreshRelevanceGrid() noexcept
{
    if (MRelevance == nullptr)
    {
        return Result::RelevanceNotConfigured;
    }
    if (MRegistry == nullptr)
    {
        return Result::NotInitialized;
    }
    MRelevance->Rebuild(*MRegistry, MEntities);
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::BuildRelevancePlan(NetPlayerId InPlayer, std::uint64_t InTick,
                                                                  RelevancePlan& OutPlan) const noexcept
{
    if (MRelevance == nullptr)
    {
        return Result::RelevanceNotConfigured;
    }
    if (!InPlayer.IsValid())
    {
        return Result::InvalidArgument;
    }
    if (!MRelevance->BuildPlan(InPlayer, InTick, OutPlan))
    {
        return Result::UnknownPlayer;
    }
    return Result::Ok;
}

NetRollbackSession::Result NetRollbackSession::FilterDeltaForPlayer(const RelevancePlan& InPlan,
                                                                    std::span<const std::byte> InDelta,
                                                                    std::span<std::byte> OutFiltered,
                                                                    std::size_t* OutBytes,
                                                                    bool InKeepRemovals) const noexcept
{
    if (OutFiltered.size() < sizeof(Ecs::SnapshotHeader))
    {
        return Result::BufferFull;
    }
    std::string_view ScopedError;
    const Ecs::SnapshotView ScopedView = Ecs::SnapshotView::Open(InDelta, &ScopedError);
    if (ScopedView.header.version == 0 || (ScopedView.header.flags & Ecs::SnapshotDelta) == 0)
    {
        return Result::InvalidArgument;
    }

    const std::size_t ScopedMinimum =
        sizeof(Ecs::SnapshotHeader) + static_cast<std::size_t>(ScopedView.header.extensionBytes);
    if (OutFiltered.size() < ScopedMinimum)
    {
        return Result::BufferFull;
    }

    std::byte* const ScopedOutputBase = OutFiltered.data() + sizeof(Ecs::SnapshotHeader);
    // The source payload size says nothing about the destination. All
    const std::size_t ScopedOutputPayloadCapacity = OutFiltered.size() - sizeof(Ecs::SnapshotHeader);
    std::size_t ScopedWriteOffset = 0;
    std::uint32_t ScopedRecordCount = 0;
    std::uint32_t ScopedComponentCount = 0;

    std::size_t ScopedReadOffset = 0;
    for (std::uint32_t ScopedRecordIndex = 0; ScopedRecordIndex < ScopedView.header.entityCount; ++ScopedRecordIndex)
    {
        if (ScopedReadOffset + sizeof(FilteredRecordHeader) > ScopedView.payload.size())
        {
            return Result::InvalidArgument;
        }
        FilteredRecordHeader ScopedRecord{};
        std::memcpy(&ScopedRecord, ScopedView.payload.data() + ScopedReadOffset, sizeof(ScopedRecord));
        const std::size_t ScopedRecordBytes = sizeof(FilteredRecordHeader) + ScopedRecord.payloadBytes;
        if (ScopedReadOffset + ScopedRecordBytes > ScopedView.payload.size())
        {
            return Result::InvalidArgument;
        }

        bool ScopedKeep = false;
        if ((ScopedRecord.flags & Ecs::SnapshotEntityRemoved) != 0)
        {
            // Removals are two words and prevent a client from keeping a ghost
            ScopedKeep = InKeepRemovals;
        }
        else
        {
            const NetEntityId ScopedNet = MEntities.IdOf(ScopedRecord.entity);
            ScopedKeep = InPlan.Contains(ScopedNet);
        }

        if (ScopedKeep)
        {
            if (ScopedWriteOffset > ScopedOutputPayloadCapacity ||
                ScopedRecordBytes > ScopedOutputPayloadCapacity - ScopedWriteOffset)
            {
                return Result::BufferFull;
            }
            std::memcpy(ScopedOutputBase + ScopedWriteOffset, ScopedView.payload.data() + ScopedReadOffset,
                        ScopedRecordBytes);
            ScopedWriteOffset += ScopedRecordBytes;
            ++ScopedRecordCount;
            ScopedComponentCount += ScopedRecord.componentCount;
        }
        ScopedReadOffset += ScopedRecordBytes;
    }

    if (ScopedReadOffset != ScopedView.payload.size())
    {
        return Result::InvalidArgument;
    }
    // The extension block is a second memcpy into the same destination.
    if (ScopedWriteOffset > ScopedOutputPayloadCapacity ||
        ScopedView.extension.size() > ScopedOutputPayloadCapacity - ScopedWriteOffset)
    {
        return Result::BufferFull;
    }

    Ecs::SnapshotHeader ScopedHeader = ScopedView.header;
    ScopedHeader.flags = Ecs::SnapshotDelta;
    ScopedHeader.entityCount = ScopedRecordCount;
    ScopedHeader.componentCount = ScopedComponentCount;
    ScopedHeader.payloadBytes = static_cast<std::uint32_t>(ScopedWriteOffset);
    ScopedHeader.extensionBytes = static_cast<std::uint32_t>(ScopedView.extension.size());
    ScopedHeader.extensionOffset = static_cast<std::uint32_t>(ScopedWriteOffset);
    // Keep baseChecksum/baseTick/schemaHash/tick/rng from the source delta: the
    ScopedHeader.checksum = HashFilteredBytes(std::span<const std::byte>(ScopedOutputBase, ScopedWriteOffset));
    if (!ScopedView.extension.empty())
    {
        std::memcpy(ScopedOutputBase + ScopedWriteOffset, ScopedView.extension.data(), ScopedView.extension.size());
    }
    ScopedHeader.extensionChecksum = HashFilteredBytes(ScopedView.extension);
    std::memcpy(OutFiltered.data(), &ScopedHeader, sizeof(ScopedHeader));
    if (OutBytes != nullptr)
    {
        *OutBytes = sizeof(Ecs::SnapshotHeader) + ScopedWriteOffset + ScopedView.extension.size();
    }
    return Result::Ok;
}
}
