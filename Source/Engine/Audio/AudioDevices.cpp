// FemboyEngine2D - audio device enumeration and capture monitor implementation.
#include "Engine/Audio/AudioDevices.hpp"

#include "miniaudio.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace Fby
{
namespace
{
static_assert(sizeof(ma_device_id) <= kAudioRawIdBytes, "raw device id storage is too small");

[[nodiscard]] std::uint64_t Fnv1a(const std::uint8_t* InBytes, std::size_t InCount) noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (std::size_t ScopedIndex = 0; ScopedIndex < InCount; ++ScopedIndex)
    {
        ScopedHash ^= InBytes[ScopedIndex];
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

void FormatId(const ma_device_id& InId, char* OutId) noexcept
{
    const std::uint64_t ScopedHash = Fnv1a(reinterpret_cast<const std::uint8_t*>(&InId), sizeof(InId));
    std::snprintf(OutId, kAudioDeviceIdChars, "%016llx", static_cast<unsigned long long>(ScopedHash));
}

void CopyName(const char* InName, char* OutName) noexcept
{
    std::memset(OutName, 0, kAudioDeviceNameChars);
    if (InName == nullptr)
    {
        return;
    }
    std::snprintf(OutName, kAudioDeviceNameChars, "%s", InName);
}

}

std::int32_t ResolveAudioDeviceIndex(const AudioDeviceList& InList, const char* InPreferredId) noexcept
{
    if (InList.Count == 0)
    {
        return -1;
    }
    if (InPreferredId != nullptr && InPreferredId[0] != '\0')
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < InList.Count; ++ScopedIndex)
        {
            if (std::strcmp(InList.Devices[ScopedIndex].Id, InPreferredId) == 0)
            {
                return static_cast<std::int32_t>(ScopedIndex);
            }
        }
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InList.Count; ++ScopedIndex)
    {
        if (InList.Devices[ScopedIndex].Default)
        {
            return static_cast<std::int32_t>(ScopedIndex);
        }
    }
    return 0;
}

std::int32_t ResolveAudioDeviceIndexByRaw(const AudioDeviceList& InList,
                                          std::span<const std::uint8_t> InRawId) noexcept
{
    if (InRawId.empty())
    {
        return -1;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InList.Count; ++ScopedIndex)
    {
        const AudioDeviceInfo& ScopedInfo = InList.Devices[ScopedIndex];
        if (ScopedInfo.RawIdSize == InRawId.size() && ScopedInfo.RawIdSize > 0 &&
            std::memcmp(ScopedInfo.RawId, InRawId.data(), InRawId.size()) == 0)
        {
            return static_cast<std::int32_t>(ScopedIndex);
        }
    }
    return -1;
}

std::uint32_t AudioDeviceIdCollisions(const AudioDeviceList& InList, const char* InId) noexcept
{
    if (InId == nullptr || InId[0] == '\0')
    {
        return 0;
    }
    std::uint32_t ScopedCount = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < InList.Count; ++ScopedIndex)
    {
        ScopedCount += std::strcmp(InList.Devices[ScopedIndex].Id, InId) == 0 ? 1u : 0u;
    }
    return ScopedCount;
}

AudioDeviceEnumerator::~AudioDeviceEnumerator()
{
    Shutdown();
}

bool AudioDeviceEnumerator::Initialize(std::string* InError) noexcept
{
    Shutdown();
    auto* ScopedContext = new (std::nothrow) ma_context{};
    if (ScopedContext == nullptr)
    {
        if (InError != nullptr)
        {
            *InError = "audio: cannot allocate the device context";
        }
        return false;
    }
    if (ma_context_init(nullptr, 0, nullptr, ScopedContext) != MA_SUCCESS)
    {
        delete ScopedContext;
        if (InError != nullptr)
        {
            *InError = "audio: no usable device backend";
        }
        return false;
    }
    MContext = ScopedContext;
    MReady = true;
    (void)Refresh();
    return true;
}

void AudioDeviceEnumerator::Shutdown() noexcept
{
    if (MContext != nullptr)
    {
        auto* ScopedContext = static_cast<ma_context*>(MContext);
        ma_context_uninit(ScopedContext);
        delete ScopedContext;
        MContext = nullptr;
    }
    MInputs = AudioDeviceList{};
    MOutputs = AudioDeviceList{};
    MRawIds = {};
    MLastHash = 0;
    MSerial = 0;
    MReady = false;
}

