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
    MCaptureRack = std::make_unique<VoiceEffectRack>();
    MParticipantRacks = std::make_unique<std::array<VoiceEffectRack, kVoiceMaxPeers>>();
    MCaptureRack->Configure(InConfig.UplinkEffects, 48000);
    for (std::uint8_t ScopedPlayer = 0; ScopedPlayer < kVoiceMaxPeers; ++ScopedPlayer)
    {
        (*MParticipantRacks)[ScopedPlayer].Configure(VoiceEffectProfile{}, 48000);
    }
    MChannel.SetDecodeSink(&VoiceSystem::DecodeSinkThunk, this);
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
    MCaptureRack.reset();
    MParticipantRacks.reset();
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

void VoiceSystem::SetUplinkEffects(const VoiceEffectProfile& InProfile) noexcept
{
    if (MCaptureRack == nullptr)
    {
        return;
    }
    MCaptureRack->Configure(InProfile, 48000);
}

void VoiceSystem::SetParticipantEffects(std::uint8_t InPlayer, const VoiceEffectProfile& InProfile) noexcept
{
    if (InPlayer >= kVoiceMaxPeers || MParticipantRacks == nullptr)
    {
        return;
    }
    (*MParticipantRacks)[InPlayer].Configure(InProfile, 48000);
}

void VoiceSystem::SetLocalCondition(const VoiceConditionState& InState) noexcept
{
    if (MCaptureRack != nullptr)
    {
        MCaptureRack->SetConditionState(InState);
    }
}

void VoiceSystem::SetParticipantCondition(std::uint8_t InPlayer, const VoiceConditionState& InState) noexcept
{
    if (InPlayer < kVoiceMaxPeers && MParticipantRacks != nullptr)
    {
        (*MParticipantRacks)[InPlayer].SetConditionState(InState);
    }
}

const VoiceConditionOutput* VoiceSystem::ParticipantCondition(std::uint8_t InPlayer) const noexcept
{
    if (InPlayer >= kVoiceMaxPeers || MParticipantRacks == nullptr)
    {
        return nullptr;
    }
    return &(*MParticipantRacks)[InPlayer].Condition();
}

void VoiceSystem::AdvanceEffects(float InDeltaSeconds) noexcept
{
    if (MCaptureRack != nullptr)
    {
        MCaptureRack->Advance(InDeltaSeconds);
    }
    if (MParticipantRacks != nullptr)
    {
        for (VoiceEffectRack& ScopedRack : *MParticipantRacks)
        {
            ScopedRack.Advance(InDeltaSeconds);
        }
    }
}

void VoiceSystem::DecodeSinkThunk(void* InUser, std::uint8_t InPlayer, float* InSamples,
                                  std::uint32_t InFrameCount) noexcept
{
    auto* ScopedVoice = static_cast<VoiceSystem*>(InUser);
    if (ScopedVoice != nullptr && ScopedVoice->MParticipantRacks != nullptr && InPlayer < kVoiceMaxPeers)
    {
        (*ScopedVoice->MParticipantRacks)[InPlayer].Process(InSamples, InFrameCount);
    }
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
    const std::uint32_t ScopedWanted = std::min<std::uint32_t>(MConfig.MaxProcessFrames, MRawRing.AvailableRead());
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
    if (MCaptureRack != nullptr)
    {
        MCaptureRack->Process(MWorkBuffer.data(), ScopedFrames);
    }
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
        if (!MChannel.IsParticipant(ScopedPlayer))
        {
            // The peer left: release its stream voice instead of leaving a
            // silent voice slot occupied until Stop().
            if (MBound[ScopedPlayer])
            {
                if (MVoices[ScopedPlayer] != 0)
                {
                    (void)MGraph->StopVoice(MVoices[ScopedPlayer]);
                    MVoices[ScopedPlayer] = 0;
                }
                MBound[ScopedPlayer] = false;
                InSession.SetParticipantVoice(ScopedPlayer, 0);
            }
            continue;
        }
        if (MBound[ScopedPlayer])
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
    AdvanceEffects(InDeltaSeconds);
    ProcessCapture();
    MChannel.Tick(InDeltaSeconds, &InSession);
    BindParticipants(InSession);
}
}
