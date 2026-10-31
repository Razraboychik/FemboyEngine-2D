// FemboyEngine2D - secure session endpoint over the UDP transport.
#pragma once

#include "Engine/Network/SecureChannel.hpp"
#include "Engine/Network/Transport.hpp"

#include <cstddef>
#include <span>

namespace Fby::Net
{

// Drives one SecureChannel over one NetTransport peer: handshake messages use
// the reliable channel, application records use the unreliable one. All state
// is fixed-size; Pump invokes the sink for every decrypted record.
class SecureEndpoint
{
public:
    using PayloadFn = void (*)(void* InUser, std::span<const std::byte> InPayload);

    SecureEndpoint() = default;

    SecureStatus Start(NetTransport* InTransport, const NetAddress& InRemote,
                       const SecureChannelConfig& InConfig) noexcept;
    void Stop() noexcept;

    [[nodiscard]] bool Established() const noexcept
    {
        return MChannel.Established();
    }
    [[nodiscard]] const SecureChannel& Channel() const noexcept
    {
        return MChannel;
    }
    [[nodiscard]] SecureStatus LastStatus() const noexcept
    {
        return MLastStatus;
    }

    // Seals one payload and sends it to the remote peer.
    SecureStatus Send(std::span<const std::byte> InPayload) noexcept;

    // Drains the transport, advances the handshake and delivers every decrypted
    // record to `InFn`. Returns the first protocol error, or Ok.
    SecureStatus Pump(PayloadFn InFn, void* InUser) noexcept;

    // Persistent sink used when the caller dispatches datagrams itself (one
    // poll for many endpoints).
    void SetSink(PayloadFn InFn, void* InUser) noexcept
    {
        MSinkFn = InFn;
        MSinkUser = InUser;
    }
    // Feeds one already-received datagram to this endpoint. The caller has
    // already routed it by remote address.
    SecureStatus HandleRawMessage(const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;

protected:
    static void OnMessage(void* InUser, const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;
    SecureStatus SendHandshake() noexcept;
    SecureStatus HandleMessage(std::span<const std::byte> InMessage) noexcept;

    NetTransport* MTransport = nullptr;
    NetAddress MRemote{};
    SecureChannel MChannel{};
    SecureStatus MLastStatus = SecureStatus::NotStarted;
    SecureStatus MFirstError = SecureStatus::Ok;
    PayloadFn MPumpFn = nullptr;
    void* MPumpUser = nullptr;
    PayloadFn MSinkFn = nullptr;
    void* MSinkUser = nullptr;
    bool MStarted = false;
};
}
