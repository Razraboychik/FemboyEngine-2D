// FemboyEngine2D - voice runtime: capture, AEC, DSP, channel and mixer.
#pragma once

#include "Engine/Audio/AudioDevices.hpp"
#include "Engine/Audio/AudioStream.hpp"
#include "Engine/Audio/DspGraph.hpp"
#include "Engine/Audio/VoiceAec.hpp"
#include "Engine/Audio/VoiceChannel.hpp"
#include "Engine/Audio/VoiceDsp.hpp"
#include "Engine/Audio/VoiceSession.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace Fby
{

struct VoiceSystemConfig
{
    std::uint8_t LocalPlayer = 0;
    VoiceRouting Routing = VoiceRouting::Relay;
    std::uint8_t RelayPlayer = 0xFF;
    bool IsRelayServer = false;
    VoiceDenoiseTier Denoise = VoiceDenoiseTier::Spectral;
    VoiceAecSettings Aec{};
    // Maximum capture frames processed per Tick (keeps the frame budget).
    std::uint32_t MaxProcessFrames = 960;
};

// Single owner of the local voice path: microphone capture -> reference AEC ->
// denoise/AGC -> encoded VoiceChannel, plus decoded peer rings on the Voice bus.
class VoiceSystem
{
public:
    VoiceSystem() = default;
    ~VoiceSystem();

    VoiceSystem(const VoiceSystem&) = delete;
    VoiceSystem& operator=(const VoiceSystem&) = delete;

    [[nodiscard]] bool Start(DspGraph& InGraph, FloatRing* InReferenceRing, const VoiceSystemConfig& InConfig,
                             VoiceChannel::SendFn InSend, void* InSendUser) noexcept;
    void Stop() noexcept;

    // Pulls captured samples through AEC and the DSP chain, then runs the channel
    // (packet encode/decode) and binds new participant rings to the Voice bus.
    void Tick(float InDeltaSeconds, VoiceSession& InSession) noexcept;

    // Synthetic capture for headless hosts and tests (mono, 48 kHz).
    void PushCaptured(std::span<const float> InMono) noexcept;

    void SetSendSink(VoiceChannel::SendFn InSend, void* InUser) noexcept
    {
        MChannel.SetSendSink(InSend, InUser);
    }

    void SetDenoiseTier(VoiceDenoiseTier InTier) noexcept;
    void SetAec(const VoiceAecSettings& InSettings) noexcept;

    [[nodiscard]] VoiceChannel& Channel() noexcept
    {
        return MChannel;
    }
    [[nodiscard]] FloatRing& RawRing() noexcept
    {
        return MRawRing;
    }
    [[nodiscard]] FloatRing& ProcessedRing() noexcept
    {
        return MProcessedRing;
    }
    [[nodiscard]] bool CaptureActive() const noexcept
    {
        return MCapture.Active();
    }

    [[nodiscard]] bool StartCapture(AudioDeviceEnumerator& InDevices, const AudioDeviceInfo& InDevice,
                                    std::string* OutError = nullptr) noexcept;
    void StopCapture() noexcept;
    // Stops the current capture and switches to another input; on failure it
    // falls back to the first available input.
    [[nodiscard]] bool SwitchInputDevice(AudioDeviceEnumerator& InDevices, const AudioDeviceInfo& InDevice,
                                         std::string* OutError = nullptr) noexcept;

protected:
    void ProcessCapture() noexcept;
    void BindParticipants(VoiceSession& InSession) noexcept;

    DspGraph* MGraph = nullptr;
    BusId MVoiceBus = kInvalidBus;
    FloatRing* MReferenceRing = nullptr;
    VoiceSystemConfig MConfig{};
    VoiceChannel MChannel{};
    VoiceCapture MCapture{};
    VoiceAec MAec{};
    VoiceDspChain MChain{};
    VoiceDspSettings MDspSettings{};
    FloatRing MRawRing{};
    FloatRing MProcessedRing{};
    std::array<float, 8192> MRawStorage{};
    std::array<float, 8192> MProcessedStorage{};
    std::array<float, 2048> MWorkBuffer{};
    std::array<float, 2048> MReferenceBuffer{};
    std::array<bool, kVoiceMaxPeers> MBound{};
    std::array<std::uint32_t, kVoiceMaxPeers> MVoices{};
    bool MStarted = false;
};
}
