// FemboyEngine2D - UDP transport tests (loopback datagrams, reliability, MTU).
#include "Engine/Network/Transport.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
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

struct Receiver
{
    std::vector<std::vector<std::byte>> Messages;

    static void OnMessage(void* InUser, const Fby::Net::NetAddress& InFrom, std::span<const std::byte> InMessage)
    {
        (void)InFrom;
        auto* ScopedSelf = static_cast<Receiver*>(InUser);
        ScopedSelf->Messages.emplace_back(InMessage.begin(), InMessage.end());
    }
};

[[nodiscard]] bool DrainUntil(Fby::Net::NetTransport& InTransport, Receiver& InReceiver, std::size_t InExpected,
                              int InAttempts = 400)
{
    for (int ScopedAttempt = 0; ScopedAttempt < InAttempts; ++ScopedAttempt)
    {
        InTransport.Poll(&Receiver::OnMessage, &InReceiver);
        if (InReceiver.Messages.size() >= InExpected)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return InReceiver.Messages.size() >= InExpected;
}

[[nodiscard]] bool SameBytes(const std::vector<std::byte>& InLeft, const char* InRight)
{
    const std::size_t ScopedLength = std::strlen(InRight);
    if (InLeft.size() != ScopedLength)
    {
        return false;
    }
    return std::memcmp(InLeft.data(), InRight, ScopedLength) == 0;
}

[[nodiscard]] Fby::Net::NetAddress LocalAddress(std::uint16_t InPort)
{
    Fby::Net::NetAddress ScopedAddress;
    ScopedAddress.Host = 0x7F000001u;
    ScopedAddress.Port = InPort;
    return ScopedAddress;
}

[[nodiscard]] bool OutboxEmpty(const Fby::Net::NetTransport& InTransport, std::uint32_t InPeer)
{
    const Fby::Net::TransportPeer* ScopedPeer = InTransport.Peer(InPeer);
    if (ScopedPeer == nullptr)
    {
        return false;
    }
    for (const Fby::Net::TransportPeer::Outstanding& ScopedSlot : ScopedPeer->Outbox)
    {
        if (ScopedSlot.Used)
        {
            return false;
        }
    }
    return true;
}

void TestAddresses()
{
    Fby::Net::NetAddress ScopedAddress;
    FBY_CHECK(Fby::Net::ParseAddress("127.0.0.1:7042", 0, &ScopedAddress));
    FBY_CHECK(ScopedAddress.Port == 7042);
    FBY_CHECK(ScopedAddress.Host == 0x7F000001u);
    FBY_CHECK(!Fby::Net::ParseAddress("127.0.0.1:70000", 0, &ScopedAddress));
    FBY_CHECK(!Fby::Net::ParseAddress("not-an-ip", 0, &ScopedAddress));
    FBY_CHECK(Fby::Net::ParseAddress("localhost", 9000, &ScopedAddress));
    FBY_CHECK(ScopedAddress.Port == 9000);
    FBY_CHECK(Fby::Net::AddressToString(ScopedAddress) == "127.0.0.1:9000");
}

void TestLoopback()
{
    std::string ScopedError;
    if (!Fby::Net::DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::printf("transport: sockets unavailable (%s), skipping\n", ScopedError.c_str());
        return;
    }

    Fby::Net::NetTransport ScopedServer;
    Fby::Net::NetTransport ScopedClient;
    FBY_CHECK(ScopedServer.Open(0, &ScopedError));
    FBY_CHECK(ScopedClient.Open(0, &ScopedError));
    if (!ScopedServer.Opened() || !ScopedClient.Opened())
    {
        std::printf("transport: bind failed (%s), skipping\n", ScopedError.c_str());
        return;
    }
    const Fby::Net::NetAddress ScopedServerAddress = LocalAddress(ScopedServer.LocalPort());
    FBY_CHECK(ScopedClient.Connect(ScopedServerAddress));

    Receiver ScopedServerInbox;
    Receiver ScopedClientInbox;

    const char ScopedHello[] = "hello";
    const std::span<const std::byte> ScopedHelloBytes(reinterpret_cast<const std::byte*>(ScopedHello), 5);
    FBY_CHECK(ScopedClient.Send(ScopedServerAddress, ScopedHelloBytes, Fby::Net::TransportChannel::Unreliable));
    FBY_CHECK(DrainUntil(ScopedServer, ScopedServerInbox, 1));
    FBY_CHECK(ScopedServerInbox.Messages.size() == 1);
    if (!ScopedServerInbox.Messages.empty())
    {
        FBY_CHECK(SameBytes(ScopedServerInbox.Messages[0], "hello"));
    }

    // Reliable message whose first datagram is lost by the simulation hook: the
    // retransmit must recover it after one Tick.
    const char ScopedReliable[] = "reliable-input-frame";
    ScopedClient.SetDropRate(1.0f);
    FBY_CHECK(ScopedClient.Send(ScopedServerAddress,
                                std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedReliable),
                                                           std::strlen(ScopedReliable)),
                                Fby::Net::TransportChannel::Reliable));
    FBY_CHECK(ScopedClient.Stats().DroppedBySimulation == 1);
    ScopedClient.SetDropRate(0.0f);
    ScopedClient.Tick(0.2f);
    FBY_CHECK(ScopedClient.Stats().Retransmits >= 1);
    FBY_CHECK(DrainUntil(ScopedServer, ScopedServerInbox, 2));
    FBY_CHECK(ScopedServerInbox.Messages.size() == 2);
    if (ScopedServerInbox.Messages.size() >= 2)
    {
        FBY_CHECK(SameBytes(ScopedServerInbox.Messages[1], "reliable-input-frame"));
    }

    // The server's next datagram carries the ack that clears the outbox.
    const char ScopedAck[] = "ack";
    const Fby::Net::TransportPeer* ScopedClientPeer = ScopedServer.Peer(0);
    FBY_CHECK(ScopedClientPeer != nullptr);
    const Fby::Net::NetAddress ScopedClientAddress =
        ScopedClientPeer != nullptr ? ScopedClientPeer->Address : LocalAddress(0);
    FBY_CHECK(ScopedServer.Send(ScopedClientAddress,
                                std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedAck), 3),
                                Fby::Net::TransportChannel::Unreliable));
    (void)DrainUntil(ScopedClient, ScopedClientInbox, 1);
    FBY_CHECK(ScopedClientInbox.Messages.size() >= 1);
    FBY_CHECK(OutboxEmpty(ScopedClient, 0));

    // A payload larger than one MTU is fragmented and reassembled.
    std::vector<std::byte> ScopedLarge(5000);
    for (std::size_t ScopedI = 0; ScopedI < ScopedLarge.size(); ++ScopedI)
    {
        ScopedLarge[ScopedI] = static_cast<std::byte>((ScopedI * 31u + 7u) & 0xFFu);
    }
    FBY_CHECK(ScopedClient.Send(ScopedServerAddress, ScopedLarge, Fby::Net::TransportChannel::Unreliable));
    FBY_CHECK(ScopedClient.Stats().FragmentPackets >= 4);
    FBY_CHECK(DrainUntil(ScopedServer, ScopedServerInbox, 3));
    FBY_CHECK(ScopedServerInbox.Messages.size() == 3);
    if (ScopedServerInbox.Messages.size() >= 3)
    {
        FBY_CHECK(ScopedServerInbox.Messages[2].size() == ScopedLarge.size());
        FBY_CHECK(std::memcmp(ScopedServerInbox.Messages[2].data(), ScopedLarge.data(), ScopedLarge.size()) == 0);
    }

    // A second client becomes a second peer on the server.
    Fby::Net::NetTransport ScopedSecond;
    FBY_CHECK(ScopedSecond.Open(0, &ScopedError));
    FBY_CHECK(ScopedSecond.Connect(ScopedServerAddress));
    const char ScopedSecondMessage[] = "second";
    FBY_CHECK(ScopedSecond.Send(ScopedServerAddress,
                                std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedSecondMessage), 6),
                                Fby::Net::TransportChannel::Unreliable));
    FBY_CHECK(DrainUntil(ScopedServer, ScopedServerInbox, 4));
    FBY_CHECK(ScopedServer.PeerCount() == 2);
    if (ScopedServerInbox.Messages.size() >= 4)
    {
        FBY_CHECK(SameBytes(ScopedServerInbox.Messages[3], "second"));
    }

    // A datagram from an unknown sender with a broken header is rejected.
    const std::uint64_t ScopedRejectedBefore = ScopedServer.Stats().Rejected;
    Fby::Net::DatagramSocket ScopedRaw;
    FBY_CHECK(ScopedRaw.Open(0, &ScopedError));
    std::vector<std::byte> ScopedGarbage(32, std::byte{0xAA});
    FBY_CHECK(ScopedRaw.SendTo(ScopedServerAddress, ScopedGarbage));
    for (int ScopedAttempt = 0; ScopedAttempt < 100; ++ScopedAttempt)
    {
        ScopedServer.Poll(&Receiver::OnMessage, &ScopedServerInbox);
        if (ScopedServer.Stats().Rejected > ScopedRejectedBefore)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    FBY_CHECK(ScopedServer.Stats().Rejected > ScopedRejectedBefore);
    FBY_CHECK(ScopedServerInbox.Messages.size() == 4);

    // Sending after Close is refused, and a second Open is allowed again.
    ScopedSecond.Close();
    FBY_CHECK(!ScopedSecond.Send(ScopedServerAddress,
                                 std::span<const std::byte>(reinterpret_cast<const std::byte*>(ScopedSecondMessage), 6),
                                 Fby::Net::TransportChannel::Unreliable));
    FBY_CHECK(ScopedSecond.Open(0, &ScopedError));
    FBY_CHECK(ScopedSecond.Opened());

    ScopedServer.Close();
    ScopedClient.Close();
    ScopedRaw.Close();
    FBY_CHECK(!ScopedServer.Opened());
    FBY_CHECK(!ScopedClient.Opened());
}
}

int main()
{
    TestAddresses();
    TestLoopback();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u transport test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("transport tests passed\n");
    return 0;
}
