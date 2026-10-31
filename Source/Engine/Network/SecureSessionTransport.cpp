// FemboyEngine2D - secure session transport implementation.
#include "Engine/Network/SecureSessionTransport.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Net
{

bool SecureSessionTransport::StartClient(NetTransport& InTransport, const NetAddress& InServer,
                                         const SecureChannelConfig& InConfig) noexcept
{
    Stop();
    MTransport = &InTransport;
    MServer = false;
    MConfig = InConfig;
    MConfig.Initiator = true;
    MServerAddress = InServer;

    Peer* ScopedPeer = AllocatePeer();
    if (ScopedPeer == nullptr)
    {
        return false;
    }
    ScopedPeer->Used = true;
    ScopedPeer->Authorized = true;
    ScopedPeer->Started = true;
    ScopedPeer->Owner = this;
    ScopedPeer->Address = InServer;
    ScopedPeer->Endpoint.SetSink(&PayloadThunk, ScopedPeer);
    if (MAuth.HasTicket)
    {
        // The handshake commits to the exact ticket bytes that AUTH carries.
        Crypto::Sha256(MAuth.SignedTicket, std::span<std::byte>(MConfig.Prologue.data(), Crypto::kSha256Bytes));
        MConfig.PrologueBytes = Crypto::kSha256Bytes;
        std::array<std::byte, kSecureAuthHeaderBytes + kTicketBytes> ScopedAuth{};
        const std::uint32_t ScopedMagic = kSecureAuthMagic;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < 4; ++ScopedIndex)
        {
            ScopedAuth[ScopedIndex] = static_cast<std::byte>((ScopedMagic >> (ScopedIndex * 8)) & 0xFFu);
        }
        ScopedAuth[4] = std::byte{1};
        ScopedAuth[5] = std::byte{0};
        std::memcpy(ScopedAuth.data() + kSecureAuthHeaderBytes, MAuth.SignedTicket.data(), kTicketBytes);
        (void)MTransport->Send(InServer, ScopedAuth, TransportChannel::Unreliable);
    }
    return ScopedPeer->Endpoint.Start(MTransport, InServer, MConfig) == SecureStatus::Ok;
}

bool SecureSessionTransport::StartServer(NetTransport& InTransport, const SecureChannelConfig& InConfig) noexcept
{
    Stop();
    MTransport = &InTransport;
    MServer = true;
    MConfig = InConfig;
    MConfig.Initiator = false;
    return true;
}

void SecureSessionTransport::Stop() noexcept
{
    for (Peer& ScopedPeer : MPeers)
    {
        ScopedPeer.Endpoint.Stop();
        ScopedPeer = Peer{};
    }
    for (RateEntry& ScopedRate : MRate)
    {
        ScopedRate = RateEntry{};
    }
    MHandshakeRateLimited = 0;
    MTimeSeconds = 0.0f;
    MTransport = nullptr;
    MServer = false;
    MServerAddress = NetAddress{};
    MSession = nullptr;
}

SecureSessionTransport::Peer* SecureSessionTransport::FindPeer(const NetAddress& InAddress) noexcept
{
    for (Peer& ScopedPeer : MPeers)
    {
        if (ScopedPeer.Used && ScopedPeer.Address == InAddress)
        {
            return &ScopedPeer;
        }
    }
    return nullptr;
}

SecureSessionTransport::Peer* SecureSessionTransport::AllocatePeer() noexcept
{
    for (Peer& ScopedPeer : MPeers)
    {
        if (!ScopedPeer.Used)
        {
            return &ScopedPeer;
        }
    }
    return nullptr;
}

void SecureSessionTransport::RawThunk(void* InUser, const NetAddress& InFrom,
                                      std::span<const std::byte> InPayload) noexcept
{
    auto* ScopedTransport = static_cast<SecureSessionTransport*>(InUser);
    if (ScopedTransport != nullptr)
    {
        ScopedTransport->Dispatch(InFrom, InPayload);
    }
}

void SecureSessionTransport::PayloadThunk(void* InUser, std::span<const std::byte> InPayload) noexcept
{
    auto* ScopedPeer = static_cast<Peer*>(InUser);
    if (ScopedPeer != nullptr && ScopedPeer->Owner != nullptr)
    {
        ScopedPeer->Owner->Deliver(*ScopedPeer, InPayload);
    }
}

void SecureSessionTransport::Deliver(Peer& InPeer, std::span<const std::byte> InPayload) noexcept
{
    if (MVoiceSink != nullptr && InPayload.size() >= 4)
    {
        const std::uint32_t ScopedMagic = static_cast<std::uint32_t>(InPayload[0]) |
                                          (static_cast<std::uint32_t>(InPayload[1]) << 8) |
                                          (static_cast<std::uint32_t>(InPayload[2]) << 16) |
                                          (static_cast<std::uint32_t>(InPayload[3]) << 24);
        if (ScopedMagic == 0x584F5646u)
        {
            MVoiceSink(MVoiceSinkUser, InPeer.Address, InPayload);
            return;
        }
    }
    if (MSession != nullptr)
    {
        MSession->ReceiveMessage(InPeer.Address, InPayload);
    }
}

