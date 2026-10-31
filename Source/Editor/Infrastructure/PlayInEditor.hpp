// FemboyEngine2D - Play-In-Editor (PIE) scaffold.
#pragma once

#include "Engine/Core/DeterministicRng.hpp"
#include "Engine/Core/FrameArena.hpp"
#include "Engine/Audio/AudioDevices.hpp"
#include "Engine/Audio/VoiceNetworkBridge.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Engine/ECS/Snapshot.hpp"
#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/Replication.hpp"
#include "Engine/Network/Session.hpp"
#include "Engine/Physics/Physics.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <span>
#include <string>

namespace Fby::Editor
{

// One PIE player's local input sample. Buttons are game-defined bits; the move
// axis is normalized by the producer.
struct PiePlayerInput
{
    float MoveX = 0.0f;
    float MoveY = 0.0f;
    std::uint32_t Buttons = 0;
};

// Second-process PIE client. The editor launches the sandbox in its client mode
// and feeds it the same delta stream over loopback UDP. An empty executable or a
// failed launch is not an error: the session keeps the local client world.
struct ExternalClientDesc
{
    std::string executable;
    std::uint16_t hostPort = 41010;
    std::uint16_t clientPort = 41011;
};

class PlayInEditor
{
public:
    enum class Result : std::uint8_t
    {
        Ok = 0,
        AlreadyPlaying,
        NotPlaying,
        NotInitialized,
        SnapshotTooLarge,
        CaptureFailed,
        RestoreFailed,
        // The isolated PIE physics world could not be created.
        PhysicsFailed,
    };

    [[nodiscard]] static constexpr const char* ResultName(Result InResult) noexcept
    {
        switch (InResult)
        {
            case Result::Ok:
                return "Ok";
            case Result::AlreadyPlaying:
                return "AlreadyPlaying";
            case Result::NotPlaying:
                return "NotPlaying";
            case Result::NotInitialized:
                return "NotInitialized";
            case Result::SnapshotTooLarge:
                return "SnapshotTooLarge";
            case Result::CaptureFailed:
                return "CaptureFailed";
            case Result::RestoreFailed:
                return "RestoreFailed";
            case Result::PhysicsFailed:
                return "PhysicsFailed";
        }
        return "?";
    }

    // `arenaStorage` is caller owned (the editor's fixed PIE block). Initialize
    [[nodiscard]] bool Initialize(std::span<std::byte> InArenaStorage, const Ecs::SnapshotSchema& InSchema,
                                  std::size_t InMaxSnapshotBytes = 0) noexcept;
    void Shutdown() noexcept;

    // Serializes `editorWorld` and restores the clone. The editor world is never
    [[nodiscard]] Result Start(Ecs::Registry& InEditorWorld, const Ecs::SnapshotSchema& InSchema,
                               const Ecs::SystemContext& InServices, std::uint64_t InTick = 0,
                               const DeterministicRng* InRng = nullptr) noexcept;
    static void ApplyPieState(void* InUser, std::uint64_t InTick, std::span<const std::byte> InState) noexcept;
    void Stop() noexcept;

