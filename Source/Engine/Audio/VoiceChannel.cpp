// FemboyEngine2D - voice packet channel implementation.
#include "Engine/Audio/VoiceChannel.hpp"

#include "Engine/Audio/DspGraph.hpp"
#include "Engine/Audio/VoiceSession.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Fby
{
namespace
{
[[nodiscard]] std::uint32_t LoadU32(const std::uint8_t* InData) noexcept
{
    return static_cast<std::uint32_t>(InData[0]) | (static_cast<std::uint32_t>(InData[1]) << 8) |
           (static_cast<std::uint32_t>(InData[2]) << 16) | (static_cast<std::uint32_t>(InData[3]) << 24);
}

[[nodiscard]] std::uint16_t LoadU16(const std::uint8_t* InData) noexcept
{
    return static_cast<std::uint16_t>(InData[0] | (static_cast<std::uint16_t>(InData[1]) << 8));
}

void StoreU32(std::uint8_t* OutData, std::uint32_t InValue) noexcept
{
    OutData[0] = static_cast<std::uint8_t>(InValue);
    OutData[1] = static_cast<std::uint8_t>(InValue >> 8);
    OutData[2] = static_cast<std::uint8_t>(InValue >> 16);
    OutData[3] = static_cast<std::uint8_t>(InValue >> 24);
}

void StoreU16(std::uint8_t* OutData, std::uint16_t InValue) noexcept
{
    OutData[0] = static_cast<std::uint8_t>(InValue);
    OutData[1] = static_cast<std::uint8_t>(InValue >> 8);
}

// 16-bit sequence comparison that tolerates the wrap.
[[nodiscard]] bool SequenceNewer(std::uint16_t InSequence, std::uint16_t InReference) noexcept
{
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(InSequence - InReference)) > 0;
}
}

VoiceChannel::~VoiceChannel()
{
    Stop();
}

bool VoiceChannel::Start(const VoiceChannelConfig& InConfig, SendFn InSend, void* InSendUser) noexcept
{
    Stop();
    if (InSend == nullptr)
    {
        return false;
    }
    MConfig = InConfig;
    MSend = InSend;
    MSendUser = InSendUser;
    if (!MEncoder.Initialize(InConfig.Codec))
    {
        return false;
    }
    MStarted = true;
    return true;
}

void VoiceChannel::Stop() noexcept
{
    MEncoder.Shutdown();
    for (Peer& ScopedPeer : MPeers)
    {
        ResetPeer(ScopedPeer);
    }
    MCaptureRing = nullptr;
    MSend = nullptr;
    MSendUser = nullptr;
    MStats = VoiceChannelStats{};
    MSendSequence = 0;
    MCaptureAccumulatorMs = 0.0f;
    MPlayoutAccumulatorMs = 0.0f;
    MNowMs = 0;
    MStarted = false;
}

void VoiceChannel::ResetPeer(Peer& InPeer) noexcept
{
    InPeer.Decoder.Shutdown();
    InPeer.Present = false;
    InPeer.LastSequence = 0;
    InPeer.HasLastSequence = false;
    InPeer.LastPacketMs = 0;
    InPeer.FirstPacketMs = 0;
    InPeer.HasFirstPacket = false;
    InPeer.PlayoutStarted = false;
    for (JitterSlot& ScopedSlot : InPeer.Jitter)
    {
        ScopedSlot = JitterSlot{};
    }
    InPeer.Ring.Configure(InPeer.RingStorage.data(), kVoiceRingFrames, 1);
}

bool VoiceChannel::AddParticipant(std::uint8_t InPlayer) noexcept
{
    if (InPlayer >= kVoiceMaxPeers || InPlayer == MConfig.LocalPlayer)
    {
        return false;
    }
    Peer& ScopedPeer = MPeers[InPlayer];
    if (ScopedPeer.Present)
    {
        return true;
    }
    ResetPeer(ScopedPeer);
    ScopedPeer.Present = true;
    ScopedPeer.Ring.Configure(ScopedPeer.RingStorage.data(), kVoiceRingFrames, 1);
    (void)ScopedPeer.Decoder.Initialize();
    return true;
}

void VoiceChannel::RemoveParticipant(std::uint8_t InPlayer) noexcept
{
    if (InPlayer >= kVoiceMaxPeers)
    {
        return;
    }
    ResetPeer(MPeers[InPlayer]);
}

bool VoiceChannel::IsParticipant(std::uint8_t InPlayer) const noexcept
{
    return InPlayer < kVoiceMaxPeers && MPeers[InPlayer].Present;
}

FloatRing* VoiceChannel::ParticipantRing(std::uint8_t InPlayer) noexcept
{
    return InPlayer < kVoiceMaxPeers && MPeers[InPlayer].Present ? &MPeers[InPlayer].Ring : nullptr;
}

