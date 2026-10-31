// FemboyEngine2D - datagram link abstraction with UDP and in-memory backends.
#pragma once

#include "Engine/Network/Transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby::Net
{

using LinkMessageFn = void (*)(void* InUser, const NetAddress& InFrom,
                               std::span<const std::byte> InMessage) noexcept;

// One send/poll pair. The backend is selected at construction; the session only
// ever sees this value type, so UDP and the deterministic loopback share code.
struct NetLink
{
    using SendFn = bool (*)(void* InSelf, const NetAddress& InTo, std::span<const std::byte> InMessage) noexcept;
    using PollFn = std::uint32_t (*)(void* InSelf, LinkMessageFn InFn, void* InUser) noexcept;

    void* Self = nullptr;
    SendFn SendCall = nullptr;
    PollFn PollCall = nullptr;

    [[nodiscard]] bool Valid() const noexcept
    {
        return Self != nullptr && SendCall != nullptr && PollCall != nullptr;
    }

    [[nodiscard]] bool Send(const NetAddress& InTo, std::span<const std::byte> InMessage) const noexcept
    {
        return Valid() && SendCall(Self, InTo, InMessage);
    }

    [[nodiscard]] std::uint32_t Poll(LinkMessageFn InFn, void* InUser) const noexcept
    {
        return Valid() ? PollCall(Self, InFn, InUser) : 0;
    }
};

[[nodiscard]] NetLink MakeTransportLink(NetTransport* InTransport) noexcept;

class MemoryEndpoint;

[[nodiscard]] NetLink MakeMemoryLink(MemoryEndpoint* InEndpoint) noexcept;

// Deterministic single-process network for tests, offline play and PIE. Messages
// are routed by address with a fixed queue; SetDropEvery simulates loss without
// sockets or threads.
class MemoryNetwork
{
public:
    static constexpr std::uint32_t kMaxEndpoints = 8;
    // Deep enough for one 16 KiB baseline burst (48 chunks) without dropping.
    static constexpr std::uint32_t kQueueDepth = 64;
    static constexpr std::size_t kMaxMessageBytes = 512;

    MemoryNetwork() = default;

    [[nodiscard]] NetAddress Open(std::uint16_t InPort) noexcept;
    void Close(const NetAddress& InAddress) noexcept;
    // 0 disables loss; N drops every Nth routed message.
    void SetDropEvery(std::uint32_t InEvery) noexcept
    {
        MDropEvery = InEvery;
    }
    // Delivery delay in AdvanceTick steps, so tests can model latency without
    // threads or wall-clock sleeps.
    void SetLatencyTicks(std::uint32_t InTicks) noexcept
    {
        MLatencyTicks = InTicks;
    }
    void AdvanceTick() noexcept
    {
        ++MTick;
    }

protected:
    friend class MemoryEndpoint;

    struct Frame
    {
        NetAddress From{};
        std::uint16_t Bytes = 0;
        std::uint64_t ReleaseTick = 0;
        std::array<std::byte, kMaxMessageBytes> Data{};
    };

    struct Endpoint
    {
        NetAddress Address{};
        bool Used = false;
        std::uint32_t Count = 0;
        std::uint32_t Head = 0;
        std::array<Frame, kQueueDepth> Queue{};
    };

    [[nodiscard]] Endpoint* Find(const NetAddress& InAddress) noexcept;
    [[nodiscard]] bool Route(const NetAddress& InFrom, const NetAddress& InTo,
                             std::span<const std::byte> InMessage) noexcept;
    [[nodiscard]] bool Pop(const NetAddress& InAddress, Frame* OutFrame) noexcept;

    std::array<Endpoint, kMaxEndpoints> MEndpoints{};
    std::uint32_t MDropEvery = 0;
    std::uint32_t MDropCounter = 0;
    std::uint32_t MLatencyTicks = 0;
    std::uint64_t MTick = 0;
};

class MemoryEndpoint
{
public:
    MemoryEndpoint() = default;

    [[nodiscard]] bool Open(MemoryNetwork* InNetwork, std::uint16_t InPort) noexcept;
    void Close() noexcept;
    [[nodiscard]] NetAddress Address() const noexcept
    {
        return MAddress;
    }
    [[nodiscard]] NetLink Link() noexcept;

protected:
    static bool SendThunk(void* InSelf, const NetAddress& InTo,
                          std::span<const std::byte> InMessage) noexcept;
    static std::uint32_t PollThunk(void* InSelf, LinkMessageFn InFn, void* InUser) noexcept;

    MemoryNetwork* MNetwork = nullptr;
    NetAddress MAddress{};
};
}