    // PIE multiplayer: a second, replicated world next to the host world. The
    // client world starts from the same snapshot and receives in-place deltas
    // after every host step. An optional sandbox process receives the same
    // stream over loopback UDP; the local client world stays the fallback.
    [[nodiscard]] Result StartMultiplayer(Ecs::Registry& InEditorWorld, const Ecs::SnapshotSchema& InSchema,
                                          const Ecs::SystemContext& InServices, std::uint32_t InClientCount = 1,
                                          std::uint64_t InTick = 0,
                                          const DeterministicRng* InRng = nullptr,
                                          const ExternalClientDesc* InExternal = nullptr) noexcept;
    [[nodiscard]] bool StepMultiplayer(Ecs::SystemGraph& InGraph, float InDeltaSeconds,
                                       float InFixedDelta = 1.0f / 60.0f) noexcept;
    [[nodiscard]] bool Multiplayer() const noexcept
    {
        return MMultiplayer;
    }
    // True when a second sandbox process is receiving the replicated stream.
    [[nodiscard]] bool ExternalClient() const noexcept
    {
        return MExternalClient;
    }
    [[nodiscard]] std::uint16_t ExternalClientPort() const noexcept
    {
        return MExternalClientPort;
    }
    // Keeps a paused session's second process alive: the last delta is
    // republished periodically so the sandbox keeps seeing host datagrams.
    void KeepExternalClientAlive(float InDeltaSeconds) noexcept;
    // PIE voice: an in-process loopback link between the local host player (0)
    // and the simulated client player (1). Voice is best-effort; a failed DSP
    // graph or endpoint leaves the session without voice.
    [[nodiscard]] bool VoiceActive() const noexcept
    {
        return MPieVoiceUp;
    }
    [[nodiscard]] VoiceSystem* VoiceHost() noexcept
    {
        return MPieVoiceHost.get();
    }
    [[nodiscard]] VoiceSystem* VoiceClient() noexcept
    {
        return MPieVoiceClient.get();
    }
    [[nodiscard]] VoiceSession& VoiceHostSession() noexcept
    {
        return MPieVoiceHostSession;
    }
    [[nodiscard]] VoiceSession& VoiceClientSession() noexcept
    {
        return MPieVoiceClientSession;
    }
    [[nodiscard]] Ecs::Registry& ClientWorld() noexcept
    {
        return MClientWorld;
    }
    [[nodiscard]] const Ecs::Registry& ClientWorld() const noexcept
    {
        return MClientWorld;
    }
    [[nodiscard]] std::uint32_t ClientCount() const noexcept
    {
        return MClientCount;
    }
    [[nodiscard]] Net::NetSession& HostSession() noexcept
    {
        return MPieHostSession;
    }
    // PIE input stream: the client sample goes to the host over the session event
    // channel; the host's copy is exposed for gameplay systems. It is applied to
    // components once gameplay input bindings exist.
    [[nodiscard]] bool SendClientInput(const PiePlayerInput& InInput) noexcept;
    [[nodiscard]] PiePlayerInput ClientInputOnHost() const noexcept
    {
        return MHostClientInput;
    }
    [[nodiscard]] bool HasClientInput() const noexcept
    {
        return MHasClientInput;
    }
    static void OnPieInput(void* InUser, const Net::NetAddress& InFrom,
                           std::span<const std::byte> InMessage) noexcept;

    [[nodiscard]] Net::NetSession& ClientSession() noexcept
    {
        return MPieClientSession;
    }

    // Advances the clone by one editor frame: PreUpdate, FixedUpdate (fixedDt
    [[nodiscard]] bool Step(Ecs::SystemGraph& InGraph, float InDeltaSeconds,
                            float InFixedDelta = 1.0f / 60.0f) noexcept;

    [[nodiscard]] bool Active() const noexcept
    {
        return MActive;
    }
    [[nodiscard]] Ecs::Registry& World() noexcept
    {
        return MWorld;
    }
    // Diagnostic seam for tests and the editor diagnostics panel: the private
    [[nodiscard]] PhysicsSystem& Physics() noexcept
    {
        return MPiePhysics;
    }
    [[nodiscard]] const PhysicsSystem& Physics() const noexcept
    {
        return MPiePhysics;
    }
    [[nodiscard]] std::uint64_t TickCount() const noexcept
    {
        return MTick;
    }
    [[nodiscard]] float ElapsedSeconds() const noexcept
    {
        return MElapsed;
    }
    [[nodiscard]] std::size_t SnapshotBytes() const noexcept
    {
        return MSnapshotBytes;
    }
    // Rollback/debug seam: capture a Snapshot v2 delta of the play world relative
    [[nodiscard]] std::size_t CaptureDelta(std::span<std::byte> Out) const noexcept;

private:
    // External services copied out of the editor's SystemContext. SystemContext
    struct Services
    {
        JobSystem* jobs = nullptr;
        Rhi::RHI* renderer = nullptr;
        WindowManager* window = nullptr;
        VirtualFileSystem* assets = nullptr;
        Assets::AssetDatabase* assetDb = nullptr;
        AudioSystem* audio = nullptr;
        PhysicsSystem* physics = nullptr;
        Ecs::CommandBuffer* commands = nullptr;
        FrameArena* frameArena = nullptr;
    };

    [[nodiscard]] static Services CaptureServices(const Ecs::SystemContext& InContext) noexcept;

private:
    [[nodiscard]] Ecs::SystemContext MakeContext(Ecs::Registry& InRegistry) const noexcept;

