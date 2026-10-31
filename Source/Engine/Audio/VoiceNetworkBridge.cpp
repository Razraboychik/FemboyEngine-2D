// FemboyEngine2D - bridge from the voice runtime to the secure session transport.
#include "Engine/Audio/VoiceNetworkBridge.hpp"

#include <algorithm>

namespace Fby
{

bool VoiceNetworkBridge::Start(VoiceSystem& InVoice, Net::SecureSessionTransport& InTransport,
                               Net::NetSession& InSession, VoiceSession& InVoiceSession,
                               std::uint8_t InLocalPlayer) noexcept
{
    Stop();
    MVoice = &InVoice;
    MTransport = &InTransport;
    MSession = &InSession;
    MVoiceSession = &InVoiceSession;
    MLocalPlayer = InLocalPlayer;
    MTransport->SetVoiceSink(&VoiceNetworkBridge::ReceiveThunk, this);
    MVoice->SetSendSink(&VoiceNetworkBridge::SendThunk, this);
    ResetRateBuckets();
    SyncParticipants();
    return true;
}

bool VoiceNetworkBridge::StartLoopback(VoiceSystem& InVoice, Net::NetSession& InSession,
                                       VoiceSession& InVoiceSession, std::uint8_t InLocalPlayer,
                                       std::uint8_t InRemotePlayer, Net::NetLink InLink,
                                       const Net::NetAddress& InPeer) noexcept
{
    Stop();
    if (!InLink.Valid() || InPeer.Port == 0 || InLocalPlayer >= kVoiceMaxPeers ||
        InRemotePlayer >= kVoiceMaxPeers || InLocalPlayer == InRemotePlayer)
    {
        return false;
    }
    MVoice = &InVoice;
    MSession = &InSession;
    MVoiceSession = &InVoiceSession;
    MLocalPlayer = InLocalPlayer;
    MLink = InLink;
    MPeer = InPeer;
    MRemotePlayer = InRemotePlayer;
    MLoopback = true;
    MVoice->SetSendSink(&VoiceNetworkBridge::SendThunk, this);
    if (MVoice->Channel().AddParticipant(InRemotePlayer))
    {
        (void)MVoiceSession->AddParticipant(InRemotePlayer);
        MAdded[InRemotePlayer] = true;
    }
    ResetRateBuckets();
    SyncParticipants();
    return true;
}

void VoiceNetworkBridge::Stop() noexcept
{
    if (MTransport != nullptr)
    {
        MTransport->SetVoiceSink(nullptr, nullptr);
    }
    if (MVoice != nullptr)
    {
        MVoice->SetSendSink(nullptr, nullptr);
    }
    MAdded.fill(false);
    MVoice = nullptr;
    MTransport = nullptr;
    MSession = nullptr;
    MVoiceSession = nullptr;
    MLink = Net::NetLink{};
    MPeer = Net::NetAddress{};
    MRemotePlayer = 0xFF;
    MLoopback = false;
    MLocalPlayer = 0xFF;
    ResetRateBuckets();
    MRateLimited = 0;
    MRateNowSeconds = 0.0f;
}

void VoiceNetworkBridge::SetRateLimit(std::uint32_t InBurst, std::uint32_t InPerSecond) noexcept
{
    MRateBurst = static_cast<float>(InBurst);
    MRatePerSecond = static_cast<float>(InPerSecond);
    ResetRateBuckets();
}

void VoiceNetworkBridge::ResetRateBuckets() noexcept
{
    for (RateBucket& ScopedBucket : MRateBuckets)
    {
        ScopedBucket = RateBucket{};
    }
}

bool VoiceNetworkBridge::AllowPacket(std::uint8_t InPlayer) noexcept
{
    if (MRateBurst <= 0.0f || InPlayer >= kVoiceMaxPeers)
    {
        return true;
    }
    RateBucket& ScopedBucket = MRateBuckets[InPlayer];
    if (!ScopedBucket.Started)
    {
        ScopedBucket.Tokens = MRateBurst;
        ScopedBucket.LastRefillSeconds = MRateNowSeconds;
        ScopedBucket.Started = true;
    }
    const float ScopedElapsed = MRateNowSeconds > ScopedBucket.LastRefillSeconds
                                    ? MRateNowSeconds - ScopedBucket.LastRefillSeconds
                                    : 0.0f;
    ScopedBucket.Tokens = std::min(MRateBurst, ScopedBucket.Tokens + ScopedElapsed * MRatePerSecond);
    ScopedBucket.LastRefillSeconds = MRateNowSeconds;
    if (ScopedBucket.Tokens < 1.0f)
    {
        ++MRateLimited;
        return false;
    }
    ScopedBucket.Tokens -= 1.0f;
    return true;
}

void VoiceNetworkBridge::SyncParticipants() noexcept
{
    if (MVoice == nullptr || MSession == nullptr || MVoiceSession == nullptr)
    {
        return;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < Net::kSessionMaxPlayers; ++ScopedIndex)
    {
        const Net::NetPlayerSlot& ScopedSlot = MSession->Slot(ScopedIndex);
        if (!ScopedSlot.Used || ScopedSlot.Id == MLocalPlayer || ScopedSlot.Id >= kVoiceMaxPeers)
        {
            continue;
        }
        if (MAdded[ScopedSlot.Id])
        {
            continue;
        }
        if (MVoice->Channel().AddParticipant(ScopedSlot.Id))
        {
            (void)MVoiceSession->AddParticipant(ScopedSlot.Id);
            MAdded[ScopedSlot.Id] = true;
        }
    }
}

void VoiceNetworkBridge::Tick(float InDeltaSeconds) noexcept
{
    if (MVoice == nullptr || MVoiceSession == nullptr)
    {
        return;
    }
    MRateNowSeconds += std::clamp(InDeltaSeconds, 0.0f, 1.0f);
    SyncParticipants();
    MVoice->Tick(InDeltaSeconds, *MVoiceSession);
}

bool VoiceNetworkBridge::SendThunk(void* InUser, std::uint8_t InToPlayer,
                                   std::span<const std::uint8_t> InPacket) noexcept
{
    auto* ScopedBridge = static_cast<VoiceNetworkBridge*>(InUser);
    if (ScopedBridge == nullptr)
    {
        return false;
    }
    if (ScopedBridge->MLoopback)
    {
        // One peer per side: relay, mesh and broadcast all resolve to it.
        (void)InToPlayer;
        return ScopedBridge->MLink.Send(
            ScopedBridge->MPeer,
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(InPacket.data()), InPacket.size()));
    }
    if (ScopedBridge->MTransport == nullptr || ScopedBridge->MSession == nullptr)
    {
        return false;
    }
    Net::NetAddress ScopedTarget{};
    if (InToPlayer == 0xFF)
    {
        ScopedTarget = ScopedBridge->MSession->Remote();
    }
    else
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < Net::kSessionMaxPlayers; ++ScopedIndex)
        {
            const Net::NetPlayerSlot& ScopedSlot = ScopedBridge->MSession->Slot(ScopedIndex);
            if (ScopedSlot.Used && ScopedSlot.Id == InToPlayer && ScopedSlot.Address.Port != 0)
            {
                ScopedTarget = ScopedSlot.Address;
                break;
            }
        }
    }
    if (ScopedTarget.Port == 0)
    {
        return false;
    }
    const Net::NetSession::MessageSendFn ScopedHook = ScopedBridge->MTransport->SendHook();
    if (ScopedHook == nullptr)
    {
        return false;
    }
    return ScopedHook(ScopedBridge->MTransport, InToPlayer, ScopedTarget,
                      std::span<const std::byte>(reinterpret_cast<const std::byte*>(InPacket.data()),
                                                 InPacket.size()));
}

void VoiceNetworkBridge::ReceiveThunk(void* InUser, const Net::NetAddress& InFrom,
                                      std::span<const std::byte> InPayload) noexcept
{
    auto* ScopedBridge = static_cast<VoiceNetworkBridge*>(InUser);
    if (ScopedBridge == nullptr || ScopedBridge->MVoice == nullptr || InPayload.size() < 12)
    {
        return;
    }
    (void)InFrom;
    const auto* ScopedBytes = reinterpret_cast<const std::uint8_t*>(InPayload.data());
    const std::uint8_t ScopedSpeaker = ScopedBytes[6];
    // A flooding speaker is cut off here, before the channel can decode or relay
    // the packet.
    if (!ScopedBridge->AllowPacket(ScopedSpeaker))
    {
        return;
    }
    (void)ScopedBridge->MVoice->Channel().ReceivePacket(
        ScopedSpeaker, std::span<const std::uint8_t>(ScopedBytes, InPayload.size()));
}
}