bool AudioDeviceEnumerator::Refresh() noexcept
{
    if (!MReady || MContext == nullptr)
    {
        return false;
    }
    auto* ScopedContext = static_cast<ma_context*>(MContext);
    ma_device_info* ScopedPlayback = nullptr;
    ma_device_info* ScopedCapture = nullptr;
    ma_uint32 ScopedPlaybackCount = 0;
    ma_uint32 ScopedCaptureCount = 0;
    if (ma_context_get_devices(ScopedContext, &ScopedPlayback, &ScopedPlaybackCount, &ScopedCapture,
                               &ScopedCaptureCount) != MA_SUCCESS)
    {
        return false;
    }

    AudioDeviceList ScopedInputs{};
    AudioDeviceList ScopedOutputs{};
    std::array<std::array<std::uint8_t, kAudioRawIdBytes>, kMaxAudioDevices * 2> ScopedRawIds{};

    const ma_uint32 ScopedInputCount = ScopedCaptureCount < kMaxAudioDevices ? ScopedCaptureCount : kMaxAudioDevices;
    for (ma_uint32 ScopedIndex = 0; ScopedIndex < ScopedInputCount; ++ScopedIndex)
    {
        AudioDeviceInfo& ScopedInfo = ScopedInputs.Devices[ScopedIndex];
        CopyName(ScopedCapture[ScopedIndex].name, ScopedInfo.Name);
        FormatId(ScopedCapture[ScopedIndex].id, ScopedInfo.Id);
        ScopedInfo.IdHash = Fnv1a(reinterpret_cast<const std::uint8_t*>(&ScopedCapture[ScopedIndex].id),
                                  sizeof(ma_device_id));
        std::memcpy(ScopedInfo.RawId, &ScopedCapture[ScopedIndex].id, sizeof(ma_device_id));
        ScopedInfo.RawIdSize = static_cast<std::uint16_t>(sizeof(ma_device_id));
        ScopedInfo.Default = ScopedCapture[ScopedIndex].isDefault != 0;
        std::memcpy(ScopedRawIds[ScopedIndex].data(), &ScopedCapture[ScopedIndex].id, sizeof(ma_device_id));
    }
    ScopedInputs.Count = ScopedInputCount;

    const ma_uint32 ScopedOutputCount = ScopedPlaybackCount < kMaxAudioDevices ? ScopedPlaybackCount : kMaxAudioDevices;
    for (ma_uint32 ScopedIndex = 0; ScopedIndex < ScopedOutputCount; ++ScopedIndex)
    {
        AudioDeviceInfo& ScopedInfo = ScopedOutputs.Devices[ScopedIndex];
        CopyName(ScopedPlayback[ScopedIndex].name, ScopedInfo.Name);
        FormatId(ScopedPlayback[ScopedIndex].id, ScopedInfo.Id);
        ScopedInfo.IdHash = Fnv1a(reinterpret_cast<const std::uint8_t*>(&ScopedPlayback[ScopedIndex].id),
                                  sizeof(ma_device_id));
        std::memcpy(ScopedInfo.RawId, &ScopedPlayback[ScopedIndex].id, sizeof(ma_device_id));
        ScopedInfo.RawIdSize = static_cast<std::uint16_t>(sizeof(ma_device_id));
        ScopedInfo.Default = ScopedPlayback[ScopedIndex].isDefault != 0;
        std::memcpy(ScopedRawIds[kMaxAudioDevices + ScopedIndex].data(), &ScopedPlayback[ScopedIndex].id,
                    sizeof(ma_device_id));
    }
    ScopedOutputs.Count = ScopedOutputCount;

    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedInputs.Count; ++ScopedIndex)
    {
        ScopedHash = Fnv1a(reinterpret_cast<const std::uint8_t*>(ScopedInputs.Devices[ScopedIndex].Id),
                           std::strlen(ScopedInputs.Devices[ScopedIndex].Id)) ^ ScopedHash;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedOutputs.Count; ++ScopedIndex)
    {
        ScopedHash = Fnv1a(reinterpret_cast<const std::uint8_t*>(ScopedOutputs.Devices[ScopedIndex].Id),
                           std::strlen(ScopedOutputs.Devices[ScopedIndex].Id)) ^ ScopedHash;
    }
    ScopedHash ^= static_cast<std::uint64_t>(ScopedInputs.Count) * 131ull;
    ScopedHash ^= static_cast<std::uint64_t>(ScopedOutputs.Count) * 137ull;

    const bool ScopedChanged = ScopedHash != MLastHash;
    MLastHash = ScopedHash;
    MInputs = ScopedInputs;
    MOutputs = ScopedOutputs;
    MRawIds = ScopedRawIds;
    if (ScopedChanged)
    {
        ++MSerial;
    }
    return ScopedChanged;
}

const AudioDeviceList& AudioDeviceEnumerator::Devices(AudioDeviceKind InKind) const noexcept
{
    return InKind == AudioDeviceKind::Input ? MInputs : MOutputs;
}

std::int32_t AudioDeviceEnumerator::FindIndex(AudioDeviceKind InKind, const char* InId) const noexcept
{
    const AudioDeviceList& ScopedList = Devices(InKind);
    if (InId == nullptr || InId[0] == '\0')
    {
        return -1;
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedList.Count; ++ScopedIndex)
    {
        if (std::strcmp(ScopedList.Devices[ScopedIndex].Id, InId) == 0)
        {
            return static_cast<std::int32_t>(ScopedIndex);
        }
    }
    return -1;
}

std::int32_t AudioDeviceEnumerator::Resolve(AudioDeviceKind InKind, const char* InPreferredId) const noexcept
{
    return ResolveAudioDeviceIndex(Devices(InKind), InPreferredId);
}

std::size_t AudioDeviceEnumerator::RawDeviceIdSize() const noexcept
{
    return sizeof(ma_device_id);
}

const std::uint8_t* AudioDeviceEnumerator::RawDeviceId(AudioDeviceKind InKind, std::uint32_t InIndex) const noexcept
{
    if (InIndex >= kMaxAudioDevices)
    {
        return nullptr;
    }
    const std::uint32_t ScopedOffset = InKind == AudioDeviceKind::Input ? 0 : kMaxAudioDevices;
    return MRawIds[ScopedOffset + InIndex].data();
}
}
