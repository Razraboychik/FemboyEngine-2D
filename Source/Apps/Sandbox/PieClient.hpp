// FemboyEngine2D - sandbox PIE client world.
#pragma once

#include "Engine/ECS/Snapshot.hpp"
#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/Replication.hpp"
#include "Engine/Network/Transport.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace Fby::Sandbox
{

struct PieClientConfig
{
    // PIE host address. Snapshots arrive from here and acknowledgements go back.
    Net::NetAddress Host{};
    // Local receive port; 0 asks the operating system for an ephemeral port.
    std::uint16_t ListenPort = 0;
    // A client that never receives a baseline gives up after this long.
    std::uint32_t FirstStateTimeoutMs = 10000;
    // Once the first state is applied, this much silence means the host stopped.
    std::uint32_t StateTimeoutMs = 2000;
    Net::SnapshotReceiverConfig Receiver{};
};

// The replicated client world of a PIE second copy. The sandbox owns this
// object: the receiver assembles snapshots, the registry holds the world and the
// timeout turns "the host stopped publishing" into a clean exit. No window or
// GPU is involved, so tests drive the same path headlessly.
class PieClient
{
public:
    PieClient() = default;
    ~PieClient();

    PieClient(const PieClient&) = delete;
    PieClient& operator=(const PieClient&) = delete;

    [[nodiscard]] bool Start(const PieClientConfig& InConfig, const Ecs::SnapshotSchema& InSchema,
                             std::string* InError = nullptr);
    void Stop() noexcept;

    // Pulls datagrams and applies every complete state. `InNowMs` is a
    // monotonic millisecond clock that drives the timeout; `InDeltaSeconds`
    // feeds the transport's peer timers.
    void Tick(std::uint64_t InNowMs, float InDeltaSeconds);
    void RequestStop() noexcept;

    [[nodiscard]] bool Running() const noexcept
    {
        return MRunning;
    }
    [[nodiscard]] bool HasState() const noexcept
    {
        return MReceiver.HasState();
    }
    [[nodiscard]] bool TimedOut() const noexcept
    {
        return MTimedOut;
    }
    [[nodiscard]] bool ShouldQuit() const noexcept
    {
        return MStopRequested || MTimedOut;
    }
    [[nodiscard]] std::uint64_t AppliedTick() const noexcept
    {
        return MReceiver.AppliedTick();
    }
    [[nodiscard]] Net::ReplicationStatus Status() const noexcept
    {
        return MReceiver.LastStatus();
    }
    [[nodiscard]] std::uint16_t ListenPort() const noexcept
    {
        return MTransport.LocalPort();
    }
    [[nodiscard]] Ecs::Registry& World() noexcept
    {
        return MWorld;
    }
    [[nodiscard]] const Ecs::Registry& World() const noexcept
    {
        return MWorld;
    }

private:
    static void ApplyStateThunk(void* InUser, std::uint64_t InTick, std::span<const std::byte> InState) noexcept;

    Net::NetTransport MTransport{};
    Net::SnapshotReceiver MReceiver{};
    Ecs::SnapshotSchema MSchema{};
    Ecs::Registry MWorld{};
    PieClientConfig MConfig{};
    std::uint64_t MStartMs = 0;
    std::uint64_t MLastPacketMs = 0;
    bool MClockStarted = false;
    bool MRunning = false;
    bool MTimedOut = false;
    bool MStopRequested = false;
};
}
