// FemboyEngine2D - lobby service application.
#include "Engine/Core/Log.hpp"
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/Time.hpp"
#include "Engine/Network/Identity.hpp"
#include "Engine/Network/Lobby.hpp"
#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/Transport.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace
{
struct Options
{
    std::uint16_t Port = 27016;
    std::uint16_t GamePort = 27015;
    std::uint32_t GameHost = 0x7F000001u;
    std::uint64_t TicketLifetime = 300;
    std::uint32_t Frames = 0;
    std::array<std::byte, 32> GameServerKey{};
    std::string IdentityPath = "lobby.identity";
};

void PrintUsage()
{
    std::printf("fby_lobby - FemboyEngine2D lobby service\n"
                "  --port <n>            UDP lobby port (default 27016)\n"
                "  --server <host:port>  game server address (default 127.0.0.1:27015)\n"
                "  --server-key <hex>    game server X25519 public key (64 hex chars)\n"
                "  --identity <path>     lobby identity file (created on first run)\n"
                "  --lifetime <seconds>  ticket lifetime (default 300)\n");
}

[[nodiscard]] bool ParseHexKey(const char* InText, std::array<std::byte, 32>* OutKey)
{
    if (std::strlen(InText) != 64)
    {
        return false;
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < 32; ++ScopedIndex)
    {
        unsigned ScopedValue = 0;
        if (std::sscanf(InText + ScopedIndex * 2, "%2x", &ScopedValue) != 1)
        {
            return false;
        }
        (*OutKey)[ScopedIndex] = static_cast<std::byte>(ScopedValue & 0xFFu);
    }
    return true;
}

[[nodiscard]] bool ParseOptions(int InCount, char** InArgs, Options* OutOptions)
{
    for (int ScopedIndex = 1; ScopedIndex < InCount; ++ScopedIndex)
    {
        const char* ScopedArg = InArgs[ScopedIndex];
        if (std::strcmp(ScopedArg, "--help") == 0)
        {
            PrintUsage();
            return false;
        }
        if (ScopedIndex + 1 >= InCount)
        {
            return false;
        }
        const char* ScopedValue = InArgs[++ScopedIndex];
        if (std::strcmp(ScopedArg, "--port") == 0)
        {
            OutOptions->Port = static_cast<std::uint16_t>(std::atoi(ScopedValue));
        }
        else if (std::strcmp(ScopedArg, "--lifetime") == 0)
        {
            OutOptions->TicketLifetime = static_cast<std::uint64_t>(std::strtoull(ScopedValue, nullptr, 10));
        }
        else if (std::strcmp(ScopedArg, "--frames") == 0)
        {
            OutOptions->Frames = static_cast<std::uint32_t>(std::atoi(ScopedValue));
        }
        else if (std::strcmp(ScopedArg, "--identity") == 0)
        {
            OutOptions->IdentityPath = ScopedValue;
        }
        else if (std::strcmp(ScopedArg, "--server-key") == 0)
        {
            if (!ParseHexKey(ScopedValue, &OutOptions->GameServerKey))
            {
                std::fprintf(stderr, "[lobby] --server-key needs 64 hex characters\n");
                return false;
            }
        }
        else if (std::strcmp(ScopedArg, "--server") == 0)
        {
            Fby::Net::NetAddress ScopedAddress{};
            if (!Fby::Net::ParseAddress(ScopedValue, 27015, &ScopedAddress))
            {
                std::fprintf(stderr, "[lobby] cannot parse --server %s\n", ScopedValue);
                return false;
            }
            OutOptions->GameHost = ScopedAddress.Host;
            OutOptions->GamePort = ScopedAddress.Port;
        }
        else
        {
            return false;
        }
    }
    return true;
}

void PrintPublicKey(const Fby::Net::NetIdentity& InIdentity)
{
    std::printf("[lobby] public key: ");
    for (const std::byte ScopedByte : InIdentity.Signing.Public)
    {
        std::printf("%02x", static_cast<unsigned>(ScopedByte));
    }
    std::printf("\n");
}
}

int main(int InCount, char** InArgs)
{
    Fby::Core::InstallCrashHandler("fby_lobby");
    Fby::SetLogCategory("lobby");

    using namespace Fby::Net;

    Options ScopedOptions;
    if (!ParseOptions(InCount, InArgs, &ScopedOptions))
    {
        return 1;
    }

    NetIdentity ScopedIdentity{};
    std::string ScopedError;
    if (!LoadIdentity(ScopedOptions.IdentityPath, &ScopedIdentity, &ScopedError))
    {
        if (!GenerateIdentity("Lobby", &ScopedIdentity) ||
            !SaveIdentity(ScopedIdentity, ScopedOptions.IdentityPath, &ScopedError))
        {
            std::fprintf(stderr, "[lobby] identity failed: %s\n", ScopedError.c_str());
            return 1;
        }
        std::printf("[lobby] generated identity at %s\n", ScopedOptions.IdentityPath.c_str());
    }
    PrintPublicKey(ScopedIdentity);

    if (!DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::fprintf(stderr, "[lobby] sockets unavailable: %s\n", ScopedError.c_str());
        return 1;
    }
    NetTransport ScopedTransport;
    if (!ScopedTransport.Open(ScopedOptions.Port, &ScopedError))
    {
        std::fprintf(stderr, "[lobby] bind failed on port %u: %s\n", static_cast<unsigned>(ScopedOptions.Port),
                     ScopedError.c_str());
        return 1;
    }

    LobbyServerConfig ScopedConfig{};
    ScopedConfig.GameServer.Host = ScopedOptions.GameHost;
    ScopedConfig.GameServer.Port = ScopedOptions.GamePort;
    ScopedConfig.GameServerPublicKey = ScopedOptions.GameServerKey;
    ScopedConfig.TicketLifetimeSeconds = ScopedOptions.TicketLifetime;

    LobbyServer ScopedLobby;
    if (!ScopedLobby.Start(MakeTransportLink(&ScopedTransport), ScopedIdentity.Signing, ScopedConfig))
    {
        std::fprintf(stderr, "[lobby] start failed\n");
        return 1;
    }

    std::printf("[lobby] listening on port %u, game server %s\n",
                static_cast<unsigned>(ScopedTransport.LocalPort()),
                AddressToString(ScopedConfig.GameServer).c_str());

    std::uint32_t ScopedRoomsSeen = 0;
    std::uint32_t ScopedFrames = 0;
    for (;;)
    {
        ScopedLobby.Tick(Fby::Core::Clock::UnixSeconds());
        ScopedTransport.Tick(0.005f);
        const std::uint32_t ScopedRooms = ScopedLobby.RoomCount();
        if (ScopedRooms != ScopedRoomsSeen)
        {
            ScopedRoomsSeen = ScopedRooms;
            std::printf("[lobby] rooms: %u\n", ScopedRooms);
        }
        Fby::Core::SleepMilliseconds(5.0);
        ++ScopedFrames;
        if (ScopedOptions.Frames != 0 && ScopedFrames >= ScopedOptions.Frames)
        {
            break;
        }
    }
    std::printf("[lobby] stopped after %u frames\n", ScopedFrames);
    return 0;
}
