// FemboyEngine2D - voice device panel: live output apply path.
#include "Editor/Panels/VoiceDevicePanel.hpp"

#include "Engine/Audio/Audio.hpp"

#include <cstdio>
#include <string>

namespace
{
std::uint32_t GFailures = 0;

#define FBY_CHECK(condition)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
            ++GFailures;                                                                                               \
        }                                                                                                              \
    } while (false)
}

int main()
{
    using namespace Fby;

    AudioDeviceEnumerator ScopedDevices;
    std::string ScopedError;
    if (!ScopedDevices.Initialize(&ScopedError))
    {
        std::printf("voice_device_panel_test: audio backend unavailable, skipped\n");
        return 0;
    }

    // An uninitialized mixer cannot reopen playback: the panel path must report
    // failure instead of pretending the device changed.
    AudioSystem ScopedAudio;
    FBY_CHECK(!Editor::VoiceDevicePanel::ApplyOutputDevice(ScopedAudio, ScopedDevices, "", &ScopedError));
    FBY_CHECK(!ScopedError.empty());

    // An explicit id that does not exist also fails cleanly.
    FBY_CHECK(!Editor::VoiceDevicePanel::ApplyOutputDevice(ScopedAudio, ScopedDevices, "no-such-device", &ScopedError));
    FBY_CHECK(!ScopedError.empty());

    if (GFailures == 0)
    {
        std::printf("voice_device_panel_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "voice_device_panel_test: %u failure(s)\n", GFailures);
    return 1;
}
