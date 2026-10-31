// FemboyEngine2D - input packet codec and transport/session glue.
#include "Engine/Network/NetProtocol.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Net
{
namespace
{
constexpr std::size_t kFrameStride = sizeof(NetInputFrame);

void WriteU16(std::byte* OutData, std::uint16_t InValue) noexcept
{
    OutData[0] = static_cast<std::byte>(InValue & 0xFFu);
    OutData[1] = static_cast<std::byte>((InValue >> 8) & 0xFFu);
}

void WriteU32(std::byte* OutData, std::uint32_t InValue) noexcept
{
    WriteU16(OutData, static_cast<std::uint16_t>(InValue & 0xFFFFu));
    WriteU16(OutData + 2, static_cast<std::uint16_t>((InValue >> 16) & 0xFFFFu));
}

void WriteU64(std::byte* OutData, std::uint64_t InValue) noexcept
{
    WriteU32(OutData, static_cast<std::uint32_t>(InValue & 0xFFFFFFFFu));
    WriteU32(OutData + 4, static_cast<std::uint32_t>((InValue >> 32) & 0xFFFFFFFFu));
}

[[nodiscard]] std::uint16_t ReadU16(const std::byte* InData) noexcept
{
    return static_cast<std::uint16_t>(static_cast<unsigned>(InData[0]) |
                                      (static_cast<unsigned>(InData[1]) << 8));
}

[[nodiscard]] std::uint32_t ReadU32(const std::byte* InData) noexcept
{
    return static_cast<std::uint32_t>(ReadU16(InData)) | (static_cast<std::uint32_t>(ReadU16(InData + 2)) << 16);
}

[[nodiscard]] std::uint64_t ReadU64(const std::byte* InData) noexcept
{
    return static_cast<std::uint64_t>(ReadU32(InData)) | (static_cast<std::uint64_t>(ReadU32(InData + 4)) << 32);
}

void WriteFrame(std::byte* OutData, const NetInputFrame& InFrame) noexcept
{
    WriteU64(OutData + 0, InFrame.tick);
    WriteU16(OutData + 8, static_cast<std::uint16_t>(InFrame.moveX));
    WriteU16(OutData + 10, static_cast<std::uint16_t>(InFrame.moveY));
    WriteU16(OutData + 12, static_cast<std::uint16_t>(InFrame.aimX));
    WriteU16(OutData + 14, static_cast<std::uint16_t>(InFrame.aimY));
    WriteU32(OutData + 16, InFrame.buttons);
    WriteU16(OutData + 20, InFrame.sequence);
    OutData[22] = static_cast<std::byte>(InFrame.player);
    OutData[23] = static_cast<std::byte>(InFrame.reserved);
}

[[nodiscard]] bool ReadFrame(const std::byte* InData, NetInputFrame* OutFrame) noexcept
{
    if (OutFrame == nullptr)
    {
        return false;
    }
    NetInputFrame ScopedFrame;
    ScopedFrame.tick = ReadU64(InData + 0);
    ScopedFrame.moveX = static_cast<std::int16_t>(ReadU16(InData + 8));
    ScopedFrame.moveY = static_cast<std::int16_t>(ReadU16(InData + 10));
    ScopedFrame.aimX = static_cast<std::int16_t>(ReadU16(InData + 12));
    ScopedFrame.aimY = static_cast<std::int16_t>(ReadU16(InData + 14));
    ScopedFrame.buttons = ReadU32(InData + 16);
    ScopedFrame.sequence = ReadU16(InData + 20);
    ScopedFrame.player = static_cast<std::uint8_t>(InData[22]);
    ScopedFrame.reserved = static_cast<std::uint8_t>(InData[23]);
    *OutFrame = ScopedFrame;
    return true;
}

struct ReceiveSink
{
    InputChannel* channel = nullptr;
    NetRollbackSession* session = nullptr;
    std::uint32_t submitted = 0;
};
}

std::size_t EncodeInputPacket(const InputPacket& InPacket, std::span<std::byte> OutBuffer) noexcept
{
    if (InPacket.Count == 0 || InPacket.Count > kMaxInputFramesPerPacket || !InPacket.Player.IsValid())
    {
        return 0;
    }
    const std::size_t ScopedBytes = kInputPacketHeaderBytes + InPacket.Count * kFrameStride;
    if (OutBuffer.size() < ScopedBytes)
    {
        return 0;
    }
    std::memset(OutBuffer.data(), 0, kInputPacketHeaderBytes);
    WriteU32(OutBuffer.data() + 0, kInputPacketMagic);
    WriteU16(OutBuffer.data() + 4, kInputPacketVersion);
    OutBuffer[6] = static_cast<std::byte>(InPacket.Player.value);
    OutBuffer[7] = static_cast<std::byte>(InPacket.Count);
    WriteU16(OutBuffer.data() + 8, InPacket.FirstSequence);
    WriteU16(OutBuffer.data() + 10, static_cast<std::uint16_t>(kFrameStride));
    WriteU32(OutBuffer.data() + 12, InPacket.PacketSequence);
    WriteU32(OutBuffer.data() + 16, 0);
    for (std::uint32_t ScopedI = 0; ScopedI < InPacket.Count; ++ScopedI)
    {
        NetInputFrame ScopedFrame = InPacket.Frames[ScopedI];
        ScopedFrame.player = InPacket.Player.value;
        WriteFrame(OutBuffer.data() + kInputPacketHeaderBytes + ScopedI * kFrameStride, ScopedFrame);
    }
    return ScopedBytes;
}

bool DecodeInputPacket(std::span<const std::byte> InBytes, InputPacket* OutPacket) noexcept
{
    if (OutPacket == nullptr || InBytes.size() < kInputPacketHeaderBytes)
    {
        return false;
    }
    if (ReadU32(InBytes.data()) != kInputPacketMagic || ReadU16(InBytes.data() + 4) != kInputPacketVersion)
    {
        return false;
    }
    const std::uint8_t ScopedPlayer = static_cast<std::uint8_t>(InBytes[6]);
    const std::uint8_t ScopedCount = static_cast<std::uint8_t>(InBytes[7]);
    if (ScopedPlayer >= kMaxNetPlayers || ScopedCount == 0 || ScopedCount > kMaxInputFramesPerPacket)
    {
        return false;
    }
    if (ReadU16(InBytes.data() + 10) != static_cast<std::uint16_t>(kFrameStride))
    {
        return false;
    }
    const std::size_t ScopedExpected = kInputPacketHeaderBytes + static_cast<std::size_t>(ScopedCount) * kFrameStride;
    if (InBytes.size() != ScopedExpected)
    {
        return false;
    }
    InputPacket ScopedPacket;
    ScopedPacket.Player.value = ScopedPlayer;
    ScopedPacket.PacketSequence = ReadU32(InBytes.data() + 12);
    ScopedPacket.FirstSequence = ReadU16(InBytes.data() + 8);
    ScopedPacket.Count = ScopedCount;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedPacket.Count; ++ScopedI)
    {
        if (!ReadFrame(InBytes.data() + kInputPacketHeaderBytes + ScopedI * kFrameStride, &ScopedPacket.Frames[ScopedI]))
        {
            return false;
        }
        if (ScopedPacket.Frames[ScopedI].tick == kInvalidNetTick)
        {
            return false;
        }
        ScopedPacket.Frames[ScopedI].player = static_cast<std::uint8_t>(ScopedPlayer);
    }
    *OutPacket = ScopedPacket;
    return true;
}