    // Launches the sandbox client and starts the loopback sender. Returns false
    // without changing the session when sockets, the process or the baseline
    // are unavailable.
    [[nodiscard]] bool StartExternalClient(const ExternalClientDesc& InDesc) noexcept;
    void PumpExternalClient(std::span<const std::byte> InDelta, std::uint64_t InNowMs, float InDeltaSeconds) noexcept;
    void StopExternalClient() noexcept;

    [[nodiscard]] bool StartVoice() noexcept;
    void TickVoice(float InDeltaSeconds) noexcept;
    void StopVoice() noexcept;

    Ecs::SnapshotSchema MSchema{};
    FrameArena MArena{};
    std::span<std::byte> MStorage{};
    entt::registry MWorld{};
    // The play world gets its own Box2D world. Sharing the editor's
    PhysicsSystem MPiePhysics{};
    bool MPhysicsOwned = false;
    Services MServices{};
    std::span<const std::byte> MSnapshot{};
    std::size_t MSnapshotBytes = 0;
    std::size_t MMaxSnapshotBytes = 0;
    std::uint64_t MTick = 0;
    // Local loopback session for PIE multiplayer: the host and client worlds
    // exchange deltas over the deterministic memory network.
    Net::MemoryNetwork MPieNetwork{};
    Net::MemoryEndpoint MPieHostEndpoint{};
    Net::MemoryEndpoint MPieClientEndpoint{};
    Net::MemoryEndpoint MPieInputHostEndpoint{};
    Net::MemoryEndpoint MPieInputClientEndpoint{};
    Net::MemoryEndpoint MPieReplicationHostEndpoint{};
    Net::MemoryEndpoint MPieReplicationClientEndpoint{};
    Net::NetSession MPieHostSession{};
    Net::NetSession MPieClientSession{};
    Net::SnapshotSender MPieSender{};
    Net::SnapshotReceiver MPieReceiver{};
    std::uint64_t MPieBaselineTick = 0;
    bool MPieNetworkUp = false;
    // PIE second copy: the sandbox process consumes the same delta stream over
    // loopback UDP. It never replaces the local client world.
    Net::NetTransport MExternalTransport{};
    Net::SnapshotSender MExternalSender{};
    Net::NetAddress MExternalAddress{};
    std::vector<std::byte> MExternalBaseline{};
    std::uint64_t MExternalBaselineTick = 0;
    std::uint16_t MExternalClientPort = 0;
    void* MExternalProcess = nullptr;
    bool MExternalClient = false;
    std::uint64_t MExternalNowMs = 0;
    std::uint64_t MExternalKeepAliveMs = 0;
    std::size_t MExternalLastDeltaBytes = 0;
    // PIE voice link: host player 0 and client player 1 share MPieNetwork
    // endpoints. The DSP graphs and voice runtimes are large (fixed voice and
    // jitter arrays), so they live on the heap and only while a PIE session runs.
    Net::MemoryEndpoint MPieVoiceHostEndpoint{};
    Net::MemoryEndpoint MPieVoiceClientEndpoint{};
    std::unique_ptr<DspGraph> MPieVoiceHostGraph;
    std::unique_ptr<DspGraph> MPieVoiceClientGraph;
    std::unique_ptr<VoiceSystem> MPieVoiceHost;
    std::unique_ptr<VoiceSystem> MPieVoiceClient;
    VoiceSession MPieVoiceHostSession{};
    VoiceSession MPieVoiceClientSession{};
    VoiceNetworkBridge MPieVoiceHostBridge{};
    VoiceNetworkBridge MPieVoiceClientBridge{};
    // PIE host microphone: the same device enumerator the voice panel uses.
    AudioDeviceEnumerator MPieVoiceDevices;
    bool MPieVoiceDevicesReady = false;
    bool MPieVoiceUp = false;
    bool MPieVoiceHostGraphOwned = false;
    PiePlayerInput MHostClientInput{};
    bool MHasClientInput = false;
    Ecs::Registry MClientWorld{};
    std::vector<std::byte> MClientDelta{};
    bool MMultiplayer = false;
    std::uint32_t MClientCount = 0;
    float MElapsed = 0.0f;
    float MFixedAccumulator = 0.0f;
    DeterministicRng MRng{};
    bool MActive = false;
    bool MInitialized = false;
};
}
