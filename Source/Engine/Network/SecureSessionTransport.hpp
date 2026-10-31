// FemboyEngine2D - secure session transport: one encrypted link per player.
#pragma once

#include "Engine/Core/Sha256.hpp"
#include "Engine/Network/SecureEndpoint.hpp"
#include "Engine/Network/Session.hpp"
#include "Engine/Network/Ticket.hpp"
#include "Engine/Network/Transport.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace Fby::Net
{

inline constexpr std::uint32_t kSecureSessionMaxPeers = 8;

inline constexpr std::uint32_t kSecureAuthMagic = 0x41594246u;
inline constexpr std::size_t kSecureAuthHeaderBytes = 6;

// Server-side gate: the client must present a lobby-signed ticket before the
// Noise handshake is accepted. The client side carries its own signed ticket.
struct SecureSessionAuthConfig
{
    bool Required = false;
    std::array<std::byte, Crypto::kEddsaPublicBytes> LobbyPublic{};
    std::uint64_t NowUnixSeconds = 0;
    bool HasTicket = false;
    std::array<std::byte, kTicketBytes> SignedTicket{};
    // Per-address cap on failed AUTH packets before the address is ignored.
    std::uint32_t MaxAuthAttempts = 8;
};

// Bridges NetSession to SecureEndpoint. The server keeps one endpoint per client
// address; the client keeps one endpoint for the server. Every session datagram
// is sealed here, so NetSession itself never sees plaintext on the wire.
class SecureSessionTransport
{
public:
    SecureSessionTransport() = default;

    SecureSessionTransport(const SecureSessionTransport&) = delete;
    SecureSessionTransport& operator=(const SecureSessionTransport&) = delete;

    void SetAuth(const SecureSessionAuthConfig& InAuth) noexcept
    {
        MAuth = InAuth;
    }

    [[nodiscard]] bool StartClient(NetTransport& InTransport, const NetAddress& InServer,
                                   const SecureChannelConfig& InConfig) noexcept;
    [[nodiscard]] bool StartServer(NetTransport& InTransport, const SecureChannelConfig& InConfig) noexcept;
    void Stop() noexcept;

    // Drains the transport and feeds decrypted payloads into the session.
    void Poll(NetSession* InSession = nullptr) noexcept;

    using VoiceSinkFn = void (*)(void* InUser, const NetAddress& InFrom,
                                 std::span<const std::byte> InPayload) noexcept;
    // Voice datagrams (magic FVOX) are demultiplexed here instead of reaching the
    // session message handler.
    void SetVoiceSink(VoiceSinkFn InFn, void* InUser) noexcept
    {
        MVoiceSink = InFn;
        MVoiceSinkUser = InUser;
    }

    // Per-address token bucket for every pre-handshake datagram(AUTH, cookies and
    // Noise messages). A burst of 0 disables the limit. The bucket refills only
    // through Tick(); callers poll once per frame.
    void SetHandshakeRateLimit(std::uint32_t InBurst, std::uint32_t InPerSecond) noexcept;
    void Tick(float InDeltaSeconds) noexcept;
    [[nodiscard]] std::uint64_t HandshakeRateLimited() const noexcept
    {
        return MHandshakeRateLimited;
    }

    [[nodiscard]] NetSession::MessageSendFn SendHook() const noexcept
    {
        return &SendThunk;
    }
    [[nodiscard]] bool ClientEstablished() const noexcept;
    [[nodiscard]] std::uint32_t PeerCount() const noexcept;

protected:
    struct Peer
    {
        bool Used = false;
        bool Authorized = false;
        bool Started = false;
        SecureSessionTransport* Owner = nullptr;
        NetAddress Address{};
        SecureEndpoint Endpoint{};
        // SHA-256 of the ticket presented in AUTH. The same value is mixed into
        // the handshake prologue, so the transcript commits to those bytes.
        std::array<std::byte, Crypto::kSha256Bytes> TicketHash{};
        bool HasTicketHash = false;
    };

    static bool SendThunk(void* InUser, std::uint8_t InToPlayer, const NetAddress& InTo,
                          std::span<const std::byte> InBytes) noexcept;
    static void RawThunk(void* InUser, const NetAddress& InFrom,
                         std::span<const std::byte> InPayload) noexcept;
    static void PayloadThunk(void* InUser, std::span<const std::byte> InPayload) noexcept;
    void Dispatch(const NetAddress& InFrom, std::span<const std::byte> InPayload) noexcept;
    void Deliver(Peer& InPeer, std::span<const std::byte> InPayload) noexcept;
    // Starts the endpoint with the per-peer ticket binding, if one was verified.
    [[nodiscard]] bool StartPeerEndpoint(Peer& InPeer, const NetAddress& InAddress) noexcept;
    [[nodiscard]] bool AllowHandshake(const NetAddress& InAddress) noexcept;
    [[nodiscard]] Peer* FindPeer(const NetAddress& InAddress) noexcept;
    [[nodiscard]] Peer* AllocatePeer() noexcept;

    NetTransport* MTransport = nullptr;
    bool MServer = false;
    SecureChannelConfig MConfig{};
    NetAddress MServerAddress{};
    std::array<Peer, kSecureSessionMaxPeers> MPeers{};
    NetSession* MSession = nullptr;
    struct RateEntry
    {
        bool Used = false;
        NetAddress Address{};
        std::uint32_t Failures = 0;
        float Tokens = 0.0f;
        float LastRefillSeconds = 0.0f;
        bool BucketStarted = false;
    };
    std::array<RateEntry, kSecureSessionMaxPeers> MRate{};
    std::uint32_t MHandshakeBurst = 32;
    float MHandshakePerSecond = 8.0f;
    float MTimeSeconds = 0.0f;
    std::uint64_t MHandshakeRateLimited = 0;
    SecureSessionAuthConfig MAuth{};
    VoiceSinkFn MVoiceSink = nullptr;
    void* MVoiceSinkUser = nullptr;
};
}