bool VoiceChannel::ReceivePacket(std::uint8_t InFromPlayer, std::span<const std::uint8_t> InPacket) noexcept
{
    if (!MStarted || InFromPlayer >= kVoiceMaxPeers || InPacket.size() < kVoicePacketHeaderBytes)
    {
        return false;
    }
    if (LoadU32(InPacket.data()) != kVoicePacketMagic || LoadU16(InPacket.data() + 4) != kVoicePacketVersion)
    {
        return false;
    }
    // In relay mode the transport hop is the server while the packet still names
    // the original speaker, so both ids must be known participants.
    const std::uint8_t ScopedSender = InPacket[6];
    if (ScopedSender >= kVoiceMaxPeers || ScopedSender == MConfig.LocalPlayer)
    {
        return false;
    }
    if (!MPeers[InFromPlayer].Present || !MPeers[ScopedSender].Present)
    {
        return false;
    }
    const std::uint16_t ScopedSequence = LoadU16(InPacket.data() + 8);
    const std::uint16_t ScopedSamples = LoadU16(InPacket.data() + 10);
    const std::span<const std::uint8_t> ScopedPayload = InPacket.subspan(kVoicePacketHeaderBytes);
    ++MStats.PacketsReceived;

    if (MConfig.IsRelayServer && MConfig.Routing == VoiceRouting::Relay)
    {
        ++MStats.PacketsForwarded;
        SendPacket(InPacket, ScopedSender);
    }

    (void)InsertJitter(MPeers[ScopedSender], ScopedSequence, ScopedSamples, InPacket[7], ScopedPayload);
    MPeers[ScopedSender].LastPacketMs = MNowMs;
    return true;
}

bool VoiceChannel::InsertJitter(Peer& InPeer, std::uint16_t InSequence, std::uint16_t InSamples,
                                std::uint8_t InFlags, std::span<const std::uint8_t> InPayload) noexcept
{
    if (InPeer.HasLastSequence && !SequenceNewer(InSequence, InPeer.LastSequence))
    {
        ++MStats.PacketsDropped;
        return false;
    }
    if (InPayload.size() > kVoiceMaxPacketBytes || InSamples > kVoiceFrameSamples)
    {
        ++MStats.PacketsDropped;
        return false;
    }

    JitterSlot* ScopedTarget = nullptr;
    for (JitterSlot& ScopedSlot : InPeer.Jitter)
    {
        if (ScopedSlot.Used && ScopedSlot.Sequence == InSequence)
        {
            return false;
        }
        if (!ScopedSlot.Used && ScopedTarget == nullptr)
        {
            ScopedTarget = &ScopedSlot;
        }
    }
    if (ScopedTarget == nullptr)
    {
        // Jitter is full: drop the oldest queued packet to make room.
        for (JitterSlot& ScopedSlot : InPeer.Jitter)
        {
            if (ScopedTarget == nullptr || SequenceNewer(ScopedTarget->Sequence, ScopedSlot.Sequence))
            {
                ScopedTarget = &ScopedSlot;
            }
        }
        ++MStats.PacketsDropped;
    }
    if (ScopedTarget == nullptr)
    {
        return false;
    }

    ScopedTarget->Used = true;
    ScopedTarget->Sequence = InSequence;
    ScopedTarget->Samples = InSamples == 0 ? static_cast<std::uint16_t>(kVoiceFrameSamples) : InSamples;
    ScopedTarget->Flags = InFlags;
    ScopedTarget->Bytes = static_cast<std::uint8_t>(InPayload.size());
    if (!InPayload.empty())
    {
        std::memcpy(ScopedTarget->Data.data(), InPayload.data(), InPayload.size());
    }
    if (!InPeer.HasFirstPacket)
    {
        InPeer.HasFirstPacket = true;
        InPeer.FirstPacketMs = MNowMs;
    }
    return true;
}

VoiceChannel::JitterSlot* VoiceChannel::TakeNextJitter(Peer& InPeer) noexcept
{
    JitterSlot* ScopedBest = nullptr;
    for (JitterSlot& ScopedSlot : InPeer.Jitter)
    {
        if (!ScopedSlot.Used)
        {
            continue;
        }
        if (InPeer.HasLastSequence && !SequenceNewer(ScopedSlot.Sequence, InPeer.LastSequence))
        {
            ScopedSlot.Used = false;
            continue;
        }
        if (ScopedBest == nullptr || SequenceNewer(ScopedBest->Sequence, ScopedSlot.Sequence))
        {
            ScopedBest = &ScopedSlot;
        }
    }
    return ScopedBest;
}

