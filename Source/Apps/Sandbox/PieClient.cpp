// FemboyEngine2D - sandbox PIE client world implementation.
#include "PieClient.hpp"

#include "Engine/Core/Log.hpp"
#include "Engine/ECS/RegistryOps.hpp"
#include "Engine/Network/ReplicationEcs.hpp"

namespace Fby::Sandbox
{

PieClient::~PieClient()
{
    Stop();
}

bool PieClient::Start(const PieClientConfig& InConfig, const Ecs::SnapshotSchema& InSchema, std::string* InError)
{
    Stop();

    if (InConfig.Host.Port == 0)
    {
        if (InError != nullptr)
        {
            *InError = "the PIE host port must be non-zero";
        }
        return false;
    }
    std::string ScopedError;
    if (!Net::DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        if (InError != nullptr)
        {
            *InError = ScopedError;
        }
        return false;
    }
    if (!MTransport.Open(InConfig.ListenPort, &ScopedError))
    {
        if (InError != nullptr)
        {
            *InError = ScopedError;
        }
        return false;
    }
    if (!MTransport.Connect(InConfig.Host))
    {
        MTransport.Close();
        if (InError != nullptr)
        {
            *InError = "the PIE host address was rejected by the transport";
        }
        return false;
    }

    MConfig = InConfig;
    MSchema = InSchema;
    if (!MReceiver.Start(Net::MakeTransportLink(&MTransport), InConfig.Host, InConfig.Receiver,
                         &PieClient::ApplyStateThunk, this))
    {
        MTransport.Close();
        if (InError != nullptr)
        {
            *InError = "the snapshot receiver rejected its configuration";
        }
        return false;
    }

    MStartMs = 0;
    MLastPacketMs = 0;
    MClockStarted = false;
    MTimedOut = false;
    MStopRequested = false;
    MRunning = true;
    LogInfo("pie_client", "client listening on UDP {}, host {}", MTransport.LocalPort(),
            Net::AddressToString(InConfig.Host));
    return true;
}

void PieClient::Stop() noexcept
{
    if (MRunning)
    {
        MReceiver.Stop();
        MTransport.Close();
    }
    // A caller may populate World() before Start(); teardown must still run.
    Scene::DestroyAllEntities(MWorld);
    MClockStarted = false;
    MRunning = false;
    MTimedOut = false;
    MStopRequested = false;
    MBaseline = {};
    MHasWorld.store(false, std::memory_order_relaxed);
    MApplyStatus.store(static_cast<std::uint8_t>(Net::ReplicationStatus::NotStarted),
                       std::memory_order_relaxed);
    MStartMs = 0;
    MLastPacketMs = 0;
}

void PieClient::RequestStop() noexcept
{
    MStopRequested = true;
}

void PieClient::Tick(std::uint64_t InNowMs, float InDeltaSeconds)
{
    if (!MRunning || MStopRequested || MTimedOut)
    {
        return;
    }
    if (!MClockStarted)
    {
        MStartMs = InNowMs;
        MLastPacketMs = InNowMs;
        MClockStarted = true;
    }

    const std::uint64_t ScopedPacketsBefore = MTransport.Stats().PacketsReceived;
    MTransport.Tick(InDeltaSeconds);
    MReceiver.Tick(InNowMs);
    // The host keeps a paused session alive by republishing its last state; the
    // receiver ignores an old tick, but the datagram still proves it is alive.
    if (MTransport.Stats().PacketsReceived > ScopedPacketsBefore)
    {
        MLastPacketMs = InNowMs;
    }

    if (MReceiver.HasState())
    {
        if (InNowMs > MLastPacketMs + MConfig.StateTimeoutMs)
        {
            MTimedOut = true;
            LogWarn("pie_client", "no datagram for {} ms; the PIE host stopped publishing", MConfig.StateTimeoutMs);
        }
        return;
    }
    if (InNowMs > MStartMs + MConfig.FirstStateTimeoutMs)
    {
        MTimedOut = true;
        LogWarn("pie_client", "no baseline within {} ms; the PIE host never answered", MConfig.FirstStateTimeoutMs);
    }
}

void PieClient::ApplyStateThunk(void* InUser, std::uint64_t InTick, std::span<const std::byte> InState) noexcept
{
    auto* ScopedClient = static_cast<PieClient*>(InUser);
    if (ScopedClient == nullptr || InState.empty())
    {
        return;
    }
    (void)InTick;
    // Classify the payload before touching the world. A full snapshot replaces
    // the world and becomes the baseline; a delta is accepted only against that
    // exact baseline. A malformed/rejected state must not fabricate entities.
    bool ScopedApplied = false;
    try
    {
        const Ecs::SnapshotView ScopedView = Ecs::SnapshotView::Open(InState);
        if (ScopedView.valid)
        {
            if ((ScopedView.header.flags & Ecs::SnapshotFull) != 0)
            {
                if (Ecs::ValidateSnapshot(InState, ScopedClient->MSchema, nullptr) &&
                    Ecs::RestoreSnapshot(ScopedClient->MWorld, ScopedClient->MSchema, InState, nullptr) &&
                    Ecs::RememberBaseline(ScopedClient->MBaseline, InState))
                {
                    ScopedApplied = true;
                }
            }
            else if ((ScopedView.header.flags & Ecs::SnapshotDelta) != 0 && ScopedClient->MBaseline.Valid)
            {
                ScopedApplied = Ecs::ApplyDeltaChecked(ScopedClient->MWorld, ScopedClient->MSchema, InState,
                                                       ScopedClient->MBaseline);
            }
        }
    }
    catch (...)
    {
        ScopedApplied = false;
    }
    if (ScopedApplied)
    {
        ScopedClient->MHasWorld.store(true, std::memory_order_relaxed);
        ScopedClient->MApplyStatus.store(static_cast<std::uint8_t>(Net::ReplicationStatus::Ok),
                                         std::memory_order_relaxed);
    }
    else
    {
        ScopedClient->MApplyFailures.fetch_add(1, std::memory_order_relaxed);
        ScopedClient->MApplyStatus.store(static_cast<std::uint8_t>(Net::ReplicationStatus::NotExpected),
                                         std::memory_order_relaxed);
    }
}
}
