// FemboyEngine2D - audio device fallback, enumeration and capture meter tests.
#include "Engine/Audio/AudioDevices.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
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
            ++GFailures;                                                                                              \
        }                                                                                                              \
    } while (false)

void AddDevice(Fby::AudioDeviceList& InList, const char* InId, const char* InName, bool InDefault)
{
    Fby::AudioDeviceInfo& ScopedDevice = InList.Devices[InList.Count++];
    std::snprintf(ScopedDevice.Id, sizeof(ScopedDevice.Id), "%s", InId);
    std::snprintf(ScopedDevice.Name, sizeof(ScopedDevice.Name), "%s", InName);
    ScopedDevice.Default = InDefault;
}

void TestFallbackPolicy()
{
    using namespace Fby;

    AudioDeviceList ScopedEmpty{};
    FBY_CHECK(ResolveAudioDeviceIndex(ScopedEmpty, nullptr) == -1);
    FBY_CHECK(ResolveAudioDeviceIndex(ScopedEmpty, "abc") == -1);

    AudioDeviceList ScopedList{};
    AddDevice(ScopedList, "aaa", "Speakers", true);
    AddDevice(ScopedList, "bbb", "Headset", false);

    FBY_CHECK(ResolveAudioDeviceIndex(ScopedList, "bbb") == 1);
    FBY_CHECK(ResolveAudioDeviceIndex(ScopedList, "missing") == 0);
    FBY_CHECK(ResolveAudioDeviceIndex(ScopedList, nullptr) == 0);
    FBY_CHECK(ResolveAudioDeviceIndex(ScopedList, "") == 0);

    AudioDeviceList ScopedNoDefault{};
    AddDevice(ScopedNoDefault, "ccc", "Mic A", false);
    AddDevice(ScopedNoDefault, "ddd", "Mic B", false);
    FBY_CHECK(ResolveAudioDeviceIndex(ScopedNoDefault, "ddd") == 1);
    FBY_CHECK(ResolveAudioDeviceIndex(ScopedNoDefault, "missing") == 0);

    AudioDevicePreferences ScopedPreferences{};
    FBY_CHECK(ScopedPreferences.InputId[0] == '\0');
    FBY_CHECK(ScopedPreferences.OutputId[0] == '\0');

    FBY_CHECK(std::strcmp(AudioDeviceStatusName(AudioDeviceStatus::PermissionDenied), "PermissionDenied") == 0);
}

void TestEnumerator()
{
    using namespace Fby;

    AudioDeviceEnumerator ScopedDevices;
    std::string ScopedError;
    if (!ScopedDevices.Initialize(&ScopedError))
    {
        std::printf("audio_devices_test: no backend (%s), enumeration skipped\n", ScopedError.c_str());
        return;
    }
    FBY_CHECK(ScopedDevices.Ready());

    (void)ScopedDevices.Refresh();
    const AudioDeviceList& ScopedInputs = ScopedDevices.Devices(AudioDeviceKind::Input);
    const AudioDeviceList& ScopedOutputs = ScopedDevices.Devices(AudioDeviceKind::Output);
    FBY_CHECK(ScopedInputs.Count <= kMaxAudioDevices);
    FBY_CHECK(ScopedOutputs.Count <= kMaxAudioDevices);

    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedInputs.Count; ++ScopedIndex)
    {
        FBY_CHECK(ScopedInputs.Devices[ScopedIndex].Id[0] != '\0');
        FBY_CHECK(std::strlen(ScopedInputs.Devices[ScopedIndex].Id) == kAudioDeviceIdChars - 1);
        FBY_CHECK(ScopedDevices.FindIndex(AudioDeviceKind::Input, ScopedInputs.Devices[ScopedIndex].Id) ==
                  static_cast<std::int32_t>(ScopedIndex));
        FBY_CHECK(ScopedDevices.RawDeviceId(AudioDeviceKind::Input, ScopedIndex) != nullptr);
    }
    if (ScopedInputs.Count > 0)
    {
        FBY_CHECK(ScopedDevices.FindIndex(AudioDeviceKind::Input, "no-such-device") == -1);
        FBY_CHECK(ScopedDevices.Resolve(AudioDeviceKind::Input, "no-such-device") >= 0);
    }
    FBY_CHECK(ScopedDevices.RefreshSerial() > 0);

    AudioCaptureMonitor ScopedMonitor;
    if (ScopedInputs.Count == 0)
    {
        FBY_CHECK(!ScopedMonitor.Start(ScopedDevices, nullptr, 48000, &ScopedError));
        FBY_CHECK(ScopedDevices.Resolve(AudioDeviceKind::Input, nullptr) == -1);
        return;
    }

    if (ScopedMonitor.Start(ScopedDevices, ScopedInputs.Devices[0].Id, 48000, &ScopedError))
    {
        FBY_CHECK(ScopedMonitor.Active());
        FBY_CHECK(ScopedMonitor.Status() == AudioDeviceStatus::Ok);
        FBY_CHECK(ScopedMonitor.Peak() >= 0.0f);
        FBY_CHECK(ScopedMonitor.Rms() >= 0.0f);
        ScopedMonitor.Stop();
        FBY_CHECK(!ScopedMonitor.Active());
    }
    else
    {
        // A machine may legitimately refuse microphone access; the failure must
        // still be a mapped status, not a crash.
        FBY_CHECK(ScopedMonitor.Status() != AudioDeviceStatus::Ok);
        FBY_CHECK(!ScopedMonitor.Active());
    }
}
}

int main()
{
    TestFallbackPolicy();
    TestEnumerator();

    if (GFailures == 0)
    {
        std::printf("audio_devices_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "audio_devices_test: %u failure(s)\n", GFailures);
    return 1;
}