void SecureSessionTransport::Dispatch(const NetAddress& InFrom,
                                      std::span<const std::byte> InPayload) noexcept
{
    if (MServer)
    {
        // Only pre-handshake traffic pays the token bucket; established peers
        // keep sending records and voice without extra cost.
        const Peer* ScopedKnown = FindPeer(InFrom);
        if ((ScopedKnown == nullptr || !ScopedKnown->Endpoint.Established()) && !AllowHandshake(InFrom))
        {
            return;
        }
    }
    Peer* ScopedPeer = FindPeer(InFrom);
    if (ScopedPeer == nullptr)
    {
        if (!MServer)
        {
            return;
        }
        ScopedPeer = AllocatePeer();
        if (ScopedPeer == nullptr)
        {
            return;
        }
        // The first datagram may be the AUTH packet, not the handshake.
        const bool ScopedAuthPacket = InPayload.size() == kSecureAuthHeaderBytes + kTicketBytes &&
                                      [&InPayload]() noexcept
        {
            const std::uint32_t ScopedMagic =
                static_cast<std::uint32_t>(InPayload[0]) | (static_cast<std::uint32_t>(InPayload[1]) << 8) |
                (static_cast<std::uint32_t>(InPayload[2]) << 16) | (static_cast<std::uint32_t>(InPayload[3]) << 24);
            return ScopedMagic == kSecureAuthMagic;
        }();
        ScopedPeer->Used = true;
        ScopedPeer->Owner = this;
        ScopedPeer->Address = InFrom;
        ScopedPeer->Endpoint.SetSink(&PayloadThunk, ScopedPeer);
        if (ScopedAuthPacket)
        {
            NetTicket ScopedTicket{};
            for (std::uint32_t ScopedIndex = 0; ScopedIndex < MRate.size(); ++ScopedIndex)
            {
                if (!MRate[ScopedIndex].Used)
                {
                    MRate[ScopedIndex].Used = true;
                    MRate[ScopedIndex].Address = InFrom;
                }
                if (MRate[ScopedIndex].Used && MRate[ScopedIndex].Address == InFrom)
                {
                    if (MRate[ScopedIndex].Failures >= MAuth.MaxAuthAttempts)
                    {
                        ScopedPeer->Used = false;
                        return;
                    }
                    break;
                }
            }
            const TicketStatus ScopedStatus = VerifyTicket(
                std::span<const std::byte>(InPayload.data() + kSecureAuthHeaderBytes, kTicketBytes),
                MAuth.LobbyPublic, MAuth.NowUnixSeconds, &ScopedTicket);
            const bool ScopedServerKey =
                std::memcmp(ScopedTicket.ServerPublicKey.data(), MConfig.LocalStatic.Public.data(),
                            Crypto::kX25519KeyBytes) == 0;
            if (ScopedStatus != TicketStatus::Ok || !ScopedServerKey)
            {
                for (RateEntry& ScopedRate : MRate)
                {
                    if (ScopedRate.Used && ScopedRate.Address == InFrom)
                    {
                        ++ScopedRate.Failures;
                    }
                }
                ScopedPeer->Used = false;
                return;
            }
            for (RateEntry& ScopedRate : MRate)
            {
                if (ScopedRate.Used && ScopedRate.Address == InFrom)
                {
                    ScopedRate.Failures = 0;
                }
            }
            Crypto::Sha256(std::span<const std::byte>(InPayload.data() + kSecureAuthHeaderBytes, kTicketBytes),
                           std::span<std::byte>(ScopedPeer->TicketHash));
            ScopedPeer->HasTicketHash = true;
            ScopedPeer->Authorized = true;
            return;
        }
        if (MAuth.Required && !ScopedPeer->Authorized)
        {
            ScopedPeer->Used = false;
            return;
        }
        ScopedPeer->Authorized = true;
        if (!StartPeerEndpoint(*ScopedPeer, InFrom))
        {
            return;
        }
    }
    if (MServer && ScopedPeer->Authorized && !ScopedPeer->Started)
    {
        if (!StartPeerEndpoint(*ScopedPeer, InFrom))
        {
            return;
        }
    }
    if (MServer && MAuth.Required && !ScopedPeer->Authorized)
    {
        return;
    }
    (void)ScopedPeer->Endpoint.HandleRawMessage(InFrom, InPayload);
}

