// FemboyEngine2D - Play-In-Editor implementation.
#include "Engine/Core/Time.hpp"
#include "Editor/Infrastructure/PlayInEditor.hpp"

#include "Engine/Audio/Audio.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/CommandBuffer.hpp"
#include "Engine/ECS/RegistryOps.hpp"
#include "Engine/ECS/SystemGraph.hpp"
#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/ReplicationEcs.hpp"
#include "Engine/Network/Transport.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace Fby::Editor
{
namespace
{
// Launches the sandbox client without waiting for it: the session talks to it
// over UDP and terminates it on Stop. `InArguments` is a plain command line
// built from ports only, so no quoting is involved.
[[nodiscard]] void* LaunchSandboxProcess(const std::string& InExecutable, const std::string& InArguments) noexcept
{
#if defined(_WIN32)
    std::string ScopedCommandLine = "\"" + InExecutable + "\" " + InArguments;
    std::vector<char> ScopedCommand(ScopedCommandLine.begin(), ScopedCommandLine.end());
    ScopedCommand.push_back('\0');
    STARTUPINFOA ScopedStartup{};
    ScopedStartup.cb = sizeof(ScopedStartup);
    PROCESS_INFORMATION ScopedProcess{};
    if (!CreateProcessA(InExecutable.c_str(), ScopedCommand.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP,
                        nullptr, nullptr, &ScopedStartup, &ScopedProcess))
    {
        LogWarn("pie", "sandbox '{}' launch failed ({})", InExecutable, static_cast<unsigned long>(GetLastError()));
        return nullptr;
    }
    (void)CloseHandle(ScopedProcess.hThread);
    return ScopedProcess.hProcess;
#else
    std::vector<std::string> ScopedArguments;
    ScopedArguments.emplace_back(InExecutable);
    std::size_t ScopedBegin = 0;
    while (ScopedBegin < InArguments.size())
    {
        const std::size_t ScopedSpace = InArguments.find(' ', ScopedBegin);
        const std::size_t ScopedEnd = ScopedSpace == std::string::npos ? InArguments.size() : ScopedSpace;
        if (ScopedEnd > ScopedBegin)
        {
            ScopedArguments.push_back(InArguments.substr(ScopedBegin, ScopedEnd - ScopedBegin));
        }
        ScopedBegin = ScopedEnd + 1;
    }
    std::vector<char*> ScopedArgv;
    ScopedArgv.reserve(ScopedArguments.size() + 1);
    for (std::string& ScopedArgument : ScopedArguments)
    {
        ScopedArgv.push_back(ScopedArgument.data());
    }
    ScopedArgv.push_back(nullptr);

    pid_t ScopedPid = 0;
    const int ScopedResult =
        posix_spawn(&ScopedPid, InExecutable.c_str(), nullptr, nullptr, ScopedArgv.data(), environ);
    if (ScopedResult != 0)
    {
        LogWarn("pie", "sandbox '{}' launch failed ({})", InExecutable, ScopedResult);
        return nullptr;
    }
    return reinterpret_cast<void*>(static_cast<std::intptr_t>(ScopedPid));
#endif
}

void ReleaseSandboxProcess(void* InHandle) noexcept
{
    if (InHandle == nullptr)
    {
        return;
    }
#if defined(_WIN32)
    HANDLE ScopedProcess = static_cast<HANDLE>(InHandle);
    (void)TerminateProcess(ScopedProcess, 0);
    (void)WaitForSingleObject(ScopedProcess, 2000);
    (void)CloseHandle(ScopedProcess);
#else
    const auto ScopedPid = static_cast<pid_t>(reinterpret_cast<std::intptr_t>(InHandle));
    if (ScopedPid <= 0)
    {
        return;
    }
    (void)kill(ScopedPid, SIGTERM);
    for (int ScopedAttempt = 0; ScopedAttempt < 50; ++ScopedAttempt)
    {
        int ScopedStatus = 0;
        if (waitpid(ScopedPid, &ScopedStatus, WNOHANG) == ScopedPid)
        {
            return;
        }
        Core::SleepMilliseconds(10.0);
    }
    (void)kill(ScopedPid, SIGKILL);
    (void)waitpid(ScopedPid, nullptr, 0);
#endif
}
}

bool PlayInEditor::Initialize(std::span<std::byte> InArenaStorage, const Ecs::SnapshotSchema& InSchema,
                              std::size_t InMaxSnapshotBytes) noexcept
{
    if (InArenaStorage.empty())
    {
        MInitialized = false;
        return false;
    }
    MStorage = InArenaStorage;
    MArena.Initialize(InArenaStorage);
    MSchema = InSchema;
    MMaxSnapshotBytes =
        InMaxSnapshotBytes == 0 ? InArenaStorage.size() : std::min(InMaxSnapshotBytes, InArenaStorage.size());
    MInitialized = true;
    MActive = false;
    return true;
}

void PlayInEditor::Shutdown() noexcept
{
    Stop();
    if (MPiePhysics.IsValid())
    {
        (void)MPiePhysics.DestroyAllBodies(&MWorld);
        MPiePhysics.Shutdown();
    }
    MPhysicsOwned = false;
    MInitialized = false;
    MStorage = {};
    MArena = FrameArena{};
}

PlayInEditor::Services PlayInEditor::CaptureServices(const Ecs::SystemContext& InContext) noexcept
{
    Services ScopedServices;
    ScopedServices.jobs = &InContext.jobs;
    ScopedServices.renderer = InContext.renderer;
    ScopedServices.window = InContext.window;
    ScopedServices.assets = InContext.assets;
    ScopedServices.assetDb = InContext.assetDb;
    ScopedServices.audio = InContext.audio;
    ScopedServices.physics = InContext.physics;
    ScopedServices.commands = InContext.commands;
    ScopedServices.frameArena = InContext.frameArena;
    return ScopedServices;
}

PlayInEditor::Result PlayInEditor::Start(Ecs::Registry& InEditorWorld, const Ecs::SnapshotSchema& InSchema,
                                         const Ecs::SystemContext& InServices, std::uint64_t InTick,
                                         const DeterministicRng* InRng) noexcept
{
    if (!MInitialized)
    {
        return Result::NotInitialized;
    }
    if (MActive)
    {
        return Result::AlreadyPlaying;
    }
    const std::size_t ScopedRequired = Ecs::SnapshotSize(InEditorWorld, InSchema);
    if (ScopedRequired == 0 || ScopedRequired > MMaxSnapshotBytes)
    {
        return Result::SnapshotTooLarge;
    }

    // Isolate the physics world before the clone is materialized. The
    const PhysicsDesc ScopedPhysicsDesc =
        InServices.physics != nullptr ? InServices.physics->Description() : PhysicsDesc{};
    if (!MPiePhysics.Initialize(ScopedPhysicsDesc))
    {
        return Result::PhysicsFailed;
    }
    MPhysicsOwned = true;
    // Snapshot restores and scene clears release this world's bodies.
    MPiePhysics.BindToRegistry(MWorld);

    if (!Ecs::CaptureSnapshot(InEditorWorld, InSchema, MStorage.first(ScopedRequired), InTick,
                              InRng != nullptr ? *InRng : DeterministicRng::FromSeed(1), &MSnapshotBytes))
    {
        (void)MPiePhysics.DestroyAllBodies(&MWorld);
        MPiePhysics.Shutdown();
        MPhysicsOwned = false;
        return Result::CaptureFailed;
    }
    MSnapshot = MStorage.first(MSnapshotBytes);
    // Restore into a *separate* registry: the editor world stays untouched.
    if (!Ecs::RestoreSnapshot(MWorld, InSchema, MSnapshot, &MRng))
    {
        Scene::DestroyAllEntities(MWorld);
        (void)MPiePhysics.DestroyAllBodies(&MWorld);
        MPiePhysics.Shutdown();
        MPhysicsOwned = false;
        return Result::RestoreFailed;
    }
    MSchema = InSchema;
    MServices = CaptureServices(InServices);
    // Systems must use the PIE world, never the editor's PhysicsSystem.
    MServices.physics = &MPiePhysics;
    // The editor's CommandBuffer contains authoring transactions against the
    MServices.commands = nullptr;
    MActive = true;
    MTick = InTick;
    MElapsed = 0.0f;
    MFixedAccumulator = 0.0f;
    return Result::Ok;
}

PlayInEditor::Result PlayInEditor::StartMultiplayer(Ecs::Registry& InEditorWorld,
                                                   const Ecs::SnapshotSchema& InSchema,
                                                   const Ecs::SystemContext& InServices,
                                                   std::uint32_t InClientCount, std::uint64_t InTick,
                                                   const DeterministicRng* InRng,
                                                   const ExternalClientDesc* InExternal) noexcept
{
    const Result ScopedStart = Start(InEditorWorld, InSchema, InServices, InTick, InRng);
    if (ScopedStart != Result::Ok)
    {
        return ScopedStart;
    }
    Scene::DestroyAllEntities(MClientWorld);
    if (!Ecs::RestoreSnapshot(MClientWorld, InSchema, MSnapshot, nullptr))
    {
        Stop();
        return Result::RestoreFailed;
    }
    MClientDelta.assign(MMaxSnapshotBytes, std::byte{0});
    MClientCount = InClientCount == 0 ? 1 : InClientCount;
    MPieBaselineTick = InTick;

    // Loopback session: host authority and one client, deterministic transport.
    (void)MPieHostEndpoint.Open(&MPieNetwork, 41000);
    (void)MPieClientEndpoint.Open(&MPieNetwork, 41001);
    (void)MPieInputHostEndpoint.Open(&MPieNetwork, 41002);
    (void)MPieInputClientEndpoint.Open(&MPieNetwork, 41003);
    (void)MPieReplicationHostEndpoint.Open(&MPieNetwork, 41004);
    (void)MPieReplicationClientEndpoint.Open(&MPieNetwork, 41005);
    Net::NetSessionConfig ScopedHostConfig{};
    ScopedHostConfig.Mode = Net::NetSessionMode::Dedicated;
    ScopedHostConfig.Headless = true;
    std::memcpy(ScopedHostConfig.LocalName, "pie-host", 9);
    Net::NetSessionConfig ScopedClientConfig{};
    ScopedClientConfig.Mode = Net::NetSessionMode::Auto;
    ScopedClientConfig.Remote = MPieHostEndpoint.Address();
    std::memcpy(ScopedClientConfig.LocalName, "pie-client", 11);
    const Net::NetLink ScopedHostLink = MPieHostEndpoint.Link();
    const Net::NetLink ScopedClientLink = MPieClientEndpoint.Link();
    if (MPieHostSession.Start(ScopedHostLink, ScopedHostConfig) &&
        MPieClientSession.Start(ScopedClientLink, ScopedClientConfig))
    {
        for (std::uint32_t ScopedStep = 0; ScopedStep < 240; ++ScopedStep)
        {
            MPieNetwork.AdvanceTick();
            MPieHostSession.Pump();
            MPieClientSession.Pump();
            MPieHostSession.Advance(0.01f);
            MPieClientSession.Advance(0.01f);
            if (MPieClientSession.PlayerCount() > 0)
            {
                break;
            }
        }
        Net::SnapshotSenderConfig ScopedSenderConfig{};
        Net::SnapshotReceiverConfig ScopedReceiverConfig{};
        MPieNetworkUp =
            MPieSender.Start(MPieReplicationHostEndpoint.Link(), MPieReplicationClientEndpoint.Address(),
                             ScopedSenderConfig) &&
            MPieReceiver.Start(MPieReplicationClientEndpoint.Link(), MPieReplicationHostEndpoint.Address(),
                               ScopedReceiverConfig, &PlayInEditor::ApplyPieState, this);
        if (MPieNetworkUp)
        {
            MPieSender.Tick(0);
            if (!MPieSender.PublishBaseline(MPieBaselineTick, MSnapshot, 0))
            {
                LogError("pie", "replication baseline publish failed; the client cannot start from tick {}",
                         MPieBaselineTick);
                MPieNetworkUp = false;
            }
            for (std::uint32_t ScopedStep = 0; ScopedStep < 128 && !MPieReceiver.HasState(); ++ScopedStep)
            {
                MPieNetwork.AdvanceTick();
                MPieHostSession.Pump();
                MPieClientSession.Pump();
                MPieHostSession.Advance(0.005f);
                MPieClientSession.Advance(0.005f);
                MPieSender.Tick(ScopedStep);
                MPieReceiver.Tick(ScopedStep);
            }
        }
    }
    MMultiplayer = true;
    // Voice is best-effort: a failure must not reject a working PIE session.
    (void)StartVoice();
    if (InExternal != nullptr && !InExternal->executable.empty())
    {
        // A failed launch is a fallback, not a session failure: the local client
        // world keeps working and the editor reports the missing second copy.
        (void)StartExternalClient(*InExternal);
    }
    return Result::Ok;
}

bool PlayInEditor::StartExternalClient(const ExternalClientDesc& InDesc) noexcept
{
    std::string ScopedError;
    if (!Net::DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        LogWarn("pie", "PIE second copy disabled: {}", ScopedError);
        return false;
    }

    // The requested port pair first, then a few neighbours when the ports are
    // still bound by a previous session.
    for (std::uint32_t ScopedAttempt = 0; ScopedAttempt < 4; ++ScopedAttempt)
    {
        const std::uint32_t ScopedHostPortValue = static_cast<std::uint32_t>(InDesc.hostPort) + ScopedAttempt * 2;
        if (ScopedHostPortValue > 65000)
        {
            break;
        }
        const auto ScopedHostPort = static_cast<std::uint16_t>(ScopedHostPortValue);
        const auto ScopedClientPort = static_cast<std::uint16_t>(ScopedHostPortValue + 1);
        if (!MExternalTransport.Open(ScopedHostPort, &ScopedError))
        {
            continue;
        }
        const std::string ScopedArguments = "--pie-client --connect 127.0.0.1:" + std::to_string(ScopedHostPort) +
                                             " --listen " + std::to_string(ScopedClientPort);
        void* ScopedProcess = LaunchSandboxProcess(InDesc.executable, ScopedArguments);
        if (ScopedProcess == nullptr)
        {
            // Another port cannot fix a missing or unlaunchable executable.
            MExternalTransport.Close();
            break;
        }

        MExternalAddress.Host = 0x7F000001u;
        MExternalAddress.Port = ScopedClientPort;
        // The sandbox needs a moment for window and graphics startup, so the
        // first baseline keeps retransmitting longer than the session default.
        Net::SnapshotSenderConfig ScopedSenderConfig{};
        ScopedSenderConfig.MaxAttempts = 64;
        if (!MExternalSender.Start(Net::MakeTransportLink(&MExternalTransport), MExternalAddress, ScopedSenderConfig))
        {
            ReleaseSandboxProcess(ScopedProcess);
            MExternalTransport.Close();
            continue;
        }
        // The snapshot captured by Start() doubles as the first baseline: the
        // sandbox client expects it at the current tick. A state that does not
        // fit the replication budget cannot be fixed by another port.
        if (!MExternalSender.PublishBaseline(MTick, MSnapshot, 0))
        {
            LogWarn("pie", "PIE second copy baseline does not fit the replication budget ({} bytes)",
                    MSnapshot.size());
            MExternalSender.Stop();
            ReleaseSandboxProcess(ScopedProcess);
            MExternalTransport.Close();
            break;
        }

        MExternalBaselineTick = MTick;
        MExternalProcess = ScopedProcess;
        MExternalClientPort = ScopedClientPort;
        MExternalClient = true;
        MExternalNowMs = MTick * 1000;
        LogInfo("pie", "PIE second copy: '{}' on 127.0.0.1:{} (host UDP {})", InDesc.executable, ScopedClientPort,
                ScopedHostPort);
        return true;
    }

    LogWarn("pie", "PIE second copy unavailable; the session keeps the local client world only");
    return false;
}

void PlayInEditor::PumpExternalClient(std::span<const std::byte> InDelta, std::uint64_t InNowMs,
                                      float InDeltaSeconds) noexcept
{
    if (!MExternalClient)
    {
        return;
    }
    MExternalTransport.Tick(InDeltaSeconds);
    MExternalSender.Tick(InNowMs);

    bool ScopedRebaselined = false;
    if (MExternalSender.NeedsBaseline())
    {
        if (MExternalBaseline.size() < MMaxSnapshotBytes)
        {
            MExternalBaseline.resize(MMaxSnapshotBytes);
        }
        std::size_t ScopedBytes = 0;
        if (Ecs::CaptureSnapshot(MWorld, MSchema, MExternalBaseline, MTick, MRng, &ScopedBytes))
        {
            MExternalBaselineTick = MTick;
            ScopedRebaselined = MExternalSender.PublishBaseline(
                MTick, std::span<const std::byte>(MExternalBaseline.data(), ScopedBytes), InNowMs);
        }
    }
    // A baseline for this tick already carries the state; a delta for the same
    // tick would be one tick behind the receiver's expectation.
    if (!ScopedRebaselined && !InDelta.empty())
    {
        (void)MExternalSender.PublishDelta(MTick, MExternalBaselineTick, InDelta, InNowMs);
    }
}

void PlayInEditor::KeepExternalClientAlive(float InDeltaSeconds) noexcept
{
    if (!MExternalClient)
    {
        return;
    }
    MExternalKeepAliveMs += static_cast<std::uint64_t>(std::clamp(InDeltaSeconds, 0.0f, 0.25f) * 1000.0f);
    MExternalNowMs += static_cast<std::uint64_t>(std::clamp(InDeltaSeconds, 0.0f, 0.25f) * 1000.0f);
    const std::uint64_t ScopedNowMs = MExternalNowMs;
    MExternalTransport.Tick(InDeltaSeconds);
    MExternalSender.Tick(ScopedNowMs);
    if (MExternalKeepAliveMs < 500)
    {
        return;
    }
    MExternalKeepAliveMs = 0;
    // A tick the client already applied is ignored by its receiver, but the
    // datagram itself proves the host is still there, so a paused session does
    // not look like a stopped one.
    if (MExternalLastDeltaBytes > 0)
    {
        (void)MExternalSender.PublishDelta(
            MTick, MExternalBaselineTick,
            std::span<const std::byte>(MClientDelta.data(), MExternalLastDeltaBytes), ScopedNowMs);
    }
    else
    {
        (void)MExternalSender.PublishBaseline(MTick, MSnapshot, ScopedNowMs);
    }
}

void PlayInEditor::StopExternalClient() noexcept
{
    MExternalSender.Stop();
    MExternalTransport.Close();
    ReleaseSandboxProcess(MExternalProcess);
    MExternalProcess = nullptr;
    MExternalBaseline.clear();
    MExternalBaseline.shrink_to_fit();
    MExternalAddress = {};
    MExternalBaselineTick = 0;
    MExternalClientPort = 0;
    MExternalClient = false;
    MExternalNowMs = 0;
    MExternalKeepAliveMs = 0;
    MExternalLastDeltaBytes = 0;
}

bool PlayInEditor::StartVoice() noexcept
{
    if (MPieVoiceUp)
    {
        return true;
    }
    StopVoice();

    MPieVoiceHost = std::make_unique<VoiceSystem>();
    MPieVoiceClient = std::make_unique<VoiceSystem>();
    MPieVoiceClientGraph = std::make_unique<DspGraph>();

    // The host side mixes into the editor's real graph when audio exists, so a
    // decoded PIE client voice is audible. The simulated client stays headless
    // to avoid playing the host back into the same speakers.
    DspGraph* ScopedHostGraph = nullptr;
    FloatRing* ScopedHostReference = nullptr;
    if (MServices.audio != nullptr)
    {
        ScopedHostGraph = &MServices.audio->Mixer();
        ScopedHostReference = MServices.audio->ReferenceRing();
    }
    else
    {
        MPieVoiceHostGraph = std::make_unique<DspGraph>();
        if (!MPieVoiceHostGraph->Initialize(48000))
        {
            LogWarn("pie", "PIE voice unavailable: the host DSP graph did not initialize");
            StopVoice();
            return false;
        }
        MPieVoiceHostGraphOwned = true;
        ScopedHostGraph = MPieVoiceHostGraph.get();
    }
    if (!MPieVoiceClientGraph->Initialize(48000))
    {
        LogWarn("pie", "PIE voice unavailable: the client DSP graph did not initialize");
        StopVoice();
        return false;
    }

    (void)MPieVoiceHostEndpoint.Open(&MPieNetwork, 41006);
    (void)MPieVoiceClientEndpoint.Open(&MPieNetwork, 41007);

    VoiceSystemConfig ScopedHostConfig{};
    ScopedHostConfig.LocalPlayer = 0;
    ScopedHostConfig.Routing = VoiceRouting::Mesh;
    ScopedHostConfig.Denoise = VoiceDenoiseTier::Spectral;
    ScopedHostConfig.Aec.Enabled = true;
    VoiceSystemConfig ScopedClientConfig = ScopedHostConfig;
    ScopedClientConfig.LocalPlayer = 1;

    MPieVoiceHostSession.SetGraph(ScopedHostGraph);
    MPieVoiceClientSession.SetGraph(MPieVoiceClientGraph.get());

    if (!MPieVoiceHost->Start(*ScopedHostGraph, ScopedHostReference, ScopedHostConfig,
                              &VoiceNetworkBridge::SendThunk, &MPieVoiceHostBridge) ||
        !MPieVoiceClient->Start(*MPieVoiceClientGraph, nullptr, ScopedClientConfig,
                                &VoiceNetworkBridge::SendThunk, &MPieVoiceClientBridge) ||
        !MPieVoiceHostBridge.StartLoopback(*MPieVoiceHost, MPieHostSession, MPieVoiceHostSession, 0, 1,
                                           MPieVoiceHostEndpoint.Link(), MPieVoiceClientEndpoint.Address()) ||
        !MPieVoiceClientBridge.StartLoopback(*MPieVoiceClient, MPieClientSession, MPieVoiceClientSession, 1, 0,
                                             MPieVoiceClientEndpoint.Link(), MPieVoiceHostEndpoint.Address()))
    {
        LogWarn("pie", "PIE voice unavailable: the loopback voice link did not start");
        StopVoice();
        return false;
    }

    MPieVoiceUp = true;
    LogInfo("pie", "PIE voice: host player 0 <-> client player 1 on the PIE network");
    // The host is the microphone side: the shipped viewport must hear the
    // developer. A machine without an input device stays loopback-only.
    std::string ScopedDeviceError;
    MPieVoiceDevicesReady = MPieVoiceDevices.Initialize(&ScopedDeviceError);
    if (MPieVoiceDevicesReady)
    {
        const AudioDeviceList& ScopedInputs = MPieVoiceDevices.Devices(AudioDeviceKind::Input);
        if (ScopedInputs.Count > 0)
        {
            std::string ScopedCaptureError;
            if (!MPieVoiceHost->StartCapture(MPieVoiceDevices, ScopedInputs.Devices[0], &ScopedCaptureError))
            {
                LogWarn("pie", "PIE voice: microphone unavailable: {}", ScopedCaptureError);
            }
        }
        else
        {
            LogInfo("pie", "PIE voice: no input device; host runs loopback-only");
        }
    }
    else
    {
        LogWarn("pie", "PIE voice: device enumeration failed: {}", ScopedDeviceError);
    }
    return true;
}

void PlayInEditor::TickVoice(float InDeltaSeconds) noexcept
{
    if (!MPieVoiceUp)
    {
        return;
    }
    (void)MPieVoiceHostEndpoint.Link().Poll(&VoiceNetworkBridge::ReceiveThunk, &MPieVoiceHostBridge);
    (void)MPieVoiceClientEndpoint.Link().Poll(&VoiceNetworkBridge::ReceiveThunk, &MPieVoiceClientBridge);
    MPieVoiceHostBridge.Tick(InDeltaSeconds);
    MPieVoiceClientBridge.Tick(InDeltaSeconds);
}

void PlayInEditor::StopVoice() noexcept
{
    MPieVoiceHostBridge.Stop();
    MPieVoiceClientBridge.Stop();
    if (MPieVoiceHost)
    {
        MPieVoiceHost->Stop();
    }
    if (MPieVoiceClient)
    {
        MPieVoiceClient->Stop();
    }
    MPieVoiceHostEndpoint.Close();
    MPieVoiceClientEndpoint.Close();
    MPieVoiceDevicesReady = false;
    MPieVoiceHostSession = VoiceSession{};
    MPieVoiceClientSession = VoiceSession{};
    if (MPieVoiceHostGraphOwned && MPieVoiceHostGraph)
    {
        MPieVoiceHostGraph->Shutdown();
    }
    if (MPieVoiceClientGraph)
    {
        MPieVoiceClientGraph->Shutdown();
    }
    MPieVoiceHostGraphOwned = false;
    MPieVoiceHost.reset();
    MPieVoiceClient.reset();
    MPieVoiceHostGraph.reset();
    MPieVoiceClientGraph.reset();
    MPieVoiceUp = false;
}

bool PlayInEditor::StepMultiplayer(Ecs::SystemGraph& InGraph, float InDeltaSeconds, float InFixedDelta) noexcept
{
    if (!MMultiplayer || !Step(InGraph, InDeltaSeconds, InFixedDelta))
    {
        return false;
    }
    TickVoice(InDeltaSeconds);
    (void)MPieInputHostEndpoint.Link().Poll(&PlayInEditor::OnPieInput, this);

    std::size_t ScopedWritten = 0;
    if (!Ecs::CaptureDelta(MWorld, MSchema, MSnapshot, MClientDelta, MTick, MRng, &ScopedWritten))
    {
        return false;
    }
    const std::span<const std::byte> ScopedDelta(MClientDelta.data(), ScopedWritten);
    MExternalLastDeltaBytes = ScopedWritten;
    const std::uint64_t ScopedNowMs = MTick * 1000;
    bool ScopedApplied = false;
    if (!MPieNetworkUp)
    {
        ScopedApplied = Net::ApplyReplicationDeltaInPlace(MClientWorld, MSchema, ScopedDelta);
    }
    else
    {
        MPieSender.Tick(ScopedNowMs);
        const bool ScopedPublished = MPieSender.PublishDelta(MTick, MPieBaselineTick, ScopedDelta, ScopedNowMs);
        if (ScopedPublished)
        {
            for (std::uint32_t ScopedStep = 0; ScopedStep < 128 && MPieReceiver.AppliedTick() < MTick; ++ScopedStep)
            {
                MPieNetwork.AdvanceTick();
                MPieHostSession.Pump();
                MPieClientSession.Pump();
                MPieHostSession.Advance(0.005f);
                MPieClientSession.Advance(0.005f);
                MPieSender.Tick(ScopedNowMs + ScopedStep);
                MPieReceiver.Tick(ScopedNowMs + ScopedStep);
            }
            // Session events (PIE input) travel reliably and need a few extra polls.
            for (std::uint32_t ScopedStep = 0; ScopedStep < 32; ++ScopedStep)
            {
                MPieNetwork.AdvanceTick();
                MPieHostSession.Pump();
                MPieClientSession.Pump();
                MPieHostSession.Advance(0.005f);
                MPieClientSession.Advance(0.005f);
            }
            ScopedApplied = MPieReceiver.AppliedTick() >= MTick;
        }
    }
    // The second process receives the same delta independently of the local
    // loopback session, so a local replication stall never hides it.
    MExternalNowMs = std::max(MExternalNowMs, MTick * 1000);
    PumpExternalClient(ScopedDelta, MExternalNowMs, InDeltaSeconds);
    return ScopedApplied;
}

void PlayInEditor::Stop() noexcept
{
    StopVoice();
    StopExternalClient();
    if (!MActive)
    {
        return;
    }
    // The clone's bodies belong to the PIE world only. Destroy them before
    if (MPhysicsOwned)
    {
        (void)MPiePhysics.DestroyAllBodies(&MWorld);
        MPiePhysics.Shutdown();
        MPhysicsOwned = false;
    }
    MPieSender.Stop();
    MPieReceiver.Stop();
    MPieHostSession.Stop();
    MPieClientSession.Stop();
    MPieHostEndpoint.Close();
    MPieClientEndpoint.Close();
    MPieInputHostEndpoint.Close();
    MPieInputClientEndpoint.Close();
    MPieReplicationHostEndpoint.Close();
    MPieReplicationClientEndpoint.Close();
    MPieNetworkUp = false;
    MPieBaselineTick = 0;
    Scene::DestroyAllEntities(MWorld);
    Scene::DestroyAllEntities(MClientWorld);
    MClientDelta.clear();    MMultiplayer = false;
    MClientCount = 0;
    MActive = false;
    MSnapshot = {};
    MSnapshotBytes = 0;
}

Ecs::SystemContext PlayInEditor::MakeContext(Ecs::Registry& InRegistry) const noexcept
{
    Ecs::SystemContext ScopedContext{InRegistry, *MServices.jobs};
    ScopedContext.renderer = MServices.renderer;
    ScopedContext.window = MServices.window;
    ScopedContext.assets = MServices.assets;
    ScopedContext.assetDb = MServices.assetDb;
    ScopedContext.audio = MServices.audio;
    ScopedContext.physics = MServices.physics;
    ScopedContext.commands = MServices.commands;
    ScopedContext.frameArena = MServices.frameArena;
    return ScopedContext;
}

bool PlayInEditor::Step(Ecs::SystemGraph& InGraph, float InDeltaSeconds, float InFixedDelta) noexcept
{
    if (!MActive || MServices.jobs == nullptr)
    {
        return false;
    }
    Ecs::SystemContext ScopedContext = MakeContext(MWorld);
    ScopedContext.dt = InDeltaSeconds;
    ScopedContext.frameIndex = MTick + 1;
    ScopedContext.interpolationAlpha = 1.0f;
    InGraph.Run(Ecs::Phase::PreUpdate, ScopedContext);

    MFixedAccumulator += InDeltaSeconds;
    const float ScopedStep = InFixedDelta > 0.0f ? InFixedDelta : 1.0f / 60.0f;
    std::uint32_t ScopedIterations = 0;
    while (MFixedAccumulator >= ScopedStep && ScopedIterations < 8)
    {
        ScopedContext.dt = ScopedStep;
        InGraph.Run(Ecs::Phase::FixedUpdate, ScopedContext);
        MFixedAccumulator -= ScopedStep;
        ++ScopedIterations;
    }
    // Do not let a long editor hitch turn into a spiral of death.
    MFixedAccumulator = std::min(MFixedAccumulator, ScopedStep);

    ScopedContext.dt = InDeltaSeconds;
    ScopedContext.interpolationAlpha = MFixedAccumulator / ScopedStep;
    InGraph.Run(Ecs::Phase::PostUpdate, ScopedContext);
    if (MServices.commands != nullptr)
    {
        MServices.commands->Play(MWorld);
    }
    MTick += 1;
    MElapsed += InDeltaSeconds;
    return true;
}

std::size_t PlayInEditor::CaptureDelta(std::span<std::byte> Out) const noexcept
{
    if (!MActive || MSnapshot.empty() || Out.empty())
    {
        return 0;
    }
    std::size_t ScopedWritten = 0;
    if (!Ecs::CaptureDelta(MWorld, MSchema, MSnapshot, Out, MTick, MRng, &ScopedWritten))
    {
        return 0;
    }
    return ScopedWritten;
}

bool PlayInEditor::SendClientInput(const PiePlayerInput& InInput) noexcept
{
    if (!MPieNetworkUp || MPieClientSession.Status() != Net::NetSessionStatus::Joined)
    {
        return false;
    }
    // Input travels on its own reliable-enough loopback endpoint, so it never
    // competes with session messages or replication chunks.
    std::array<std::byte, 4 + sizeof(PiePlayerInput)> ScopedMessage{};
    ScopedMessage[0] = std::byte{'P'};
    ScopedMessage[1] = std::byte{'I'};
    ScopedMessage[2] = std::byte{'E'};
    ScopedMessage[3] = std::byte{'I'};
    std::memcpy(ScopedMessage.data() + 4, &InInput, sizeof(PiePlayerInput));
    return MPieInputClientEndpoint.Link().Send(MPieInputHostEndpoint.Address(),
                                               std::span<const std::byte>(ScopedMessage.data(),
                                                                          ScopedMessage.size()));
}

void PlayInEditor::OnPieInput(void* InUser, const Net::NetAddress& InFrom,
                             std::span<const std::byte> InMessage) noexcept
{
    auto* ScopedPie = static_cast<PlayInEditor*>(InUser);
    if (ScopedPie == nullptr || InMessage.size() != 4 + sizeof(PiePlayerInput) ||
        InMessage[0] != std::byte{'P'} || InMessage[1] != std::byte{'I'} ||
        InMessage[2] != std::byte{'E'} || InMessage[3] != std::byte{'I'})
    {
        return;
    }
    (void)InFrom;
    std::memcpy(&ScopedPie->MHostClientInput, InMessage.data() + 4, sizeof(PiePlayerInput));
    ScopedPie->MHasClientInput = true;
}

void PlayInEditor::ApplyPieState(void* InUser, std::uint64_t InTick, std::span<const std::byte> InState) noexcept
{
    auto* ScopedPie = static_cast<PlayInEditor*>(InUser);
    if (ScopedPie == nullptr || InState.empty())
    {
        return;
    }
    (void)InTick;
    if (!Net::ApplyReplicationDeltaInPlace(ScopedPie->MClientWorld, ScopedPie->MSchema, InState))
    {
        // The first published state is a full baseline; later ones are deltas.
        (void)Ecs::RestoreSnapshot(ScopedPie->MClientWorld, ScopedPie->MSchema, InState, nullptr);
    }
}
}
