// FemboyEngine2D - lobby protocol tests: rooms, tickets and match start.
#include "Engine/Network/Lobby.hpp"
#include "Engine/Network/NetLink.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

[[nodiscard]] Fby::Crypto::EddsaKeyPair MakeIssuer()
{
    std::array<std::byte, 32> ScopedSeed{};
    ScopedSeed.fill(std::byte{0x51});
    Fby::Crypto::EddsaKeyPair ScopedIssuer{};
    (void)Fby::Crypto::EddsaKeyPairFromSeed(ScopedSeed, &ScopedIssuer);
    return ScopedIssuer;
}
}

int main()
{
    using namespace Fby::Net;

    MemoryNetwork ScopedNetwork;
    MemoryEndpoint ScopedLobbyEndpoint;
    MemoryEndpoint ScopedAlphaEndpoint;
    MemoryEndpoint ScopedBravoEndpoint;
    FBY_CHECK(ScopedLobbyEndpoint.Open(&ScopedNetwork, 0));
    FBY_CHECK(ScopedAlphaEndpoint.Open(&ScopedNetwork, 0));
    FBY_CHECK(ScopedBravoEndpoint.Open(&ScopedNetwork, 0));

    const Fby::Crypto::EddsaKeyPair ScopedIssuer = MakeIssuer();

    LobbyServerConfig ScopedServerConfig{};
    ScopedServerConfig.GameServer.Host = 0x7F000001u;
    ScopedServerConfig.GameServer.Port = 9999;
    ScopedServerConfig.GameServerPublicKey.fill(std::byte{0x33});
    ScopedServerConfig.TicketLifetimeSeconds = 300;
    std::strncpy(ScopedServerConfig.GameName, "FemboyEngine2D", sizeof(ScopedServerConfig.GameName) - 1);

    LobbyServer ScopedServer;
    FBY_CHECK(ScopedServer.Start(ScopedLobbyEndpoint.Link(), ScopedIssuer, ScopedServerConfig));

    NetLobbyClientConfig ScopedAlphaConfig{};
    ScopedAlphaConfig.LobbyPublicKey = ScopedIssuer.Public;
    std::strncpy(ScopedAlphaConfig.PlayerName, "Alpha", sizeof(ScopedAlphaConfig.PlayerName) - 1);
    NetLobbyClientConfig ScopedBravoConfig = ScopedAlphaConfig;
    std::strncpy(ScopedBravoConfig.PlayerName, "Bravo", sizeof(ScopedBravoConfig.PlayerName) - 1);

    NetLobbyClient ScopedAlpha;
    NetLobbyClient ScopedBravo;
    FBY_CHECK(ScopedAlpha.Start(ScopedAlphaEndpoint.Link(), ScopedLobbyEndpoint.Address(), ScopedAlphaConfig));
    FBY_CHECK(ScopedBravo.Start(ScopedBravoEndpoint.Link(), ScopedLobbyEndpoint.Address(), ScopedBravoConfig));

    FBY_CHECK(ScopedAlpha.CreateRoom("Room A"));
    ScopedServer.Tick(1000);
    ScopedAlpha.Tick(1000);
    FBY_CHECK(ScopedAlpha.Ready());
    FBY_CHECK(ScopedAlpha.Error() == LobbyError::None);
    FBY_CHECK(ScopedAlpha.PlayerId() == 0);
    FBY_CHECK(ScopedAlpha.Server().Port == 9999);
    FBY_CHECK(ScopedAlpha.Ticket().MatchId == ScopedAlpha.RoomId());
    FBY_CHECK(ScopedAlpha.Ticket().Role == kTicketRoleHost);
    FBY_CHECK(ScopedServer.RoomCount() == 1);

    NetTicket ScopedVerified{};
    FBY_CHECK(VerifyTicket(ScopedAlpha.SignedTicket(), ScopedIssuer.Public, 1000, &ScopedVerified) ==
              TicketStatus::Ok);
    FBY_CHECK(ScopedVerified.Host == 0x7F000001u);
    FBY_CHECK(ScopedVerified.ServerPublicKey == ScopedServerConfig.GameServerPublicKey);

    const std::uint32_t ScopedRoomId = ScopedAlpha.RoomId();
    FBY_CHECK(ScopedBravo.JoinRoom(ScopedRoomId));
    ScopedServer.Tick(1001);
    ScopedBravo.Tick(1001);
    FBY_CHECK(ScopedBravo.Ready());
    FBY_CHECK(ScopedBravo.PlayerId() == 1);
    FBY_CHECK(ScopedBravo.Ticket().Role == kTicketRoleClient);

    const LobbyRoomInfo* ScopedRoom = ScopedServer.RoomAt(0);
    FBY_CHECK(ScopedRoom != nullptr);
    if (ScopedRoom != nullptr)
    {
        FBY_CHECK(ScopedRoom->Players == 2);
        FBY_CHECK(std::string_view(ScopedRoom->Name) == "Room A");
    }

    FBY_CHECK(ScopedAlpha.StartMatch());
    ScopedServer.Tick(1002);
    ScopedAlpha.Tick(1002);
    ScopedBravo.Tick(1002);
    FBY_CHECK(ScopedAlpha.MatchStarted());
    FBY_CHECK(ScopedBravo.MatchStarted());

    // Joining a missing room is reported, not silently ignored.
    FBY_CHECK(ScopedBravo.JoinRoom(777));
    ScopedServer.Tick(1003);
    ScopedBravo.Tick(1003);
    FBY_CHECK(!ScopedBravo.Ready());
    FBY_CHECK(ScopedBravo.Error() == LobbyError::NoSuchRoom);

    if (GFailures == 0)
    {
        std::printf("net_lobby_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "net_lobby_test: %u failure(s)\n", GFailures);
    return 1;
}
