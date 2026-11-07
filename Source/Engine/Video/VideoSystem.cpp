// FemboyEngine2D - VideoSystem: the fixed player-slot array and the
#include "Engine/Video/VideoStream.hpp"

#include "Engine/Assets/AssetDatabase.hpp"
#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/RHI/RHI.hpp"

#include <string>
#include <utility>

namespace Fby::Video
{
namespace
{

void SetError(std::string* InError, std::string InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::move(InMessage);
    }
}

// Trampoline for the engine's allocation-free Schedule (function pointer plus
void Tick(Ecs::SystemContext& InContext, void* InUser)
{
    static_cast<VideoSystem*>(InUser)->Update(InContext);
}
}

VideoSystem::~VideoSystem()
{
    Shutdown();
}

bool VideoSystem::Initialize(Rhi::RHI* InGpu, VirtualFileSystem* InVfs, const Vp9Backend* InBackend,
                             std::string* InError)
{
    Shutdown();
    (void)InError;
    MGpu = InGpu;
    MVfs = InVfs;
    MBackend = InBackend;
    if (MBackend == nullptr)
    {
        MBackend = LibvpxBackend();
        if (MBackend == nullptr)
        {
            // Not fatal: a headless editor or a test injects a backend later,
            LogWarn("video", "libvpx (vpx.dll / libvpx.so) not found; VideoSystem opens without a VP9 decoder");
        }
    }
    MInitialized = true;
    LogInfo("video", "VideoSystem initialized ({} player slots, backend '{}')", kMaxVideoPlayers,
            MBackend != nullptr && MBackend->name != nullptr ? MBackend->name : "none");
    return true;
}

void VideoSystem::Shutdown()
{
    for (std::uint32_t ScopedSlot = 0; ScopedSlot < kMaxVideoPlayers; ++ScopedSlot)
    {
        MPlayers[ScopedSlot].Close();
        MSlotUsed[ScopedSlot] = false;
    }
    MGpu = nullptr;
    MVfs = nullptr;
    MBackend = nullptr;
    MInitialized = false;
}

bool VideoSystem::OpenFromAsset(VideoStreamComponent& InComponent, Assets::AssetDatabase& InDatabase,
                                std::string* InError)
{
    const std::string_view ScopedSource = InDatabase.SourcePath(InComponent.Media);
    if (ScopedSource.empty())
    {
        SetError(InError, "video component has no .femeta asset handle (or the handle is stale)");
        return false;
    }
    return Open(InComponent, ScopedSource, InError);
}

bool VideoSystem::Open(VideoStreamComponent& InComponent, std::string_view InPath, std::string* InError)
{
    if (!MInitialized || MVfs == nullptr)
    {
        SetError(InError, "VideoSystem is not initialized with a VirtualFileSystem");
        return false;
    }
    // Pin() is the owning read: if the asset watcher reloads this clip while the
    const auto ScopedPinned = MVfs->Pin(InPath);
    if (!ScopedPinned)
    {
        SetError(InError, "video stream not found in the virtual file system: " + std::string(InPath));
        return false;
    }
    return OpenOnFreeSlot(InComponent, ScopedPinned->bytes, InError, ScopedPinned->mapping);
}

bool VideoSystem::OpenMemory(VideoStreamComponent& InComponent, std::span<const std::byte> InWebm, std::string* InError)
{
    if (!MInitialized)
    {
        SetError(InError, "VideoSystem is not initialized");
        return false;
    }
    return OpenOnFreeSlot(InComponent, InWebm, InError);
}

bool VideoSystem::OpenOnFreeSlot(VideoStreamComponent& InComponent, std::span<const std::byte> InWebm,
                                 std::string* InError, std::shared_ptr<const Fby::MappedFile> InOwner)
{
    if (MBackend == nullptr)
    {
        SetError(InError, "no VP9 decoder backend: install libvpx or inject a Vp9Backend (tests)");
        return false;
    }
    // Re-opening a component that already owns a slot replaces the old stream;
    if (InComponent.Player < kMaxVideoPlayers && MSlotUsed[InComponent.Player])
    {
        (void)Close(InComponent);
    }

    std::uint32_t ScopedSlot = kMaxVideoPlayers;
    for (std::uint32_t ScopedCandidate = 0; ScopedCandidate < kMaxVideoPlayers; ++ScopedCandidate)
    {
        if (!MSlotUsed[ScopedCandidate])
        {
            ScopedSlot = ScopedCandidate;
            break;
        }
    }
    if (ScopedSlot >= kMaxVideoPlayers)
    {
        SetError(InError, "no free video player slot (VideoSystem owns " + std::to_string(kMaxVideoPlayers) + ")");
        return false;
    }

    VideoPlayer& ScopedPlayer = MPlayers[ScopedSlot];
    ScopedPlayer.SetRenderer(MGpu);
    if (!ScopedPlayer.Open(InWebm, MBackend, InError, std::move(InOwner)))
    {
        ScopedPlayer.Close();
        return false;
    }
    MSlotUsed[ScopedSlot] = true;
    InComponent.Player = ScopedSlot;
    InComponent.StreamId = ++MNextStreamId;
    MStreamIds[ScopedSlot] = InComponent.StreamId;
    if (MThreadingEnabled)
    {
        ScopedPlayer.StartDecoderThread();
    }
    SyncComponent(InComponent, ScopedPlayer);
    return true;
}