void InputChannel::Bind(NetTransport* InTransport, const NetAddress& InRemote, NetPlayerId InLocalPlayer) noexcept
{
    MTransport = InTransport;
    MRemote = InRemote;
    MLocalPlayer = InLocalPlayer;
}

void InputChannel::Unbind() noexcept
{
    MTransport = nullptr;
    MRemote = NetAddress{};
}

bool InputChannel::SendFrame(const NetInputFrame& InFrame) noexcept
{
    if (MTransport == nullptr || !MLocalPlayer.IsValid())
    {
        return false;
    }
    InputPacket ScopedPacket;
    ScopedPacket.Player = MLocalPlayer;
    ScopedPacket.PacketSequence = MPacketSequence++;
    ScopedPacket.FirstSequence = InFrame.sequence;
    ScopedPacket.Count = 1;
    ScopedPacket.Frames[0] = InFrame;
    std::array<std::byte, kInputPacketBytes> ScopedBytes{};
    const std::size_t ScopedSize = EncodeInputPacket(ScopedPacket, ScopedBytes);
    if (ScopedSize == 0)
    {
        ++MStats.Rejected;
        return false;
    }
    if (!MTransport->Send(MRemote, std::span<const std::byte>(ScopedBytes.data(), ScopedSize),
                          TransportChannel::Reliable))
    {
        ++MStats.Rejected;
        return false;
    }
    ++MStats.PacketsSent;
    ++MStats.FramesSent;
    return true;
}

