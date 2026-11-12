// FemboyEngine2D - voice device panel implementation.
#include "Editor/Panels/VoiceDevicePanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Editor/Infrastructure/ProjectConfig.hpp"

#include "Engine/Audio/Audio.hpp"
#include "Engine/Audio/VoiceDsp.hpp"
#include "Engine/Audio/VoicePreset.hpp"
#include "Engine/Audio/VoiceReactiveSfx.hpp"
#include "Engine/Audio/VoiceSession.hpp"

#include "imgui.h"

#include <cstdio>
#include <cstring>

namespace Fby::Editor
{
namespace
{
void CopyIdInto(char* OutDestination, std::size_t InCapacity, const char* InSource) noexcept
{
    if (OutDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    OutDestination[0] = '\0';
    if (InSource == nullptr)
    {
        return;
    }
    std::snprintf(OutDestination, InCapacity, "%s", InSource);
}

// Preset ids are stable data; the list shows the last id segment so a data pack
// keeps readable labels without shipping a localization table in the panel.
[[nodiscard]] const char* PresetLabel(const Fby::VoicePresetDesc& InPreset) noexcept
{
    const char* ScopedDot = std::strrchr(InPreset.Id, '.');
    return ScopedDot != nullptr ? ScopedDot + 1 : InPreset.Id;
}

[[nodiscard]] bool DrawPresetCombo(const char* InLabel, const Fby::VoicePresetRegistry& InPresets,
                                   std::uint32_t InRequiredFlag, Fby::VoicePresetChoice& InChoice) noexcept
{
    const Fby::VoicePresetDesc* ScopedCurrent = InChoice.Enabled ? InPresets.Find(InChoice.Id) : nullptr;
    bool ScopedChanged = false;
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo(InLabel, ScopedCurrent != nullptr ? PresetLabel(*ScopedCurrent) : "None"))
    {
        if (ImGui::Selectable("None", ScopedCurrent == nullptr))
        {
            InChoice.Clear();
            ScopedChanged = true;
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < InPresets.Count(); ++ScopedIndex)
        {
            const Fby::VoicePresetDesc* ScopedPreset = InPresets.At(ScopedIndex);
            if (ScopedPreset == nullptr || (ScopedPreset->Flags & InRequiredFlag) == 0 ||
                (ScopedPreset->Flags & Fby::VoicePresetFlagModeration) != 0)
            {
                continue;
            }
            if (ImGui::Selectable(PresetLabel(*ScopedPreset), ScopedCurrent == ScopedPreset))
            {
                InChoice.Set(ScopedPreset->Id, InChoice.Intensity);
                ScopedChanged = true;
            }
        }
        ImGui::EndCombo();
    }
    return ScopedChanged;
}

void DrawChoiceIntensity(Fby::VoicePresetChoice& InChoice) noexcept
{
    if (!InChoice.Enabled)
    {
        return;
    }
    ImGui::SetNextItemWidth(180.0f);
    (void)ImGui::SliderFloat("Intensity", &InChoice.Intensity, 0.0f, 1.0f, "%.2f");
}
}

void VoiceDevicePanel::RefreshDevices(ProjectConfigStore* InStore, std::uint64_t InNowMs)
{
    if (!MInitialized)
    {
        std::string ScopedError;
        if (!MDevices.Initialize(&ScopedError))
        {
            MStatus = ScopedError;
            return;
        }
        MInitialized = true;
    }
    if (InNowMs - MLastRefreshMs < 1000)
    {
        return;
    }
    MLastRefreshMs = InNowMs;
    const bool ScopedChanged = MDevices.Refresh();

    if (InStore == nullptr)
    {
        return;
    }
    ProjectSettings& ScopedProject = InStore->Project();

    // Hotplug: a stored device that disappeared falls back to the default and is
    // written back, so the next launch does not repeat the failure.
    if (ScopedProject.voiceInputDevice[0] != '\0' &&
        MDevices.FindIndex(AudioDeviceKind::Input, ScopedProject.voiceInputDevice) < 0)
    {
        const std::int32_t ScopedFallback = MDevices.Resolve(AudioDeviceKind::Input, ScopedProject.voiceInputDevice);
        if (ScopedFallback >= 0)
        {
            CopyIdInto(ScopedProject.voiceInputDevice, sizeof(ScopedProject.voiceInputDevice),
                       MDevices.Devices(AudioDeviceKind::Input).Devices[ScopedFallback].Id);
        }
        MStatus = "input device disappeared; switched to the default";
        MMonitor.Stop();
    }
    if (ScopedProject.voiceOutputDevice[0] != '\0' &&
        MDevices.FindIndex(AudioDeviceKind::Output, ScopedProject.voiceOutputDevice) < 0)
    {
        const std::int32_t ScopedFallback = MDevices.Resolve(AudioDeviceKind::Output, ScopedProject.voiceOutputDevice);
        if (ScopedFallback >= 0)
        {
            CopyIdInto(ScopedProject.voiceOutputDevice, sizeof(ScopedProject.voiceOutputDevice),
                       MDevices.Devices(AudioDeviceKind::Output).Devices[ScopedFallback].Id);
        }
        MStatus = "output device disappeared; switched to the default";
    }
    (void)ScopedChanged;
}

void VoiceDevicePanel::DrawDeviceCombo(AudioDeviceKind InKind, const char* InLabel, ProjectConfigStore* InStore)
{
    if (InStore == nullptr)
    {
        ImGui::TextDisabled("%s: no project store", InLabel);
        return;
    }
    ProjectSettings& ScopedProject = InStore->Project();
    char* ScopedStoredId =
        InKind == AudioDeviceKind::Input ? ScopedProject.voiceInputDevice : ScopedProject.voiceOutputDevice;
    const std::size_t ScopedCapacity = InKind == AudioDeviceKind::Input ? sizeof(ScopedProject.voiceInputDevice)
                                                                        : sizeof(ScopedProject.voiceOutputDevice);
    const AudioDeviceList& ScopedDevices = MDevices.Devices(InKind);

    const std::int32_t ScopedCurrent = ScopedStoredId[0] == '\0' ? 0 : 1 + MDevices.FindIndex(InKind, ScopedStoredId);
    ImGui::TextUnformatted(InLabel);
    ImGui::SetNextItemWidth(320.0f);
    if (ImGui::BeginCombo(InLabel, ScopedStoredId[0] == '\0' ? "System default" : ScopedStoredId))
    {
        if (ImGui::Selectable("System default", ScopedCurrent == 0))
        {
            ScopedStoredId[0] = '\0';
            (void)InStore->PersistIfChanged(nullptr);
        }
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedDevices.Count; ++ScopedIndex)
        {
            const bool ScopedSelected = ScopedCurrent == static_cast<std::int32_t>(ScopedIndex) + 1;
            ImGui::PushID(static_cast<int>(ScopedIndex));
            if (ImGui::Selectable(ScopedDevices.Devices[ScopedIndex].Name, ScopedSelected))
            {
                CopyIdInto(ScopedStoredId, ScopedCapacity, ScopedDevices.Devices[ScopedIndex].Id);
                (void)InStore->PersistIfChanged(nullptr);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
}

void VoiceDevicePanel::Draw(bool* InOpen, ProjectConfigStore* InStore, AudioSystem* InAudio, VoiceSystem* InVoice,
                            VoiceSession* InSession, VoicePresetRegistry* InPresets, VoicePresetSelection* InSelection,
                            VoiceEffectPolicy* InPolicy, VoiceReactiveSfxSettings* InSfxSettings)
{
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(560.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Voice Devices", InOpen))
    {
        EndEditorPanel();
        return;
    }

    const std::uint64_t ScopedNowMs = static_cast<std::uint64_t>(ImGui::GetTime() * 1000.0);
    RefreshDevices(InStore, ScopedNowMs);

    ImGui::TextDisabled("Inputs: %u   Outputs: %u   backend: %s",
                        static_cast<unsigned>(MDevices.Devices(AudioDeviceKind::Input).Count),
                        static_cast<unsigned>(MDevices.Devices(AudioDeviceKind::Output).Count),
                        MDevices.Ready() ? "ready" : "unavailable");
    if (ImGui::Button("Refresh devices"))
    {
        MLastRefreshMs = 0;
        (void)MDevices.Refresh();
    }
    ImGui::SameLine();
    if (!MStatus.empty())
    {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "%s", MStatus.c_str());
    }
    ImGui::Separator();

    DrawDeviceCombo(AudioDeviceKind::Input, "Microphone", InStore);
    DrawDeviceCombo(AudioDeviceKind::Output, "Output", InStore);
    (void)InVoice;
    if (InAudio != nullptr)
    {
        if (ImGui::Button("Apply output"))
        {
            const char* ScopedId = InStore != nullptr ? InStore->Project().voiceOutputDevice : "";
            std::string ScopedError;
            if (ApplyOutputDevice(*InAudio, MDevices, ScopedId, &ScopedError))
            {
                MStatus = "output device applied";
            }
            else
            {
                MStatus = ScopedError.empty() ? "output device was not reopened" : ScopedError;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Reopens playback without restarting the editor.");
    }

    if (InStore != nullptr)
    {
        MInputIndex = MDevices.Resolve(AudioDeviceKind::Input, InStore->Project().voiceInputDevice);
        MOutputIndex = MDevices.Resolve(AudioDeviceKind::Output, InStore->Project().voiceOutputDevice);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Microphone test");
    if (MMonitor.Active())
    {
        if (ImGui::Button("Stop test"))
        {
            MMonitor.Stop();
            MStatus = "microphone test stopped";
        }
        ImGui::SameLine();
        ImGui::Text("peak %.3f   rms %.3f", static_cast<double>(MMonitor.Peak()), static_cast<double>(MMonitor.Rms()));
        ImGui::ProgressBar(MMonitor.Peak(), ImVec2(-1.0f, 0.0f));
    }
    else
    {
        if (ImGui::Button("Start test"))
        {
            std::string ScopedError;
            const char* ScopedId = InStore != nullptr ? InStore->Project().voiceInputDevice : "";
            if (MMonitor.Start(MDevices, ScopedId, 48000, &ScopedError))
            {
                MStatus = "microphone test running";
            }
            else
            {
                MStatus = ScopedError.empty() ? AudioDeviceStatusName(MMonitor.Status()) : ScopedError;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Speak to see the level; no audio is sent anywhere.");
    }

    if (InStore != nullptr)
    {
        ProjectSettings& ScopedProject = InStore->Project();
        ImGui::Separator();
        ImGui::TextUnformatted("Processing");
        int ScopedTier = static_cast<int>(ScopedProject.voiceDenoise);
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::Combo("Denoise", &ScopedTier, "None\0Gate\0Spectral\0RNNoise (fallback)\0"))
        {
            ScopedProject.voiceDenoise = static_cast<std::uint8_t>(ScopedTier);
            (void)InStore->PersistIfChanged(nullptr);
        }
        float ScopedGate = ScopedProject.voiceGateDb;
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::SliderFloat("Gate dB", &ScopedGate, -90.0f, 0.0f, "%.0f"))
        {
            ScopedProject.voiceGateDb = ScopedGate;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            (void)InStore->PersistIfChanged(nullptr);
        }
        bool ScopedAgc = ScopedProject.voiceAgc;
        if (ImGui::Checkbox("AGC", &ScopedAgc))
        {
            ScopedProject.voiceAgc = ScopedAgc;
            (void)InStore->PersistIfChanged(nullptr);
        }
        bool ScopedAec = ScopedProject.voiceAec;
        if (ImGui::Checkbox("AEC (speakers leak)", &ScopedAec))
        {
            ScopedProject.voiceAec = ScopedAec;
            (void)InStore->PersistIfChanged(nullptr);
        }
        if (ScopedProject.voiceAec)
        {
            float ScopedTail = ScopedProject.voiceAecTailMs;
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::SliderFloat("AEC tail ms", &ScopedTail, 8.0f, 160.0f, "%.0f"))
            {
                ScopedProject.voiceAecTailMs = ScopedTail;
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                (void)InStore->PersistIfChanged(nullptr);
            }
            float ScopedStep = ScopedProject.voiceAecStep;
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::SliderFloat("AEC step", &ScopedStep, 0.01f, 1.0f, "%.2f"))
            {
                ScopedProject.voiceAecStep = ScopedStep;
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                (void)InStore->PersistIfChanged(nullptr);
            }
        }
        float ScopedTarget = ScopedProject.voiceAgcTargetDb;
        ImGui::SetNextItemWidth(240.0f);
        if (ImGui::SliderFloat("AGC target dBFS", &ScopedTarget, -40.0f, -3.0f, "%.0f"))
        {
            ScopedProject.voiceAgcTargetDb = ScopedTarget;
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            (void)InStore->PersistIfChanged(nullptr);
        }
        for (std::uint8_t ScopedPreset = 0; ScopedPreset < static_cast<std::uint8_t>(VoicePresetKind::Count);
             ++ScopedPreset)
        {
            if (ScopedPreset > 0)
            {
                ImGui::SameLine();
            }
            const auto ScopedKind = static_cast<VoicePresetKind>(ScopedPreset);
            if (ImGui::Button(VoicePresetName(ScopedKind)))
            {
                const VoiceDspSettings ScopedSettings = VoicePresetSettings(ScopedKind);
                ScopedProject.voiceDenoise = static_cast<std::uint8_t>(ScopedSettings.Denoise);
                ScopedProject.voiceGateDb = ScopedSettings.GateThresholdDb;
                ScopedProject.voiceAgc = ScopedSettings.AgcEnabled;
                ScopedProject.voiceAgcTargetDb = ScopedSettings.AgcTargetDbfs;
                (void)InStore->PersistIfChanged(nullptr);
            }
        }
    }

    if (InPresets != nullptr)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Voice effects");
        if (InSelection != nullptr)
        {
            (void)DrawPresetCombo("Your voice", *InPresets, VoicePresetFlagAllowUplink, InSelection->Local);
            DrawChoiceIntensity(InSelection->Local);
        }
        if (InPolicy != nullptr)
        {
            ImGui::Checkbox("Mumble when muted", &InPolicy->MumbleWhenModerated);
            ImGui::SameLine();
            ImGui::Checkbox("Allow local effects", &InPolicy->AllowLocalUplink);
        }
        if (InSfxSettings != nullptr)
        {
            ImGui::Checkbox("Reactive SFX", &InSfxSettings->Enabled);
            if (InSfxSettings->Enabled)
            {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160.0f);
                (void)ImGui::SliderFloat("SFX gain", &InSfxSettings->MasterGain, 0.0f, 1.0f, "%.2f");
            }
        }
        if (InSession != nullptr && InSelection != nullptr)
        {
            ImGui::SeparatorText("Participants");
            bool ScopedAny = false;
            for (std::uint8_t ScopedPlayer = 0; ScopedPlayer < static_cast<std::uint8_t>(kVoiceMaxPeers);
                 ++ScopedPlayer)
            {
                const VoiceParticipant* ScopedParticipant = InSession->Participant(ScopedPlayer);
                if (ScopedParticipant == nullptr || !ScopedParticipant->Present)
                {
                    continue;
                }
                ScopedAny = true;
                ImGui::PushID(static_cast<int>(ScopedPlayer));
                ImGui::Text("Player %u%s", static_cast<unsigned>(ScopedPlayer),
                            ScopedParticipant->LocalMuted ? " (muted)" : "");
                (void)DrawPresetCombo("Effect", *InPresets, VoicePresetFlagAllowDownlink,
                                      InSelection->Participants[ScopedPlayer]);
                DrawChoiceIntensity(InSelection->Participants[ScopedPlayer]);
                ImGui::PopID();
            }
            if (!ScopedAny)
            {
                ImGui::TextDisabled("No participants in the session.");
            }
        }
    }

    if (MInputIndex >= 0)
    {
        ImGui::Separator();
        ImGui::TextDisabled("resolved input: %s", MDevices.Devices(AudioDeviceKind::Input).Devices[MInputIndex].Name);
    }
    if (MOutputIndex >= 0)
    {
        ImGui::TextDisabled("resolved output: %s",
                            MDevices.Devices(AudioDeviceKind::Output).Devices[MOutputIndex].Name);
    }

    EndEditorPanel();
}

bool VoiceDevicePanel::ApplyOutputDevice(AudioSystem& InAudio, AudioDeviceEnumerator& InDevices,
                                         const char* InPreferredId, std::string* InError)
{
    if (!InDevices.Ready())
    {
        if (InError != nullptr)
        {
            *InError = "output: the audio backend is not initialized";
        }
        return false;
    }
    const std::int32_t ScopedIndex = InDevices.Resolve(AudioDeviceKind::Output, InPreferredId);
    if (ScopedIndex < 0)
    {
        if (InError != nullptr)
        {
            *InError = "output: no playback device is available";
        }
        return false;
    }
    if (!InAudio.ReopenOutput(InDevices, AudioDeviceKind::Output, static_cast<std::uint32_t>(ScopedIndex)))
    {
        if (InError != nullptr)
        {
            *InError = "output: the playback device was not reopened";
        }
        return false;
    }
    return true;
}
}
