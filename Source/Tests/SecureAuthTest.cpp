// FemboyEngine2D - lobby ticket gate for the secure session transport tests.
#include "Engine/Network/SecureSessionTransport.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
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
}

int main()
{
    using namespace Fby;
    using namespace Fby::Net;

    std::string ScopedError;
    if (!DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::printf("secure_auth_test: sockets unavailable, skipped\n");
        return 0;
    }

    NetTransport ScopedServerTransport;
    if (!ScopedServerTransport.Open(0, &ScopedError))
    {
        std::printf("secure_auth_test: bind failed, skipped\n");
        return 0;
    }
    NetAddress ScopedServerAddress{};
    ScopedServerAddress.Host = 0x7F000001u;
    ScopedServerAddress.Port = ScopedServerTransport.LocalPort();

    Crypto::X25519KeyPair ScopedServerKey{};
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x71), &ScopedServerKey);
    Crypto::EddsaKeyPair ScopedLobbyKey{};
    (void)Crypto::EddsaKeyPairFromSeed(Seed<32>(0x72), &ScopedLobbyKey);

    NetTicket ScopedTicket{};
    ScopedTicket.MatchId = 1;
    ScopedTicket.IssuedAt = 1000;
    ScopedTicket.ExpiresAt = 2000;
    ScopedTicket.Host = ScopedServerAddress.Host;
    ScopedTicket.Port = ScopedServerAddress.Port;
    ScopedTicket.ProtocolVersion = 1;
    ScopedTicket.PlayerId = 5;
    ScopedTicket.Role = kTicketRoleClient;
    std::memcpy(ScopedTicket.ServerPublicKey.data(), ScopedServerKey.Public.data(), Crypto::kX25519KeyBytes);
    std::memcpy(ScopedTicket.PlayerName, "auth", 5);
    std::array<std::byte, kTicketBytes> ScopedSigned{};
    FBY_CHECK(SignTicket(ScopedTicket, ScopedLobbyKey, ScopedSigned));

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
    Crypto::X25519KeyPair ScopedClientKey{};
    (void)Crypto::X25519KeyPairFromSeed(Seed<32>(0x73), &ScopedClientKey);
    SecureChannelConfig ScopedClientConfig{};
    ScopedClientConfig.LocalStatic = ScopedClientKey;
    ScopedClientConfig.ExpectedRemoteStatic = ScopedServerKey.Public;
    ScopedClientConfig.RequireExpectedRemote = true;
    SecureSessionAuthConfig ScopedClientAuth{};
    ScopedClientAuth.HasTicket = true;
    ScopedClientAuth.SignedTicket = ScopedSigned;
    SecureSessionTransport ScopedClientLink;
    ScopedClientLink.SetAuth(ScopedClientAuth);
    FBY_CHECK(ScopedClientLink.StartClient(ScopedClientTransport, ScopedServerAddress, ScopedClientConfig));

    for (std::uint32_t ScopedStep = 0; ScopedStep < 400 && !ScopedClientLink.ClientEstablished(); ++ScopedStep)
    {
        ScopedServerLink.Poll(nullptr);
        ScopedClientLink.Poll(nullptr);
        ScopedServerTransport.Tick(0.01f);
        ScopedClientTransport.Tick(0.01f);
    }
    FBY_CHECK(ScopedClientLink.ClientEstablished());
    FBY_CHECK(ScopedServerLink.PeerCount() == 1);

    // A tampered signature must be rejected before the handshake is accepted.
    NetTransport ScopedBadTransport;
    FBY_CHECK(ScopedBadTransport.Open(0, &ScopedError));
    FBY_CHECK(ScopedBadTransport.Connect(ScopedServerAddress));
    SecureSessionAuthConfig ScopedBadAuth = ScopedClientAuth;
    ScopedBadAuth.SignedTicket[kTicketBytes - 1] ^= std::byte{0x01};
    SecureSessionTransport ScopedBadLink;
    ScopedBadLink.SetAuth(ScopedBadAuth);
    FBY_CHECK(ScopedBadLink.StartClient(ScopedBadTransport, ScopedServerAddress, ScopedClientConfig));
    for (std::uint32_t ScopedStep = 0; ScopedStep < 200; ++ScopedStep)
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
    ScopedClientLink.Stop();
    ScopedClientTransport.Close();
    ScopedServerLink.Stop();
    ScopedServerTransport.Close();

    if (GFailures == 0)
    {
        std::printf("secure_auth_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "secure_auth_test: %u failure(s)\n", GFailures);
    return 1;
}
