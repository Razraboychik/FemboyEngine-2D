// FemboyEngine2D - bridge from the voice runtime to the secure session transport.
#pragma once

#include "Engine/Audio/VoiceSystem.hpp"
#include "Engine/Network/NetLink.hpp"
#include "Engine/Network/SecureSessionTransport.hpp"
#include "Engine/Network/Session.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace Fby
{

// Keeps VoiceChannel/VoiceSession participants in sync with the session roster
// and carries voice datagrams through the encrypted session transport.
class VoiceNetworkBridge
{
public:
    VoiceNetworkBridge() = default;

    [[nodiscard]] bool Start(VoiceSystem& InVoice, Net::SecureSessionTransport& InTransport,
                             Net::NetSession& InSession, VoiceSession& InVoiceSession,
                             std::uint8_t InLocalPlayer) noexcept;
    // Loopback voice link for PIE: no encrypted transport, one peer. The caller
    // polls Link() with ReceiveThunk, so one endpoint pair carries both
    // directions. `InRemotePlayer` is pre-added so packets pass before the
    // session roster has a slot.
    [[nodiscard]] bool StartLoopback(VoiceSystem& InVoice, Net::NetSession& InSession,
                                     VoiceSession& InVoiceSession, std::uint8_t InLocalPlayer,
                                     std::uint8_t InRemotePlayer, Net::NetLink InLink,
                                     const Net::NetAddress& InPeer) noexcept;
    void Stop() noexcept;
    void SyncParticipants() noexcept;
    void Tick(float InDeltaSeconds) noexcept;

    // Per-speaker token bucket for received voice packets. A flood is dropped
    // before the channel sees it, so a relay never forwards it. Burst 0 disables
    // the limit; Tick() refills the buckets.
    void SetRateLimit(std::uint32_t InBurst, std::uint32_t InPerSecond) noexcept;
    [[nodiscard]] std::uint64_t PacketsRateLimited() const noexcept
    {
        return MRateLimited;
    }

    [[nodiscard]] Net::NetLink Link() const noexcept
    {
        return MLink;
    }
    [[nodiscard]] bool Active() const noexcept
    {
        return MVoice != nullptr;
    }

    static bool SendThunk(void* InUser, std::uint8_t InToPlayer,
                          std::span<const std::uint8_t> InPacket) noexcept;
    static void ReceiveThunk(void* InUser, const Net::NetAddress& InFrom,
                             std::span<const std::byte> InPayload) noexcept;

protected:
    struct RateBucket
    {
        float Tokens = 0.0f;
        float LastRefillSeconds = 0.0f;
        bool Started = false;
    };

    [[nodiscard]] bool AllowPacket(std::uint8_t InPlayer) noexcept;
    void ResetRateBuckets() noexcept;

    VoiceSystem* MVoice = nullptr;
    Net::SecureSessionTransport* MTransport = nullptr;
    Net::NetSession* MSession = nullptr;
    VoiceSession* MVoiceSession = nullptr;
    // Loopback mode: the one peer voice packets travel to and arrive from.
    Net::NetLink MLink{};
    Net::NetAddress MPeer{};
    std::uint8_t MRemotePlayer = 0xFF;
    bool MLoopback = false;
    std::uint8_t MLocalPlayer = 0xFF;
    std::array<bool, kVoiceMaxPeers> MAdded{};
    std::array<RateBucket, kVoiceMaxPeers> MRateBuckets{};
    float MRateBurst = 64.0f;
    float MRatePerSecond = 64.0f;
    float MRateNowSeconds = 0.0f;
    std::uint64_t MRateLimited = 0;
};
}
