// FemboyEngine2D - video stream component and system.
#pragma once

#include "Engine/Assets/AssetMeta.hpp"
#include "Engine/Video/VideoPlayer.hpp"
#include "Engine/Video/VideoTypes.hpp"

#include "Engine/ECS/Schedule.hpp"
#include "Engine/Core/Mirror.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fby
{
class VirtualFileSystem;

namespace Ecs
{
class SystemGraph;
}
namespace Assets
{
class AssetDatabase;
}
}

namespace Fby::Video
{

// The component. `player` was assigned by VideoSystem::Open().
struct VideoStreamComponent
{
    // Transactional metadata handle. The .femeta row owns the WebM source
    Fby::Assets::AssetHandle Media{};
    std::uint32_t Player = kInvalidVideoPlayer;
    std::uint32_t Texture = kInvalidVideoTexture;
    std::uint32_t Layer = 0;
    std::uint32_t Flags = 0;
    bool Loop = false;
    bool Paused = false;
    float Speed = 1.0f;
    // Owner token assigned by Open(); a stale token means the slot was reused.
    std::uint32_t StreamId = 0;
};

static_assert(std::is_trivially_copyable_v<VideoStreamComponent>, "a component must be trivially copyable");
static_assert(std::is_standard_layout_v<VideoStreamComponent>, "a component must be standard layout");
static_assert(sizeof(VideoStreamComponent) == 36, "VideoStreamComponent is ids/state only");

// Owns the fixed player-slot array. One instance per game session; the editor
class VideoSystem
{
public:
    VideoSystem() = default;
    ~VideoSystem();

    VideoSystem(const VideoSystem&) = delete;
    VideoSystem& operator=(const VideoSystem&) = delete;

    // Binds the device and the VFS.
    [[nodiscard]] bool Initialize(Rhi::RHI* InGpu, VirtualFileSystem* InVfs, const Vp9Backend* InBackend = nullptr,
                                  std::string* InError = nullptr);
    void Shutdown();

    // Test seam: injects a deterministic fake decoder table.
    void SetVp9Backend(const Vp9Backend* InBackend) noexcept
    {
        MBackend = InBackend;
    }
    // Tests drive PumpFrame()/Update() synchronously and do not want.
    void SetThreadingEnabled(bool InEnabled) noexcept
    {
        MThreadingEnabled = InEnabled;
    }
    [[nodiscard]] bool IsThreadingEnabled() const noexcept
    {
        return MThreadingEnabled;
    }

    // Reads `path` from the VFS, starts playback on a free player slot.
    [[nodiscard]] bool Open(VideoStreamComponent& InComponent, std::string_view InPath, std::string* InError = nullptr);
    // path: resolves the transactional .femeta handle to its source path.
    [[nodiscard]] bool OpenFromAsset(VideoStreamComponent& InComponent, Assets::AssetDatabase& InDatabase,
                                     std::string* InError = nullptr);
    // Same, over a caller-owned span.
    [[nodiscard]] bool OpenMemory(VideoStreamComponent& InComponent, std::span<const std::byte> InWebm,
                                  std::string* InError = nullptr);

    // Stops the player, releases its texture array and clears the component.
    bool Close(VideoStreamComponent& InComponent);

    // PostUpdate entry point: no allocation, no blocking, no GPU wait.
    void Update(Ecs::SystemContext& InContext);
    void Update(Ecs::Registry& InRegistry, float InDeltaSeconds);

    // Convenience for the Schedule-based sandbox/editor: registers Update.
    [[nodiscard]] static bool Register(Ecs::Schedule& InSchedule, VideoSystem& InSystem);
    // SystemGraph overload for the current scheduler.
    [[nodiscard]] static bool Register(Ecs::SystemGraph& InGraph, VideoSystem& InSystem);
    // Opt-in: close used slots whose component was not seen by the last Update
    // (the component was destroyed without Close). Off by default because a
    // preview component may live in a different registry.
    void SetPruneOrphans(bool InEnabled) noexcept
    {
        MPruneOrphans = InEnabled;
    }

    [[nodiscard]] VideoPlayer* Player(std::uint32_t InSlot) noexcept;
    [[nodiscard]] const VideoPlayer* Player(std::uint32_t InSlot) const noexcept;

    [[nodiscard]] std::uint32_t OpenPlayers() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept
    {
        return MInitialized;
    }

private:
    [[nodiscard]] bool OpenOnFreeSlot(VideoStreamComponent& InComponent, std::span<const std::byte> InWebm,
                                      std::string* InError, std::shared_ptr<const Fby::MappedFile> InOwner = nullptr);
    void SyncComponent(VideoStreamComponent& InComponent, const VideoPlayer& InPlayer) const;

    Rhi::RHI* MGpu = nullptr;
    VirtualFileSystem* MVfs = nullptr;
    const Vp9Backend* MBackend = nullptr;
    bool MThreadingEnabled = true;
    bool MInitialized = false;
    std::array<VideoPlayer, kMaxVideoPlayers> MPlayers{};
    std::array<bool, kMaxVideoPlayers> MSlotUsed{};
    std::array<std::uint32_t, kMaxVideoPlayers> MStreamIds{};
    std::array<bool, kMaxVideoPlayers> MSlotSeen{};
    std::uint32_t MNextStreamId = 0;
    bool MPruneOrphans = false;
    // Reused by Open()/OpenMemory() error paths only. The steady-state path
    std::string MErrorText{};
};
}

// Mirror registration
FBY_REFLECT_COMPONENT(Fby::Video::VideoStreamComponent, "VideoStreamComponent",
                      FBY_FIELD(Fby::Video::VideoStreamComponent, Player, UInt32),
                      FBY_FIELD(Fby::Video::VideoStreamComponent, Texture, UInt32),
                      FBY_FIELD(Fby::Video::VideoStreamComponent, Layer, UInt32),
                      FBY_FIELD(Fby::Video::VideoStreamComponent, Flags, UInt32),
                      FBY_FIELD(Fby::Video::VideoStreamComponent, Loop, Bool),
                      FBY_FIELD(Fby::Video::VideoStreamComponent, Paused, Bool),
                      FBY_FIELD(Fby::Video::VideoStreamComponent, Speed, Float),
                      FBY_FIELD(Fby::Video::VideoStreamComponent, StreamId, UInt32))