bool InputChannel::SendRing(const InputRingBuffer& InRing, std::uint32_t InMaxFrames) noexcept
{
    if (MTransport == nullptr || !MLocalPlayer.IsValid() || InRing.Count() == 0 || InMaxFrames == 0)
    {
        return false;
    }
    const std::uint32_t ScopedCapacity = std::min(InMaxFrames, kMaxInputFramesPerPacket);
    const std::uint64_t ScopedNewest = InRing.NewestTick();
    std::uint64_t ScopedStart = ScopedNewest;
    std::uint32_t ScopedAvailable = 1;
    while (ScopedAvailable < ScopedCapacity && ScopedStart > InRing.OldestTick())
    {
        if (InRing.Find(ScopedStart - 1) == nullptr)
        {
            break;
        }
        --ScopedStart;
        ++ScopedAvailable;
    }
    InputPacket ScopedPacket;
    ScopedPacket.Player = MLocalPlayer;
    ScopedPacket.PacketSequence = MPacketSequence++;
    ScopedPacket.Count = 0;
    for (std::uint64_t ScopedTick = ScopedStart; ScopedTick <= ScopedNewest; ++ScopedTick)
    {
        const NetInputFrame* ScopedFrame = InRing.Find(ScopedTick);
        if (ScopedFrame == nullptr)
        {
            continue;
        }
        if (ScopedPacket.Count == 0)
        {
            ScopedPacket.FirstSequence = ScopedFrame->sequence;
        }
        ScopedPacket.Frames[ScopedPacket.Count++] = *ScopedFrame;
    }
    if (ScopedPacket.Count == 0)
    {
        return false;
    }
    std::array<std::byte, kInputPacketBytes> ScopedBytes{};
    const std::size_t ScopedSize = EncodeInputPacket(ScopedPacket, ScopedBytes);
    if (ScopedSize == 0)
    {
        ++MStats.Rejected;
        return false;
    }
    if (!MTransport->Send(MRemote, std::span<const std::byte>(ScopedBytes.data(), ScopedSize),
                          TransportChannel::Reliable))
    {
        ++MStats.Rejected;
        return false;
    }
    ++MStats.PacketsSent;
    MStats.FramesSent += ScopedPacket.Count;
    return true;
}

std::uint32_t InputChannel::Receive(NetRollbackSession& InSession) noexcept
{
    if (MTransport == nullptr)
    {
        return 0;
    }
    ReceiveSink ScopedSink;
    ScopedSink.channel = this;
    ScopedSink.session = &InSession;
    (void)MTransport->Poll(&InputChannel::OnPacket, &ScopedSink);
    return ScopedSink.submitted;
}

void InputChannel::OnPacket(void* InUser, const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept
{
    (void)InFrom;
    auto* ScopedSink = static_cast<ReceiveSink*>(InUser);
    if (ScopedSink == nullptr || ScopedSink->channel == nullptr || ScopedSink->session == nullptr)
    {
        return;
    }
    InputChannel& ScopedChannel = *ScopedSink->channel;
    InputPacket ScopedPacket;
    if (!DecodeInputPacket(InMessage, &ScopedPacket))
    {
        ++ScopedChannel.MStats.Rejected;
        return;
    }
    ++ScopedChannel.MStats.PacketsReceived;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedPacket.Count; ++ScopedI)
    {
        NetInputFrame ScopedFrame = ScopedPacket.Frames[ScopedI];
        ScopedFrame.player = ScopedPacket.Player.value;
        if (ScopedSink->session->SubmitRemoteInput(ScopedPacket.Player, ScopedFrame) ==
            NetRollbackSession::Result::Ok)
        {
            ++ScopedSink->submitted;
            ++ScopedChannel.MStats.FramesSubmitted;
        }
        else
        {
            ++ScopedChannel.MStats.SubmitFailures;
        }
    }
}
}