bool SecureSessionTransport::StartPeerEndpoint(Peer& InPeer, const NetAddress& InAddress) noexcept
{
    if (MTransport == nullptr)
    {
        return false;
    }
    SecureChannelConfig ScopedConfig = MConfig;
    if (InPeer.HasTicketHash)
    {
        ScopedConfig.PrologueBytes = InPeer.TicketHash.size();
        std::memcpy(ScopedConfig.Prologue.data(), InPeer.TicketHash.data(), InPeer.TicketHash.size());
    }
    InPeer.Started = true;
    if (InPeer.Endpoint.Start(MTransport, InAddress, ScopedConfig) != SecureStatus::Ok)
    {
        InPeer.Used = false;
        return false;
    }
    return true;
}

void SecureSessionTransport::Poll(NetSession* InSession) noexcept
{
    if (MTransport == nullptr)
    {
        return;
    }
    MSession = InSession;
    (void)MTransport->Poll(&RawThunk, this);
    MSession = nullptr;
}

void SecureSessionTransport::SetHandshakeRateLimit(std::uint32_t InBurst, std::uint32_t InPerSecond) noexcept
{
    MHandshakeBurst = InBurst;
    MHandshakePerSecond = static_cast<float>(InPerSecond);
    for (RateEntry& ScopedRate : MRate)
    {
        ScopedRate.BucketStarted = false;
    }
}

void SecureSessionTransport::Tick(float InDeltaSeconds) noexcept
{
    MTimeSeconds += std::clamp(InDeltaSeconds, 0.0f, 10.0f);
}

bool SecureSessionTransport::AllowHandshake(const NetAddress& InAddress) noexcept
{
    if (MHandshakeBurst == 0)
    {
        return true;
    }
    RateEntry* ScopedEntry = nullptr;
    for (RateEntry& ScopedRate : MRate)
    {
        if (ScopedRate.Used && ScopedRate.Address == InAddress)
        {
            ScopedEntry = &ScopedRate;
            break;
        }
    }
    if (ScopedEntry == nullptr)
    {
        for (RateEntry& ScopedRate : MRate)
        {
            if (!ScopedRate.Used)
            {
                ScopedRate = RateEntry{};
                ScopedRate.Used = true;
                ScopedRate.Address = InAddress;
                ScopedEntry = &ScopedRate;
                break;
            }
        }
    }
    if (ScopedEntry == nullptr)
    {
        // The rate table is full: treat new addresses as limited instead of
        // evicting an existing bucket.
        ++MHandshakeRateLimited;
        return false;
    }
    if (!ScopedEntry->BucketStarted)
    {
        ScopedEntry->Tokens = static_cast<float>(MHandshakeBurst);
        ScopedEntry->LastRefillSeconds = MTimeSeconds;
        ScopedEntry->BucketStarted = true;
    }
    const float ScopedElapsed =
        MTimeSeconds > ScopedEntry->LastRefillSeconds ? MTimeSeconds - ScopedEntry->LastRefillSeconds : 0.0f;
    ScopedEntry->Tokens = std::min(static_cast<float>(MHandshakeBurst),
                                   ScopedEntry->Tokens + ScopedElapsed * MHandshakePerSecond);
    ScopedEntry->LastRefillSeconds = MTimeSeconds;
    if (ScopedEntry->Tokens < 1.0f)
    {
        ++MHandshakeRateLimited;
        return false;
    }
    ScopedEntry->Tokens -= 1.0f;
    return true;
}

bool SecureSessionTransport::SendThunk(void* InUser, std::uint8_t InToPlayer, const NetAddress& InTo,
                                       std::span<const std::byte> InBytes) noexcept
{
    (void)InToPlayer;
    auto* ScopedTransport = static_cast<SecureSessionTransport*>(InUser);
    if (ScopedTransport == nullptr || ScopedTransport->MTransport == nullptr)
    {
        return false;
    }
    Peer* ScopedPeer = ScopedTransport->FindPeer(InTo);
    if (ScopedPeer == nullptr && !ScopedTransport->MServer && ScopedTransport->MPeers[0].Used)
    {
        ScopedPeer = &ScopedTransport->MPeers[0];
    }
    if (ScopedPeer == nullptr || !ScopedPeer->Endpoint.Established())
    {
        return false;
    }
    return ScopedPeer->Endpoint.Send(InBytes) == SecureStatus::Ok;
}

bool SecureSessionTransport::ClientEstablished() const noexcept
{
    return !MServer && MPeers[0].Used && MPeers[0].Endpoint.Established();
}

std::uint32_t SecureSessionTransport::PeerCount() const noexcept
{
    std::uint32_t ScopedCount = 0;
    for (const Peer& ScopedPeer : MPeers)
    {
        ScopedCount += ScopedPeer.Used ? 1u : 0u;
    }
    return ScopedCount;
}
}
