// FemboyEngine2D - input packets that connect NetTransport to NetRollbackSession.
#pragma once

#include "Engine/Network/NetRollback.hpp"
#include "Engine/Network/Transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby::Net
{

inline constexpr std::uint32_t kInputPacketMagic = 0x49504E46u;
inline constexpr std::uint16_t kInputPacketVersion = 1;
inline constexpr std::uint32_t kInputPacketHeaderBytes = 20;
inline constexpr std::uint32_t kMaxInputFramesPerPacket = 8;
inline constexpr std::uint32_t kInputPacketBytes = kInputPacketHeaderBytes +
                                                   kMaxInputFramesPerPacket * static_cast<std::uint32_t>(sizeof(NetInputFrame));

// A batch of one player's inputs. The packet is versioned and self-describing so
// a mismatched build is rejected instead of misread as input.
struct InputPacket
{
    NetPlayerId Player{};
    std::uint32_t PacketSequence = 0;
    std::uint16_t FirstSequence = 0;
    std::uint32_t Count = 0;
    std::array<NetInputFrame, kMaxInputFramesPerPacket> Frames{};
};

static_assert(sizeof(NetInputFrame) == 24);

// Writes the packet into `OutBuffer`. Returns the byte count, or 0 when the
// buffer is too small or the packet is invalid.
[[nodiscard]] std::size_t EncodeInputPacket(const InputPacket& InPacket, std::span<std::byte> OutBuffer) noexcept;

// Validates magic, version, table size, player id and the frame stride. Frames
// inherit the packet's player id, so a forged per-frame id cannot impersonate
// another player.
[[nodiscard]] bool DecodeInputPacket(std::span<const std::byte> InBytes, InputPacket* OutPacket) noexcept;

struct InputChannelStats
{
    std::uint64_t PacketsSent = 0;
    std::uint64_t FramesSent = 0;
    std::uint64_t PacketsReceived = 0;
    std::uint64_t FramesSubmitted = 0;
    std::uint64_t Rejected = 0;
    std::uint64_t SubmitFailures = 0;
};

// Binds a transport peer to a rollback session: Send* writes input packets, and
// Receive() decodes whatever arrived and feeds SubmitRemoteInput().
class InputChannel
{
public:
    InputChannel() = default;

    void Bind(NetTransport* InTransport, const NetAddress& InRemote, NetPlayerId InLocalPlayer) noexcept;
    void Unbind() noexcept;
    [[nodiscard]] bool Bound() const noexcept
    {
        return MTransport != nullptr;
    }

    // Sends one frame (one packet). A small batch is preferable: it amortizes the
    // header and lets one lost datagram cost several ticks at once.
    [[nodiscard]] bool SendFrame(const NetInputFrame& InFrame) noexcept;
    // Sends up to `InMaxFrames` of the newest frames in the ring, oldest first.
    [[nodiscard]] bool SendRing(const InputRingBuffer& InRing, std::uint32_t InMaxFrames) noexcept;

    // Drains the transport and submits every decoded remote frame to the session.
    // Returns the number of frames accepted by the session.
    std::uint32_t Receive(NetRollbackSession& InSession) noexcept;

    [[nodiscard]] const InputChannelStats& Stats() const noexcept
    {
        return MStats;
    }
    void ResetStats() noexcept
    {
        MStats = InputChannelStats{};
    }

private:
    static void OnPacket(void* InUser, const NetAddress& InFrom, std::span<const std::byte> InMessage) noexcept;

    NetTransport* MTransport = nullptr;
    NetAddress MRemote{};
    NetPlayerId MLocalPlayer{};
    std::uint32_t MPacketSequence = 0;
    std::uint16_t MFrameSequence = 0;
    InputChannelStats MStats{};
};
}
