// FemboyEngine2D - input packet codec and transport/session bridge tests.
#include "Engine/Network/NetProtocol.hpp"

#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Snapshot.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

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

[[nodiscard]] Fby::Net::NetInputFrame MakeFrame(std::uint64_t InTick, std::int16_t InMoveX, std::uint32_t InButtons)
{
    Fby::Net::NetInputFrame ScopedFrame;
    ScopedFrame.tick = InTick;
    ScopedFrame.moveX = InMoveX;
    ScopedFrame.moveY = -32767;
    ScopedFrame.aimX = 1234;
    ScopedFrame.aimY = -4321;
    ScopedFrame.buttons = InButtons;
    ScopedFrame.sequence = static_cast<std::uint16_t>(InTick & 0xFFFFu);
    return ScopedFrame;
}

void TestPacketRoundTrip()
{
    Fby::Net::InputPacket ScopedPacket;
    ScopedPacket.Player.value = 1;
    ScopedPacket.PacketSequence = 77;
    ScopedPacket.FirstSequence = 5;
    ScopedPacket.Count = 3;
    ScopedPacket.Frames[0] = MakeFrame(10, 100, 1);
    ScopedPacket.Frames[1] = MakeFrame(11, -100, 2);
    ScopedPacket.Frames[2] = MakeFrame(12, 0, 0x80000000u);

    std::array<std::byte, Fby::Net::kInputPacketBytes> ScopedBytes{};
    const std::size_t ScopedSize = Fby::Net::EncodeInputPacket(ScopedPacket, ScopedBytes);
    FBY_CHECK(ScopedSize == Fby::Net::kInputPacketHeaderBytes + 3 * sizeof(Fby::Net::NetInputFrame));

    Fby::Net::InputPacket ScopedDecoded;
    FBY_CHECK(Fby::Net::DecodeInputPacket(std::span<const std::byte>(ScopedBytes.data(), ScopedSize), &ScopedDecoded));
    FBY_CHECK(ScopedDecoded.Player.value == 1);
    FBY_CHECK(ScopedDecoded.PacketSequence == 77);
    FBY_CHECK(ScopedDecoded.FirstSequence == 5);
    FBY_CHECK(ScopedDecoded.Count == 3);
    FBY_CHECK(ScopedDecoded.Frames[1].tick == 11);
    FBY_CHECK(ScopedDecoded.Frames[1].moveX == -100);
    FBY_CHECK(ScopedDecoded.Frames[1].moveY == -32767);
    FBY_CHECK(ScopedDecoded.Frames[1].aimX == 1234);
    FBY_CHECK(ScopedDecoded.Frames[1].aimY == -4321);
    FBY_CHECK(ScopedDecoded.Frames[1].buttons == 2);
    FBY_CHECK(ScopedDecoded.Frames[2].buttons == 0x80000000u);
    FBY_CHECK(ScopedDecoded.Frames[0].player == 1);

    // A forged magic, version, stride, count or a truncated payload is rejected.
    std::array<std::byte, Fby::Net::kInputPacketBytes> ScopedCorrupt = ScopedBytes;
    ScopedCorrupt[0] = std::byte{0x00};
    FBY_CHECK(!Fby::Net::DecodeInputPacket(std::span<const std::byte>(ScopedCorrupt.data(), ScopedSize), &ScopedDecoded));
    ScopedCorrupt = ScopedBytes;
    ScopedCorrupt[7] = std::byte{0};
    FBY_CHECK(!Fby::Net::DecodeInputPacket(std::span<const std::byte>(ScopedCorrupt.data(), ScopedSize), &ScopedDecoded));
    ScopedCorrupt = ScopedBytes;
    ScopedCorrupt[7] = static_cast<std::byte>(Fby::Net::kMaxInputFramesPerPacket + 1);
    FBY_CHECK(!Fby::Net::DecodeInputPacket(std::span<const std::byte>(ScopedCorrupt.data(), ScopedSize), &ScopedDecoded));
    ScopedCorrupt = ScopedBytes;
    ScopedCorrupt[6] = static_cast<std::byte>(Fby::Net::kMaxNetPlayers);
    FBY_CHECK(!Fby::Net::DecodeInputPacket(std::span<const std::byte>(ScopedCorrupt.data(), ScopedSize), &ScopedDecoded));
    FBY_CHECK(!Fby::Net::DecodeInputPacket(std::span<const std::byte>(ScopedBytes.data(), ScopedSize - 1),
                                          &ScopedDecoded));
    FBY_CHECK(!Fby::Net::DecodeInputPacket(std::span<const std::byte>(), &ScopedDecoded));

    Fby::Net::InputPacket ScopedEmpty;
    ScopedEmpty.Player.value = 1;
    ScopedEmpty.Count = 0;
    FBY_CHECK(Fby::Net::EncodeInputPacket(ScopedEmpty, ScopedBytes) == 0);
}

struct SessionFixture
{
    entt::registry Registry;
    std::array<std::byte, 32768> Arena{};
    Fby::Net::NetRollbackSession Session;

    [[nodiscard]] bool Initialize()
    {
        Fby::Net::NetRollbackSession::Config ScopedConfig;
        ScopedConfig.playerCount = 2;
        ScopedConfig.localPlayer = 0;
        ScopedConfig.rollbackTicks = 2;
        ScopedConfig.snapshotBytes = 4096;
        ScopedConfig.startTick = 0;
        return Session.Initialize(Registry, Fby::Ecs::SnapshotSchema::BuiltIns(), ScopedConfig,
                                  std::span<std::byte>(Arena.data(), Arena.size())) ==
               Fby::Net::NetRollbackSession::Result::Ok;
    }
};

