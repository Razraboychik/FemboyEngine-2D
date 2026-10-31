// FemboyEngine2D - secure endpoint over the loopback UDP transport tests.
#include "Engine/Network/SecureEndpoint.hpp"

#include "Engine/Network/Transport.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

[[nodiscard]] Fby::Crypto::X25519KeyPair MakeKey(std::uint8_t InSeed)
{
    std::array<std::byte, 32> ScopedSeed{};
    ScopedSeed.fill(static_cast<std::byte>(InSeed));
    Fby::Crypto::X25519KeyPair ScopedPair{};
    (void)Fby::Crypto::X25519KeyPairFromSeed(ScopedSeed, &ScopedPair);
    return ScopedPair;
}

[[nodiscard]] std::vector<std::byte> TextBytes(std::string_view InText)
{
    std::vector<std::byte> ScopedResult(InText.size());
    for (std::size_t ScopedIndex = 0; ScopedIndex < InText.size(); ++ScopedIndex)
    {
        ScopedResult[ScopedIndex] = static_cast<std::byte>(static_cast<unsigned char>(InText[ScopedIndex]));
    }
    return ScopedResult;
}

[[nodiscard]] bool SameBytes(const std::vector<std::byte>& InBytes, std::string_view InText)
{
    if (InBytes.size() != InText.size())
    {
        return false;
    }
    for (std::size_t ScopedIndex = 0; ScopedIndex < InBytes.size(); ++ScopedIndex)
    {
        if (static_cast<unsigned char>(InBytes[ScopedIndex]) != static_cast<unsigned char>(InText[ScopedIndex]))
        {
            return false;
        }
    }
    return true;
}

struct Collector
{
    std::vector<std::vector<std::byte>> Payloads;
};

void Collect(void* InUser, std::span<const std::byte> InPayload)
{
    auto* ScopedCollector = static_cast<Collector*>(InUser);
    ScopedCollector->Payloads.emplace_back(InPayload.begin(), InPayload.end());
}
}

int main()
{
    using namespace Fby::Net;

    std::string ScopedError;
    if (!DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::printf("secure_endpoint_test: sockets unavailable (%s), skipping\n", ScopedError.c_str());
        return 0;
    }

    NetTransport ScopedServerTransport;
    NetTransport ScopedClientTransport;
    FBY_CHECK(ScopedServerTransport.Open(0, &ScopedError));
    FBY_CHECK(ScopedClientTransport.Open(0, &ScopedError));
    if (!ScopedServerTransport.Opened() || !ScopedClientTransport.Opened())
    {
        std::printf("secure_endpoint_test: bind failed (%s), skipping\n", ScopedError.c_str());
        return 0;
    }

    NetAddress ScopedServerAddress;
    ScopedServerAddress.Host = 0x7F000001u;
    ScopedServerAddress.Port = ScopedServerTransport.LocalPort();
    FBY_CHECK(ScopedClientTransport.Connect(ScopedServerAddress));

    const Fby::Crypto::X25519KeyPair ScopedClientKey = MakeKey(0x61);
    const Fby::Crypto::X25519KeyPair ScopedServerKey = MakeKey(0x62);

    SecureChannelConfig ScopedClientConfig{};
    ScopedClientConfig.LocalStatic = ScopedClientKey;
    ScopedClientConfig.Initiator = true;
    ScopedClientConfig.SessionId = 0xABCDEFu;
    ScopedClientConfig.ExpectedRemoteStatic = ScopedServerKey.Public;
    ScopedClientConfig.RequireExpectedRemote = true;

    SecureChannelConfig ScopedServerConfig{};
    ScopedServerConfig.LocalStatic = ScopedServerKey;
    ScopedServerConfig.SessionId = 0xABCDEFu;

    SecureEndpoint ScopedClient;
    SecureEndpoint ScopedServer;
    FBY_CHECK(ScopedClient.Start(&ScopedClientTransport, ScopedServerAddress, ScopedClientConfig) ==
              SecureStatus::Ok);
    // The server learns the client address from the first datagram.
    FBY_CHECK(ScopedServer.Start(&ScopedServerTransport, NetAddress{}, ScopedServerConfig) == SecureStatus::Ok);

    Collector ScopedServerInbox;
    Collector ScopedClientInbox;
    bool ScopedEstablished = false;
    for (std::uint32_t ScopedStep = 0; ScopedStep < 64 && !ScopedEstablished; ++ScopedStep)
    {
        (void)ScopedServer.Pump(&Collect, &ScopedServerInbox);
        (void)ScopedClient.Pump(&Collect, &ScopedClientInbox);
        ScopedServerTransport.Tick(0.05f);
        ScopedClientTransport.Tick(0.05f);
        ScopedEstablished = ScopedClient.Established() && ScopedServer.Established();
    }
    FBY_CHECK(ScopedEstablished);
    FBY_CHECK(ScopedClient.Channel().SessionId() == ScopedServer.Channel().SessionId());
    FBY_CHECK(ScopedServer.Channel().RemoteStatic() == ScopedClientKey.Public);

    const std::vector<std::byte> ScopedPing = TextBytes("ping");
    FBY_CHECK(ScopedClient.Send(ScopedPing) == SecureStatus::Ok);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 16 && ScopedServerInbox.Payloads.empty(); ++ScopedStep)
    {
        (void)ScopedServer.Pump(&Collect, &ScopedServerInbox);
        ScopedServerTransport.Tick(0.05f);
        ScopedClientTransport.Tick(0.05f);
    }
    FBY_CHECK(ScopedServerInbox.Payloads.size() == 1);
    if (!ScopedServerInbox.Payloads.empty())
    {
        FBY_CHECK(SameBytes(ScopedServerInbox.Payloads[0], "ping"));
    }

    const std::vector<std::byte> ScopedPong = TextBytes("pong");
    FBY_CHECK(ScopedServer.Send(ScopedPong) == SecureStatus::Ok);
    for (std::uint32_t ScopedStep = 0; ScopedStep < 16 && ScopedClientInbox.Payloads.empty(); ++ScopedStep)
    {
        (void)ScopedClient.Pump(&Collect, &ScopedClientInbox);
        ScopedServerTransport.Tick(0.05f);
        ScopedClientTransport.Tick(0.05f);
    }
    FBY_CHECK(ScopedClientInbox.Payloads.size() == 1);
    if (!ScopedClientInbox.Payloads.empty())
    {
        FBY_CHECK(SameBytes(ScopedClientInbox.Payloads[0], "pong"));
    }

    // Encryption overhead is visible on the wire: 8-byte sequence plus 16-byte
    // tag on top of the payload.
    FBY_CHECK(ScopedClientTransport.Stats().BytesSent >= ScopedPing.size() + 24);

    ScopedClient.Stop();
    ScopedServer.Stop();
    ScopedClientTransport.Close();
    ScopedServerTransport.Close();

    if (GFailures == 0)
    {
        std::printf("secure_endpoint_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "secure_endpoint_test: %u failure(s)\n", GFailures);
    return 1;
}
