// FemboyEngine2D - signed ticket binding to the secure handshake.
#include "Engine/Core/CryptoAsymmetric.hpp"
#include "Engine/Network/SecureSessionTransport.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)

template <std::size_t N>
[[nodiscard]] std::array<std::byte, N> Seed(std::uint8_t InValue)
{
    std::array<std::byte, N> ScopedSeed{};
    for (std::byte& ScopedByte : ScopedSeed)
    {
        ScopedByte = static_cast<std::byte>(InValue);
    }
    return ScopedSeed;
}

[[nodiscard]] std::array<std::byte, Fby::Crypto::kEddsaPublicBytes> LobbyPublic(const Fby::Crypto::EddsaKeyPair& InKey)
{
    std::array<std::byte, Fby::Crypto::kEddsaPublicBytes> ScopedPublic{};
    std::memcpy(ScopedPublic.data(), InKey.Public.data(), ScopedPublic.size());
    return ScopedPublic;
}

[[nodiscard]] std::array<std::byte, Fby::Crypto::kSha256Bytes> TicketHash(
    const std::array<std::byte, Fby::Net::kTicketBytes>& InTicket)
{
    std::array<std::byte, Fby::Crypto::kSha256Bytes> ScopedHash{};
    Fby::Crypto::Sha256(InTicket, std::span<std::byte>(ScopedHash));
    return ScopedHash;
}

// Channel-level binding check: both sides use fixed keys so only the prologue
// binding can make the transcript diverge.
[[nodiscard]] Fby::Net::SecureStatus RunBoundHandshake(const std::array<std::byte, 32>& InInitiatorBinding,
                                                       const std::array<std::byte, 32>& InResponderBinding)
{
    using namespace Fby;
    Crypto::X25519KeyPair ScopedInitiatorKey{};
    Crypto::X25519KeyPair ScopedResponderKey{};
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x41), &ScopedInitiatorKey);
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x42), &ScopedResponderKey);

    Net::SecureChannelConfig ScopedInitiatorConfig{};
    ScopedInitiatorConfig.LocalStatic = ScopedInitiatorKey;
    ScopedInitiatorConfig.Initiator = true;
    ScopedInitiatorConfig.SessionId = 7;
    ScopedInitiatorConfig.HasFixedEphemeral = true;
    ScopedInitiatorConfig.FixedEphemeralSeed = Seed<32>(0x43);
    ScopedInitiatorConfig.PrologueBytes = InInitiatorBinding.size();
    std::memcpy(ScopedInitiatorConfig.Prologue.data(), InInitiatorBinding.data(), InInitiatorBinding.size());

    Net::SecureChannelConfig ScopedResponderConfig{};
    ScopedResponderConfig.LocalStatic = ScopedResponderKey;
    ScopedResponderConfig.Initiator = false;
    ScopedResponderConfig.SessionId = 7;
    ScopedResponderConfig.HasFixedEphemeral = true;
    ScopedResponderConfig.FixedEphemeralSeed = Seed<32>(0x44);
    ScopedResponderConfig.PrologueBytes = InResponderBinding.size();
    std::memcpy(ScopedResponderConfig.Prologue.data(), InResponderBinding.data(), InResponderBinding.size());

    Net::SecureChannel ScopedInitiator;
    Net::SecureChannel ScopedResponder;
    if (ScopedInitiator.Start(ScopedInitiatorConfig) != Net::SecureStatus::Ok ||
        ScopedResponder.Start(ScopedResponderConfig) != Net::SecureStatus::Ok)
    {
        return Net::SecureStatus::InvalidConfig;
    }

    std::array<std::byte, Net::kSecureHandshakeBytes> ScopedMessage{};
    std::size_t ScopedBytes = 0;
    if (ScopedInitiator.WriteHandshake(ScopedMessage, &ScopedBytes) != Net::SecureStatus::Ok ||
        ScopedResponder.ReadHandshake(std::span<const std::byte>(ScopedMessage.data(), ScopedBytes)) !=
            Net::SecureStatus::Ok)
    {
        return Net::SecureStatus::BadSize;
    }
    if (ScopedResponder.WriteHandshake(ScopedMessage, &ScopedBytes) != Net::SecureStatus::Ok)
    {
        return Net::SecureStatus::BadSize;
    }
    const Net::SecureStatus ScopedStatus =
        ScopedInitiator.ReadHandshake(std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
    if (ScopedStatus != Net::SecureStatus::Ok)
    {
        return ScopedStatus;
    }
    if (ScopedInitiator.WriteHandshake(ScopedMessage, &ScopedBytes) != Net::SecureStatus::Ok)
    {
        return Net::SecureStatus::BadSize;
    }
    const Net::SecureStatus ScopedFinal =
        ScopedResponder.ReadHandshake(std::span<const std::byte>(ScopedMessage.data(), ScopedBytes));
    if (ScopedFinal != Net::SecureStatus::Ok)
    {
        return ScopedFinal;
    }
    return (ScopedInitiator.Established() && ScopedResponder.Established()) ? Net::SecureStatus::Ok
                                                                          : Net::SecureStatus::BadTag;
}
}