void TestChannelOverLoopback()
{
    std::string ScopedError;
    if (!Fby::Net::DatagramSocket::EnsureSocketsInitialized(&ScopedError))
    {
        std::printf("net protocol: sockets unavailable, skipping transport test\n");
        return;
    }
    Fby::Net::NetTransport ScopedServer;
    Fby::Net::NetTransport ScopedClient;
    FBY_CHECK(ScopedServer.Open(0, &ScopedError));
    FBY_CHECK(ScopedClient.Open(0, &ScopedError));
    if (!ScopedServer.Opened() || !ScopedClient.Opened())
    {
        std::printf("net protocol: bind failed (%s), skipping transport test\n", ScopedError.c_str());
        return;
    }
    Fby::Net::NetAddress ScopedServerAddress{0x7F000001u, ScopedServer.LocalPort()};
    FBY_CHECK(ScopedClient.Connect(ScopedServerAddress));

    Fby::Net::InputChannel ScopedChannel;
    Fby::Net::NetPlayerId ScopedLocalPlayer;
    ScopedLocalPlayer.value = 1;
    ScopedChannel.Bind(&ScopedClient, ScopedServerAddress, ScopedLocalPlayer);
    FBY_CHECK(ScopedChannel.Bound());

    SessionFixture ScopedFixture;
    FBY_CHECK(ScopedFixture.Initialize());

    Fby::Net::InputChannel ScopedServerChannel;
    Fby::Net::NetPlayerId ScopedServerPlayer;
    ScopedServerPlayer.value = 0;
    ScopedServerChannel.Bind(&ScopedServer, Fby::Net::NetAddress{}, ScopedServerPlayer);

    FBY_CHECK(ScopedChannel.SendFrame(MakeFrame(10, 111, 4)));
    FBY_CHECK(ScopedChannel.SendFrame(MakeFrame(11, -222, 8)));
    FBY_CHECK(ScopedChannel.Stats().FramesSent == 2);

    std::uint32_t ScopedSubmitted = 0;
    for (int ScopedAttempt = 0; ScopedAttempt < 400 && ScopedSubmitted < 2; ++ScopedAttempt)
    {
        ScopedSubmitted += ScopedServerChannel.Receive(ScopedFixture.Session);
        if (ScopedSubmitted < 2)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    FBY_CHECK(ScopedSubmitted == 2);
    FBY_CHECK(ScopedServerChannel.Stats().FramesSubmitted == 2);
    FBY_CHECK(ScopedServerChannel.Stats().PacketsReceived >= 1);

    const Fby::Net::InputRingBuffer& ScopedRemoteInputs = ScopedFixture.Session.Inputs(ScopedLocalPlayer);
    const Fby::Net::NetInputFrame* ScopedFirst = ScopedRemoteInputs.Find(10);
    const Fby::Net::NetInputFrame* ScopedSecond = ScopedRemoteInputs.Find(11);
    FBY_CHECK(ScopedFirst != nullptr);
    FBY_CHECK(ScopedSecond != nullptr);
    if (ScopedFirst != nullptr)
    {
        FBY_CHECK(ScopedFirst->moveX == 111);
        FBY_CHECK(ScopedFirst->buttons == 4);
        FBY_CHECK(ScopedFirst->player == 1);
    }
    if (ScopedSecond != nullptr)
    {
        FBY_CHECK(ScopedSecond->moveX == -222);
    }

    // A batch from an input ring arrives in one packet.
    Fby::Net::InputRingBuffer ScopedRing;
    for (std::uint64_t ScopedTick = 20; ScopedTick < 23; ++ScopedTick)
    {
        FBY_CHECK(ScopedRing.Push(MakeFrame(ScopedTick, static_cast<std::int16_t>(ScopedTick), 0)));
    }
    FBY_CHECK(ScopedChannel.SendRing(ScopedRing, 8));
    ScopedSubmitted = 0;
    for (int ScopedAttempt = 0; ScopedAttempt < 400 && ScopedSubmitted < 3; ++ScopedAttempt)
    {
        ScopedSubmitted += ScopedServerChannel.Receive(ScopedFixture.Session);
        if (ScopedSubmitted < 3)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    FBY_CHECK(ScopedSubmitted == 3);
    FBY_CHECK(ScopedFixture.Session.Inputs(ScopedLocalPlayer).Find(22) != nullptr);

    // A packet for a player the session does not run fails cleanly and is counted.
    Fby::Net::InputChannel ScopedUnknownChannel;
    Fby::Net::NetPlayerId ScopedUnknownPlayer;
    ScopedUnknownPlayer.value = 7;
    ScopedUnknownChannel.Bind(&ScopedClient, ScopedServerAddress, ScopedUnknownPlayer);
    const std::uint64_t ScopedFailuresBefore = ScopedServerChannel.Stats().SubmitFailures;
    FBY_CHECK(ScopedUnknownChannel.SendFrame(MakeFrame(30, 1, 0)));
    for (int ScopedAttempt = 0; ScopedAttempt < 400; ++ScopedAttempt)
    {
        (void)ScopedServerChannel.Receive(ScopedFixture.Session);
        if (ScopedServerChannel.Stats().SubmitFailures > ScopedFailuresBefore)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    FBY_CHECK(ScopedServerChannel.Stats().SubmitFailures > ScopedFailuresBefore);

    ScopedServer.Close();
    ScopedClient.Close();
}
}

int main()
{
    TestPacketRoundTrip();
    TestChannelOverLoopback();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%u net protocol test failure(s)\n", GFailures);
        return 1;
    }
    std::printf("net protocol tests passed\n");
    return 0;
}