VoiceChannel::JitterSlot* VoiceChannel::FindNextSlot(Peer& InPeer) noexcept
{
    if (!InPeer.HasLastSequence)
    {
        return nullptr;
    }
    JitterSlot* ScopedBest = nullptr;
    const std::uint16_t ScopedExpected = static_cast<std::uint16_t>(InPeer.LastSequence + 1);
    for (JitterSlot& ScopedSlot : InPeer.Jitter)
    {
        if (!ScopedSlot.Used || !SequenceNewer(ScopedSlot.Sequence, ScopedExpected))
        {
            continue;
        }
        if (ScopedBest == nullptr || SequenceNewer(ScopedBest->Sequence, ScopedSlot.Sequence))
        {
            ScopedBest = &ScopedSlot;
        }
    }
    return ScopedBest;
}

void VoiceChannel::RecordFirstPlayed(std::uint16_t InSequence) noexcept
{
    if (!MStats.FirstPlayedValid)
    {
        MStats.FirstPlayedValid = true;
        MStats.FirstPlayedSequence = InSequence;
    }
}

void VoiceChannel::SendPacket(std::span<const std::uint8_t> InPacket, std::uint8_t InExceptPlayer) noexcept
{
    if (MSend == nullptr || InPacket.empty())
    {
        return;
    }
    if (MConfig.Routing == VoiceRouting::Relay && !MConfig.IsRelayServer)
    {
        // 0xFF means "the session remote", resolved by the network bridge.
        if (MSend(MSendUser, MConfig.RelayPlayer, InPacket))
        {
            ++MStats.PacketsSent;
        }
        return;
    }
    for (std::uint8_t ScopedPlayer = 0; ScopedPlayer < kVoiceMaxPeers; ++ScopedPlayer)
    {
        if (!MPeers[ScopedPlayer].Present || ScopedPlayer == InExceptPlayer ||
            ScopedPlayer == MConfig.LocalPlayer)
        {
            continue;
        }
        if (MSend(MSendUser, ScopedPlayer, InPacket))
        {
            ++MStats.PacketsSent;
        }
    }
}

void VoiceChannel::EncodeCapture(VoiceSession* InSession) noexcept
{
    if (MCaptureRing == nullptr || !MEncoder.Ready())
    {
        return;
    }
    while (MCaptureRing->AvailableRead() >= kVoiceFrameSamples)
    {
        const std::uint32_t ScopedRead =
            MCaptureRing->Read(MCaptureScratch.data(), kVoiceFrameSamples);
        if (ScopedRead != kVoiceFrameSamples)
        {
            break;
        }
        if (InSession != nullptr && !InSession->CanTransmit())
        {
            continue;
        }
        std::uint8_t* ScopedPacket = MPacketScratch.data();
        const std::uint32_t ScopedBytes =
            MEncoder.Encode(MCaptureScratch, std::span<std::uint8_t>(ScopedPacket + kVoicePacketHeaderBytes,
                                                                    kVoiceMaxPacketBytes - kVoicePacketHeaderBytes));
        if (ScopedBytes == 0)
        {
            continue;
        }
        StoreU32(ScopedPacket, kVoicePacketMagic);
        StoreU16(ScopedPacket + 4, kVoicePacketVersion);
        ScopedPacket[6] = MConfig.LocalPlayer;
        ScopedPacket[7] = 0;
        StoreU16(ScopedPacket + 8, MSendSequence++);
        StoreU16(ScopedPacket + 10, static_cast<std::uint16_t>(kVoiceFrameSamples));
        SendPacket(std::span<const std::uint8_t>(ScopedPacket, kVoicePacketHeaderBytes + ScopedBytes), 0xFF);
    }
}