bool VideoSystem::Close(VideoStreamComponent& InComponent)
{
    const std::uint32_t ScopedSlot = InComponent.Player;
    if (ScopedSlot >= kMaxVideoPlayers || !MSlotUsed[ScopedSlot])
    {
        InComponent = VideoStreamComponent{};
        return false;
    }
    MPlayers[ScopedSlot].Close();
    MSlotUsed[ScopedSlot] = false;
    MStreamIds[ScopedSlot] = 0;
    const bool ScopedLoop = InComponent.Loop;
    const bool ScopedPaused = InComponent.Paused;
    const float ScopedSpeed = InComponent.Speed;
    InComponent = VideoStreamComponent{};
    // Playback switches are authored data, not decoder state: a later Open()
    InComponent.Loop = ScopedLoop;
    InComponent.Paused = ScopedPaused;
    InComponent.Speed = ScopedSpeed;
    return true;
}

void VideoSystem::Update(Ecs::SystemContext& InContext)
{
    Update(InContext.registry, InContext.dt);
}

void VideoSystem::Update(Ecs::Registry& InRegistry, float InDeltaSeconds)
{
    if (!MInitialized)
    {
        return;
    }
    MSlotSeen.fill(false);
    auto ScopedView = InRegistry.view<VideoStreamComponent>();
    for (const entt::entity ScopedEntity : ScopedView)
    {
        VideoStreamComponent& ScopedComponent = ScopedView.get<VideoStreamComponent>(ScopedEntity);
        const std::uint32_t ScopedSlot = ScopedComponent.Player;
        // A stale StreamId means the slot was reused by another component; the
        // two streams must not share a decoder.
        if (ScopedSlot >= kMaxVideoPlayers || !MSlotUsed[ScopedSlot] ||
            MStreamIds[ScopedSlot] != ScopedComponent.StreamId)
        {
            ScopedComponent.Texture = kInvalidVideoTexture;
            ScopedComponent.Flags |= kVideoFlagError;
            continue;
        }
        MSlotSeen[ScopedSlot] = true;
        VideoPlayer& ScopedPlayer = MPlayers[ScopedSlot];
        ScopedPlayer.Advance(InDeltaSeconds, ScopedComponent.Speed, ScopedComponent.Paused, ScopedComponent.Loop);
        SyncComponent(ScopedComponent, ScopedPlayer);
    }
    if (MPruneOrphans)
    {
        for (std::uint32_t ScopedSlot = 0; ScopedSlot < kMaxVideoPlayers; ++ScopedSlot)
        {
            if (MSlotUsed[ScopedSlot] && !MSlotSeen[ScopedSlot])
            {
                MPlayers[ScopedSlot].Close();
                MSlotUsed[ScopedSlot] = false;
                MStreamIds[ScopedSlot] = 0;
            }
        }
    }
}

void VideoSystem::SyncComponent(VideoStreamComponent& InComponent, const VideoPlayer& InPlayer) const
{
    InComponent.Texture = InPlayer.TextureId();
    InComponent.Layer = InPlayer.CurrentLayer();
    // The player owns the video bits only; app-authored flags survive the sync.
    InComponent.Flags = (InComponent.Flags & ~kVideoFlagMask) | (InPlayer.StateFlags() & kVideoFlagMask);
}

bool VideoSystem::Register(Ecs::Schedule& InSchedule, VideoSystem& InSystem)
{
    return InSchedule.Add(Ecs::Phase::PostUpdate, "VideoSystem", &Tick, &InSystem);
}

namespace
{
void VideoCoreTick(const Ecs::CoreSystemView& InView, void* InUser)
{
    auto* ScopedVideo = static_cast<VideoSystem*>(InUser);
    if (ScopedVideo != nullptr)
    {
        ScopedVideo->Update(InView.registry, InView.dt);
    }
}
}

bool VideoSystem::Register(Ecs::SystemGraph& InGraph, VideoSystem& InSystem)
{
    return InGraph.Add(Ecs::Phase::PostUpdate, "VideoSystem", Ecs::BindCoreSystem<&VideoCoreTick>(), &InSystem);
}

VideoPlayer* VideoSystem::Player(std::uint32_t InSlot) noexcept
{
    return InSlot < kMaxVideoPlayers ? &MPlayers[InSlot] : nullptr;
}

const VideoPlayer* VideoSystem::Player(std::uint32_t InSlot) const noexcept
{
    return InSlot < kMaxVideoPlayers ? &MPlayers[InSlot] : nullptr;
}

std::uint32_t VideoSystem::OpenPlayers() const noexcept
{
    std::uint32_t ScopedCount = 0;
    for (const bool ScopedUsed : MSlotUsed)
    {
        ScopedCount += ScopedUsed ? 1u : 0u;
    }
    return ScopedCount;
}
}
