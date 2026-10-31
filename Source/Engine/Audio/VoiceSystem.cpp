// FemboyEngine2D - voice runtime implementation.
#include "Engine/Audio/VoiceSystem.hpp"

#include <algorithm>

namespace Fby
{

VoiceSystem::~VoiceSystem()
{
    Stop();
}

bool VoiceSystem::Start(DspGraph& InGraph, FloatRing* InReferenceRing, const VoiceSystemConfig& InConfig,
                        VoiceChannel::SendFn InSend, void* InSendUser) noexcept
{
    Stop();
    MGraph = &InGraph;
    MReferenceRing = InReferenceRing;
    MConfig = InConfig;
    MDspSettings.Denoise = InConfig.Denoise;

    if (InGraph.CreateBus(kMasterBus, 1.0f, &MVoiceBus) != DspResult::Ok)
    {
        return false;
    }
    MRawRing.Configure(MRawStorage.data(), static_cast<std::uint32_t>(MRawStorage.size()), 1);
    MProcessedRing.Configure(MProcessedStorage.data(), static_cast<std::uint32_t>(MProcessedStorage.size()), 1);

    VoiceChannelConfig ScopedChannelConfig{};
    ScopedChannelConfig.Routing = InConfig.Routing;
    ScopedChannelConfig.LocalPlayer = InConfig.LocalPlayer;
    ScopedChannelConfig.RelayPlayer = InConfig.RelayPlayer;
    ScopedChannelConfig.IsRelayServer = InConfig.IsRelayServer;
    if (!MChannel.Start(ScopedChannelConfig, InSend, InSendUser))
    {
        (void)InGraph.DestroyBus(MVoiceBus);
        MVoiceBus = kInvalidBus;
        return false;
    }
    MChannel.SetCaptureRing(&MProcessedRing);

    MChain.Configure(MDspSettings, 48000);
    MAec.Configure(InConfig.Aec, 48000);
    MBound.fill(false);
    MVoices.fill(0);
    MStarted = true;
    return true;
}

void VoiceSystem::Stop() noexcept
{
    StopCapture();
    MChannel.Stop();
    if (MGraph != nullptr)
    {
        for (std::uint32_t ScopedVoice : MVoices)
        {
            if (ScopedVoice != 0)
            {
                (void)MGraph->StopVoice(ScopedVoice);
            }
        }
        if (MVoiceBus != kInvalidBus)
        {
            (void)MGraph->DestroyBus(MVoiceBus);
        }
    }
    MVoiceBus = kInvalidBus;
    MGraph = nullptr;
    MReferenceRing = nullptr;
    MBound.fill(false);
    MVoices.fill(0);
    MStarted = false;
}

void VoiceSystem::PushCaptured(std::span<const float> InMono) noexcept
{
    (void)MRawRing.Write(InMono.data(), static_cast<std::uint32_t>(InMono.size()));
}

void VoiceSystem::SetDenoiseTier(VoiceDenoiseTier InTier) noexcept
{
    MDspSettings.Denoise = InTier;
    MChain.Configure(MDspSettings, 48000);
}

void VoiceSystem::SetAec(const VoiceAecSettings& InSettings) noexcept
{
    MAec.Configure(InSettings, 48000);
}

bool VoiceSystem::StartCapture(AudioDeviceEnumerator& InDevices, const AudioDeviceInfo& InDevice,
                               std::string* OutError) noexcept
{
    (void)MCapture.Start(InDevices, InDevice.Id, &MRawRing, 48000, OutError);
    return MCapture.Active();
}

void VoiceSystem::StopCapture() noexcept
{
    MCapture.Stop();
}

bool VoiceSystem::SwitchInputDevice(AudioDeviceEnumerator& InDevices, const AudioDeviceInfo& InDevice,
                                    std::string* OutError) noexcept
{
    StopCapture();
    if (MCapture.Start(InDevices, InDevice.Id, &MRawRing, 48000, OutError))
    {
        return true;
    }
    const AudioDeviceList& ScopedInputs = InDevices.Devices(AudioDeviceKind::Input);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedInputs.Count; ++ScopedIndex)
    {
        if (MCapture.Start(InDevices, ScopedInputs.Devices[ScopedIndex].Id, &MRawRing, 48000, OutError))
        {
            return true;
        }
    }
    return false;
}

void VoiceSystem::ProcessCapture() noexcept
{
    const std::uint32_t ScopedWanted =
        std::min<std::uint32_t>(MConfig.MaxProcessFrames, MRawRing.AvailableRead());
    if (ScopedWanted == 0)
    {
        return;
    }
    const std::uint32_t ScopedFrames =
        MRawRing.Read(MWorkBuffer.data(), std::min<std::uint32_t>(ScopedWanted, MWorkBuffer.size()));
    if (ScopedFrames == 0)
    {
        return;
    }

    std::uint32_t ScopedReferenceFrames = 0;
    if (MReferenceRing != nullptr)
    {
        ScopedReferenceFrames = std::min<std::uint32_t>(ScopedFrames, MReferenceRing->AvailableRead());
        (void)MReferenceRing->Read(MReferenceBuffer.data(), ScopedReferenceFrames);
    }
    if (ScopedReferenceFrames == ScopedFrames && MConfig.Aec.Enabled)
    {
        MAec.Process(MWorkBuffer.data(), MReferenceBuffer.data(), ScopedFrames);
    }
    MChain.Process(MWorkBuffer.data(), ScopedFrames);
    (void)MProcessedRing.Write(MWorkBuffer.data(), ScopedFrames);
}

void VoiceSystem::BindParticipants(VoiceSession& InSession) noexcept
{
    if (MGraph == nullptr)
    {
        return;
    }
    for (std::uint8_t ScopedPlayer = 0; ScopedPlayer < kVoiceMaxPeers; ++ScopedPlayer)
    {
        if (MBound[ScopedPlayer] || !MChannel.IsParticipant(ScopedPlayer))
        {
            continue;
        }
        FloatRing* ScopedRing = MChannel.ParticipantRing(ScopedPlayer);
        std::uint32_t ScopedVoice = 0;
        if (ScopedRing != nullptr &&
            MGraph->PlayStream(ScopedRing, MVoiceBus, 1.0f, 0.0f, &ScopedVoice) == DspResult::Ok)
        {
            MBound[ScopedPlayer] = true;
            MVoices[ScopedPlayer] = ScopedVoice;
            InSession.SetParticipantVoice(ScopedPlayer, ScopedVoice);
        }
    }
}

void VoiceSystem::Tick(float InDeltaSeconds, VoiceSession& InSession) noexcept
{
    if (!MStarted)
    {
        return;
    }
    ProcessCapture();
    MChannel.Tick(InDeltaSeconds, &InSession);
    BindParticipants(InSession);
}
}