void VoiceChannel::DecodePeer(Peer& InPeer, VoiceSession* InSession) noexcept
{
    if (!InPeer.Present || !InPeer.Decoder.Ready())
    {
        return;
    }
    // First-packet policy: hold playout for one 20 ms interval so a lower
    // out-of-order sequence can still become the first played frame. This stops
    // a jitter "future" packet from locking the stream to its sequence.
    if (!InPeer.PlayoutStarted)
    {
        if (!InPeer.HasFirstPacket || MNowMs < InPeer.FirstPacketMs + static_cast<std::uint64_t>(kVoicePlayoutMs))
        {
            return;
        }
        InPeer.PlayoutStarted = true;
    }
    const std::uint8_t ScopedPlayer = static_cast<std::uint8_t>(&InPeer - MPeers.data());
    JitterSlot* ScopedSlot = TakeNextJitter(InPeer);

    if (ScopedSlot == nullptr)
    {
        // Playout underrun: run PLC briefly so speech tails are not cut off.
        if (InPeer.HasLastSequence && MNowMs > InPeer.LastPacketMs && MNowMs - InPeer.LastPacketMs < 500)
        {
            const std::uint32_t ScopedFrames = InPeer.Decoder.Conceal(MDecodeScratch);
            if (ScopedFrames > 0)
            {
                (void)InPeer.Ring.Write(MDecodeScratch.data(), ScopedFrames);
                ++MStats.FramesConcealed;
            }
        }
        return;
    }

    if (InPeer.HasLastSequence && SequenceNewer(ScopedSlot->Sequence,
                                                static_cast<std::uint16_t>(InPeer.LastSequence + 1)))
    {
        // A hole: reconstruct the missing frame from the Opus FEC carried by the
        // next packet before falling back to concealment.
        JitterSlot* ScopedNext = FindNextSlot(InPeer);
        if (ScopedNext != nullptr &&
            InPeer.Decoder.DecodeFec(
                std::span<const std::uint8_t>(ScopedNext->Data.data(), ScopedNext->Bytes), MDecodeScratch) > 0)
        {
            (void)InPeer.Ring.Write(MDecodeScratch.data(), kVoiceFrameSamples);
            ++MStats.FramesFecRecovered;
            ++InPeer.LastSequence;
            RecordFirstPlayed(InPeer.LastSequence);
            return;
        }
        const std::uint32_t ScopedFrames = InPeer.Decoder.Conceal(MDecodeScratch);
        if (ScopedFrames > 0)
        {
            (void)InPeer.Ring.Write(MDecodeScratch.data(), ScopedFrames);
            ++MStats.FramesConcealed;
        }
        ++InPeer.LastSequence;
        return;
    }

    const std::uint32_t ScopedFrames = InPeer.Decoder.Decode(
        std::span<const std::uint8_t>(ScopedSlot->Data.data(), ScopedSlot->Bytes), MDecodeScratch);
    InPeer.LastSequence = ScopedSlot->Sequence;
    InPeer.HasLastSequence = true;
    ScopedSlot->Used = false;
    if (ScopedFrames == 0)
    {
        return;
    }
    (void)InPeer.Ring.Write(MDecodeScratch.data(), ScopedFrames);
    ++MStats.FramesDecoded;
    RecordFirstPlayed(ScopedSlot->Sequence);

    if (InSession != nullptr)
    {
        double ScopedSum = 0.0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedFrames; ++ScopedIndex)
        {
            ScopedSum += static_cast<double>(MDecodeScratch[ScopedIndex]) * MDecodeScratch[ScopedIndex];
        }
        const float ScopedRms =
            static_cast<float>(std::sqrt(ScopedSum / static_cast<double>(ScopedFrames)));
        InSession->SetParticipantLevel(ScopedPlayer, ScopedRms);
    }
}

void VoiceChannel::Tick(float InDeltaSeconds, VoiceSession* InSession) noexcept
{
    if (!MStarted)
    {
        return;
    }
    const float ScopedDeltaMs = std::clamp(InDeltaSeconds, 0.0f, 0.25f) * 1000.0f;
    MNowMs += static_cast<std::uint64_t>(ScopedDeltaMs);
    MCaptureAccumulatorMs += ScopedDeltaMs;
    MPlayoutAccumulatorMs += ScopedDeltaMs;

    std::uint32_t ScopedSteps = 0;
    while (MCaptureAccumulatorMs >= static_cast<float>(kVoicePlayoutMs) && ScopedSteps < 8)
    {
        MCaptureAccumulatorMs -= static_cast<float>(kVoicePlayoutMs);
        ++ScopedSteps;
        if (!MConfig.IsRelayServer)
        {
            EncodeCapture(InSession);
        }
    }

    ScopedSteps = 0;
    while (MPlayoutAccumulatorMs >= static_cast<float>(kVoicePlayoutMs) && ScopedSteps < 8)
    {
        MPlayoutAccumulatorMs -= static_cast<float>(kVoicePlayoutMs);
        ++ScopedSteps;
        if (MConfig.IsRelayServer)
        {
            continue;
        }
        for (Peer& ScopedPeer : MPeers)
        {
            DecodePeer(ScopedPeer, InSession);
        }
    }
}

DspResult ConfigureVoiceDucking(DspGraph& InGraph, BusId InVoiceBus, BusId InMusicBus) noexcept
{
    DuckingParams ScopedParams{};
    ScopedParams.sourceBus = InVoiceBus;
    ScopedParams.threshold = 0.08f;
    ScopedParams.ratio = 2.0f;
    ScopedParams.depth = 0.7f;
    ScopedParams.attackSeconds = 0.01f;
    ScopedParams.releaseSeconds = 0.4f;
    return InGraph.SetBusDucking(InMusicBus, ScopedParams);
}
}
