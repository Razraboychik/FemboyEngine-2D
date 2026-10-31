// FemboyEngine2D - voice device selection, hotplug fallback and microphone test.
#pragma once

#include "Engine/Audio/AudioDevices.hpp"

#include <cstdint>
#include <string>

namespace Fby
{
class AudioSystem;
}

namespace Fby::Editor
{

class ProjectConfigStore;

class VoiceDevicePanel
{
public:
    VoiceDevicePanel() = default;

    // `InStore` owns the project settings; the panel writes device ids into the
    // `[audio]` section and persists the project when the selection changes.
    // `InAudio` is optional; when present, "Apply output" reopens playback on
    // the selected device without restarting the editor.
    void Draw(bool* InOpen, ProjectConfigStore* InStore, AudioSystem* InAudio = nullptr);

    // Live output path without ImGui: resolves the preferred id (default when
    // empty) and reopens playback. Exposed so the panel path has a test.
    [[nodiscard]] static bool ApplyOutputDevice(AudioSystem& InAudio, AudioDeviceEnumerator& InDevices,
                                                const char* InPreferredId, std::string* InError = nullptr);

protected:
    void RefreshDevices(ProjectConfigStore* InStore, std::uint64_t InNowMs);
    void DrawDeviceCombo(AudioDeviceKind InKind, const char* InLabel, ProjectConfigStore* InStore);

    AudioDeviceEnumerator MDevices;
    AudioCaptureMonitor MMonitor;
    std::string MStatus;
    std::uint64_t MLastRefreshMs = 0;
    std::int32_t MInputIndex = -1;
    std::int32_t MOutputIndex = -1;
    bool MInitialized = false;
};
}