int main()
{
    using namespace Fby;
    using namespace Fby::Net;

    std::string ScopedError;
    if (!DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::printf("ticket_binding_test: sockets unavailable, skipped\n");
        return 0;
    }

    Crypto::X25519KeyPair ScopedServerKey{};
    Crypto::X25519KeyPair ScopedClientKey{};
    Crypto::EddsaKeyPair ScopedLobbyKey{};
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x51), &ScopedServerKey);
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x52), &ScopedClientKey);
    (void)Crypto::EddsaKeyPairFromSeed(Seed<32>(0x53), &ScopedLobbyKey);

    NetTransport ScopedServerTransport;
    FBY_CHECK(ScopedServerTransport.Open(0, &ScopedError));
    NetAddress ScopedServerAddress{};
    ScopedServerAddress.Host = 0x7F000001u;
    ScopedServerAddress.Port = ScopedServerTransport.LocalPort();

    const auto MakeTicket = [&](std::uint8_t InPlayer, const std::array<std::byte, 32>& InServerPublic,
                                std::array<std::byte, kTicketBytes>* OutSigned)
    {
        NetTicket ScopedTicket{};
        ScopedTicket.MatchId = 9;
        ScopedTicket.IssuedAt = 1000;
        ScopedTicket.ExpiresAt = 2000;
        ScopedTicket.Host = ScopedServerAddress.Host;
        ScopedTicket.Port = ScopedServerAddress.Port;
        ScopedTicket.ProtocolVersion = 1;
        ScopedTicket.PlayerId = InPlayer;
        ScopedTicket.Role = kTicketRoleClient;
        std::memcpy(ScopedTicket.ServerPublicKey.data(), InServerPublic.data(), Crypto::kX25519KeyBytes);
        std::memcpy(ScopedTicket.PlayerName, "binding", 7);
        return SignTicket(ScopedTicket, ScopedLobbyKey, *OutSigned);
    };

    std::array<std::byte, kTicketBytes> ScopedTicketA{};
    FBY_CHECK(MakeTicket(1, ScopedServerKey.Public, &ScopedTicketA));

    std::array<std::byte, 32> ScopedOtherServerKey{};
    for (std::byte& ScopedByte : ScopedOtherServerKey)
    {
        ScopedByte = std::byte{0x77};
    }
    std::array<std::byte, kTicketBytes> ScopedTicketForOtherServer{};
    FBY_CHECK(MakeTicket(2, ScopedOtherServerKey, &ScopedTicketForOtherServer));

    // 1. Different ticket hashes in the prologue make the handshake fail.
    {
        const auto ScopedHashA = TicketHash(ScopedTicketA);
        auto ScopedHashB = TicketHash(ScopedTicketForOtherServer);
        FBY_CHECK(ScopedHashA != ScopedHashB);
        FBY_CHECK(RunBoundHandshake(ScopedHashA, ScopedHashA) == SecureStatus::Ok);
        FBY_CHECK(RunBoundHandshake(ScopedHashA, ScopedHashB) == SecureStatus::BadTag);
    }

    // 2. The transport accepts a client whose AUTH ticket is signed for this
    // server, and binds that exact ticket into the handshake.
    SecureChannelConfig ScopedServerConfig{};
    ScopedServerConfig.LocalStatic = ScopedServerKey;
    ScopedServerConfig.Initiator = false;
    SecureSessionTransport ScopedServerLink;
    SecureSessionAuthConfig ScopedServerAuth{};
    ScopedServerAuth.Required = true;
    ScopedServerAuth.LobbyPublic = LobbyPublic(ScopedLobbyKey);
    ScopedServerAuth.NowUnixSeconds = 1500;
    ScopedServerLink.SetAuth(ScopedServerAuth);
    FBY_CHECK(ScopedServerLink.StartServer(ScopedServerTransport, ScopedServerConfig));

    NetTransport ScopedClientTransport;
    FBY_CHECK(ScopedClientTransport.Open(0, &ScopedError));
    FBY_CHECK(ScopedClientTransport.Connect(ScopedServerAddress));
    SecureChannelConfig ScopedClientConfig{};
    ScopedClientConfig.LocalStatic = ScopedClientKey;
    ScopedClientConfig.ExpectedRemoteStatic = ScopedServerKey.Public;
    ScopedClientConfig.RequireExpectedRemote = true;
    SecureSessionAuthConfig ScopedClientAuth{};
    ScopedClientAuth.HasTicket = true;
    ScopedClientAuth.SignedTicket = ScopedTicketA;
    SecureSessionTransport ScopedClientLink;
    ScopedClientLink.SetAuth(ScopedClientAuth);
    FBY_CHECK(ScopedClientLink.StartClient(ScopedClientTransport, ScopedServerAddress, ScopedClientConfig));

    for (std::uint32_t ScopedStep = 0; ScopedStep < 400 && !ScopedClientLink.ClientEstablished(); ++ScopedStep)
    {
        ScopedServerLink.Poll(nullptr);
        ScopedClientLink.Poll(nullptr);
        ScopedServerTransport.Tick(0.01f);
        ScopedClientTransport.Tick(0.01f);
        ScopedServerLink.Tick(0.01f);
        ScopedClientLink.Tick(0.01f);
    }
    FBY_CHECK(ScopedClientLink.ClientEstablished());
    FBY_CHECK(ScopedServerLink.PeerCount() == 1);

    // 3. A ticket for another server is rejected before the handshake starts.
    {
        NetTransport ScopedBadTransport;
        FBY_CHECK(ScopedBadTransport.Open(0, &ScopedError));
        FBY_CHECK(ScopedBadTransport.Connect(ScopedServerAddress));
        SecureSessionAuthConfig ScopedBadAuth{};
        ScopedBadAuth.HasTicket = true;
        ScopedBadAuth.SignedTicket = ScopedTicketForOtherServer;
        SecureSessionTransport ScopedBadLink;
        ScopedBadLink.SetAuth(ScopedBadAuth);
        FBY_CHECK(ScopedBadLink.StartClient(ScopedBadTransport, ScopedServerAddress, ScopedClientConfig));
        for (std::uint32_t ScopedStep = 0; ScopedStep < 120; ++ScopedStep)
        {
            ScopedServerLink.Poll(nullptr);
            ScopedBadLink.Poll(nullptr);
            ScopedServerTransport.Tick(0.01f);
            ScopedBadTransport.Tick(0.01f);
        }
        FBY_CHECK(!ScopedBadLink.ClientEstablished());
        FBY_CHECK(ScopedServerLink.PeerCount() == 1);
        ScopedBadLink.Stop();
        ScopedBadTransport.Close();
    }

    ScopedClientLink.Stop();
    ScopedClientTransport.Close();
    ScopedServerLink.Stop();
    ScopedServerTransport.Close();

    if (GFailures == 0)
    {
        std::printf("ticket_binding_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "ticket_binding_test: %u failure(s)\n", GFailures);
    return 1;
}
