// FemboyEngine2D - audio device id hash, raw id and collision resolution.
#include "Engine/Audio/AudioDevices.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

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

    AudioDeviceList ScopedList;
    ScopedList.Count = 2;
    std::memcpy(ScopedList.Devices[0].Id, "0123456789abcdef", 16);
    std::memcpy(ScopedList.Devices[1].Id, "0123456789abcdef", 16);
    std::memcpy(ScopedList.Devices[0].Name, "Mic A", 5);
    std::memcpy(ScopedList.Devices[1].Name, "Mic B", 5);
    ScopedList.Devices[0].IdHash = 0x1234;
    ScopedList.Devices[1].IdHash = 0x1234;
    ScopedList.Devices[0].RawId[0] = 0x0A;
    ScopedList.Devices[0].RawId[1] = 0x0B;
    ScopedList.Devices[0].RawIdSize = 2;
    ScopedList.Devices[1].RawId[0] = 0x0C;
    ScopedList.Devices[1].RawId[1] = 0x0D;
    ScopedList.Devices[1].RawIdSize = 2;

    // Both devices share the persisted hash: the hash alone is ambiguous, the
    // raw id still finds the exact device.
    FBY_CHECK(AudioDeviceIdCollisions(ScopedList, "0123456789abcdef") == 2);
    const std::uint8_t ScopedRawA[2] = {0x0A, 0x0B};
    const std::uint8_t ScopedRawB[2] = {0x0C, 0x0D};
    FBY_CHECK(ResolveAudioDeviceIndexByRaw(ScopedList, ScopedRawA) == 0);
    FBY_CHECK(ResolveAudioDeviceIndexByRaw(ScopedList, ScopedRawB) == 1);
    const std::uint8_t ScopedRawMissing[2] = {0xEE, 0xFF};
    FBY_CHECK(ResolveAudioDeviceIndexByRaw(ScopedList, ScopedRawMissing) == -1);

    // A unique id resolves the classic way; an unknown id falls back to the
    // default device (or index 0).
    ScopedList.Devices[1].Id[0] = 'f';
    FBY_CHECK(AudioDeviceIdCollisions(ScopedList, "0123456789abcdef") == 1);
    FBY_CHECK(ResolveAudioDeviceIndex(ScopedList, "0123456789abcdef") == 0);
    FBY_CHECK(ResolveAudioDeviceIndex(ScopedList, "ffffffffffffffff") == 0);

    // Empty inputs are rejected without touching the list.
    FBY_CHECK(ResolveAudioDeviceIndexByRaw(ScopedList, {}) == -1);
    FBY_CHECK(AudioDeviceIdCollisions(ScopedList, "") == 0);

    if (GFailures == 0)
    {
        std::printf("device_id_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "device_id_test: %u failure(s)\n", GFailures);
    return 1;
}
