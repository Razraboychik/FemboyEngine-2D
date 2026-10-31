// FemboyEngine2D - voice packet channel: Opus frames, jitter and relay/mesh routing.
#pragma once

#include "Engine/Audio/AudioStream.hpp"
#include "Engine/Audio/VoiceCodec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Fby
{

class VoiceSession;
class DspGraph;
// Forward-declared so this header does not pull the whole mixer: the only
// mixer-touching function is ConfigureVoiceDucking, defined in the .cpp.
enum class DspResult : std::uint8_t;
using BusId = std::uint16_t;

inline constexpr std::uint32_t kVoiceMaxPeers = 8;
inline constexpr std::uint32_t kVoiceJitterSlots = 8;
inline constexpr std::size_t kVoicePacketHeaderBytes = 12;
inline constexpr std::uint32_t kVoiceRingFrames = kVoiceFrameSamples * 4;
inline constexpr std::uint32_t kVoicePacketMagic = 0x584F5646u;
inline constexpr std::uint16_t kVoicePacketVersion = 1;
inline constexpr std::uint32_t kVoicePlayoutMs = 20;

enum class VoiceRouting : std::uint8_t
{
    Relay = 0,
    Mesh,
};

struct VoiceChannelConfig
{
    VoiceRouting Routing = VoiceRouting::Relay;
    std::uint8_t LocalPlayer = 0xFF;
    // Relay mode: the player that forwards voice (the server). Ignored for mesh.
    std::uint8_t RelayPlayer = 0xFF;
    bool IsRelayServer = false;
    VoiceCodecConfig Codec{};
};

struct VoiceChannelStats
{
    std::uint64_t PacketsSent = 0;
    std::uint64_t PacketsReceived = 0;
    std::uint64_t PacketsDropped = 0;
    std::uint64_t PacketsForwarded = 0;
    std::uint64_t FramesDecoded = 0;
    std::uint64_t FramesConcealed = 0;
    // Frames reconstructed from the in-band FEC of the following packet.
    std::uint64_t FramesFecRecovered = 0;
    // First sequence that actually reached the playout ring; the first-packet
    // hold lets an out-of-order lower sequence win before playout starts.
    std::uint16_t FirstPlayedSequence = 0;
    bool FirstPlayedValid = false;
};

// One channel covers the local capture stream and every remote participant. The
// transport is a function pointer: the caller encrypts packets (SecureChannel)
// and routes them by player id, so relay and mesh share this code.
class VoiceChannel
{
public:
    using SendFn = bool (*)(void* InUser, std::uint8_t InToPlayer,
                            std::span<const std::uint8_t> InPacket) noexcept;

    VoiceChannel() = default;
    ~VoiceChannel();

    VoiceChannel(const VoiceChannel&) = delete;
    VoiceChannel& operator=(const VoiceChannel&) = delete;

    [[nodiscard]] bool Start(const VoiceChannelConfig& InConfig, SendFn InSend, void* InSendUser) noexcept;
    void Stop() noexcept;

    [[nodiscard]] bool AddParticipant(std::uint8_t InPlayer) noexcept;
    void RemoveParticipant(std::uint8_t InPlayer) noexcept;
    [[nodiscard]] bool IsParticipant(std::uint8_t InPlayer) const noexcept;

    void SetSendSink(SendFn InSend, void* InUser) noexcept
    {
        MSend = InSend;
        MSendUser = InUser;
    }

    void SetCaptureRing(FloatRing* InRing) noexcept
    {
        MCaptureRing = InRing;
    }

    // Called by the transport with an authenticated packet from a peer.
    [[nodiscard]] bool ReceivePacket(std::uint8_t InFromPlayer,
                                     std::span<const std::uint8_t> InPacket) noexcept;

    // Encodes captured frames and plays decoded jitter frames. `InSession`
    // supplies mute/PTT gating and receives participant levels; it may be null.
    void Tick(float InDeltaSeconds, VoiceSession* InSession) noexcept;

    [[nodiscard]] FloatRing* ParticipantRing(std::uint8_t InPlayer) noexcept;
    [[nodiscard]] const VoiceChannelStats& Stats() const noexcept
    {
        return MStats;
    }

protected:
    struct JitterSlot
    {
        bool Used = false;
        std::uint16_t Sequence = 0;
        std::uint16_t Samples = 0;
        std::uint8_t Flags = 0;
        std::uint8_t Bytes = 0;
        std::array<std::uint8_t, kVoiceMaxPacketBytes> Data{};
    };

    struct Peer
    {
        bool Present = false;
        VoiceDecoder Decoder{};
        FloatRing Ring{};
        std::array<float, kVoiceRingFrames> RingStorage{};
        std::array<JitterSlot, kVoiceJitterSlots> Jitter{};
        std::uint16_t LastSequence = 0;
        bool HasLastSequence = false;
        std::uint64_t LastPacketMs = 0;
        std::uint64_t FirstPacketMs = 0;
        bool HasFirstPacket = false;
        bool PlayoutStarted = false;
    };

    void EncodeCapture(VoiceSession* InSession) noexcept;
    void DecodePeer(Peer& InPeer, VoiceSession* InSession) noexcept;
    void SendPacket(std::span<const std::uint8_t> InPacket, std::uint8_t InExceptPlayer) noexcept;
    [[nodiscard]] bool InsertJitter(Peer& InPeer, std::uint16_t InSequence, std::uint16_t InSamples,
                                    std::uint8_t InFlags, std::span<const std::uint8_t> InPayload) noexcept;
    [[nodiscard]] JitterSlot* TakeNextJitter(Peer& InPeer) noexcept;
    // Lowest queued slot that is more than one sequence ahead of playout; its
    // Opus FEC can recover the missing frame.
    [[nodiscard]] JitterSlot* FindNextSlot(Peer& InPeer) noexcept;
    void RecordFirstPlayed(std::uint16_t InSequence) noexcept;
    void ResetPeer(Peer& InPeer) noexcept;

    VoiceChannelConfig MConfig{};
    SendFn MSend = nullptr;
    void* MSendUser = nullptr;
    VoiceEncoder MEncoder{};
    FloatRing* MCaptureRing = nullptr;
    std::array<Peer, kVoiceMaxPeers> MPeers{};
    std::array<float, kVoiceFrameSamples> MCaptureScratch{};
    std::array<float, kVoiceFrameSamples> MDecodeScratch{};
    std::array<std::uint8_t, kVoiceMaxPacketBytes> MPacketScratch{};
    VoiceChannelStats MStats{};
    std::uint16_t MSendSequence = 0;
    float MCaptureAccumulatorMs = 0.0f;
    float MPlayoutAccumulatorMs = 0.0f;
    std::uint64_t MNowMs = 0;
    bool MStarted = false;
};

// Routes the voice bus into the music bus with ducking. The Voice bus itself is
// created by the caller (DspGraph::CreateBus) and streamed per participant.
[[nodiscard]] DspResult ConfigureVoiceDucking(DspGraph& InGraph, BusId InVoiceBus, BusId InMusicBus) noexcept;
}
