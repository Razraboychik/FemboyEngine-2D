// FemboyEngine2D - audio input/output enumeration, fallback and capture monitor.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace Fby
{

enum class AudioDeviceKind : std::uint8_t
{
    Input = 0,
    Output,
};

enum class AudioDeviceStatus : std::uint8_t
{
    Ok = 0,
    Unavailable,
    PermissionDenied,
    Busy,
    BackendMissing,
};

[[nodiscard]] constexpr const char* AudioDeviceStatusName(AudioDeviceStatus InStatus) noexcept
{
    switch (InStatus)
    {
        case AudioDeviceStatus::Ok:
            return "Ok";
        case AudioDeviceStatus::Unavailable:
            return "Unavailable";
        case AudioDeviceStatus::PermissionDenied:
            return "PermissionDenied";
        case AudioDeviceStatus::Busy:
            return "Busy";
        case AudioDeviceStatus::BackendMissing:
            return "BackendMissing";
    }
    return "?";
}

inline constexpr std::uint32_t kMaxAudioDevices = 16;
inline constexpr std::size_t kAudioDeviceIdChars = 17;
inline constexpr std::size_t kAudioDeviceNameChars = 128;
// Raw backend device ids are opaque; this is the storage bound they must fit.
inline constexpr std::size_t kAudioRawIdBytes = 512;

struct AudioDeviceInfo
{
    char Id[kAudioDeviceIdChars]{};
    char Name[kAudioDeviceNameChars]{};
    bool Default = false;
    // Full backend id next to the short hash. The hash is the persisted key;
    // the raw bytes disambiguate a hash collision and are what the backend opens.
    std::uint64_t IdHash = 0;
    std::uint8_t RawId[kAudioRawIdBytes]{};
    std::uint16_t RawIdSize = 0;
};

struct AudioDeviceList
{
    std::array<AudioDeviceInfo, kMaxAudioDevices> Devices{};
    std::uint32_t Count = 0;
};

// Persisted selection. Empty strings mean "follow the system default".
struct AudioDevicePreferences
{
    char InputId[kAudioDeviceIdChars]{};
    char OutputId[kAudioDeviceIdChars]{};
};

// Pure fallback policy: preferred id, then the system default, then the first
// available device. Returns -1 for an empty list.
[[nodiscard]] std::int32_t ResolveAudioDeviceIndex(const AudioDeviceList& InList,
                                                   const char* InPreferredId) noexcept;

// Exact match on the full backend id. Returns -1 when no device owns the bytes.
[[nodiscard]] std::int32_t ResolveAudioDeviceIndexByRaw(const AudioDeviceList& InList,
                                                        std::span<const std::uint8_t> InRawId) noexcept;

// Number of devices that share one persisted hash. More than one means the hash
// alone cannot identify the device; callers should use the raw id.
[[nodiscard]] std::uint32_t AudioDeviceIdCollisions(const AudioDeviceList& InList, const char* InId) noexcept;

// Owns one miniaudio context shared by enumeration and the capture monitor.
class AudioDeviceEnumerator
{
public:
    AudioDeviceEnumerator() = default;
    ~AudioDeviceEnumerator();

    AudioDeviceEnumerator(const AudioDeviceEnumerator&) = delete;
    AudioDeviceEnumerator& operator=(const AudioDeviceEnumerator&) = delete;

    [[nodiscard]] bool Initialize(std::string* InError = nullptr) noexcept;
    void Shutdown() noexcept;
    [[nodiscard]] bool Ready() const noexcept
    {
        return MReady;
    }

    // Re-reads the device lists. Returns true when the set or the defaults
    // changed, so a host can react to hotplug.
    [[nodiscard]] bool Refresh() noexcept;

    [[nodiscard]] const AudioDeviceList& Devices(AudioDeviceKind InKind) const noexcept;
    [[nodiscard]] std::int32_t FindIndex(AudioDeviceKind InKind, const char* InId) const noexcept;
    [[nodiscard]] std::int32_t Resolve(AudioDeviceKind InKind, const char* InPreferredId) const noexcept;
    [[nodiscard]] std::uint32_t RefreshSerial() const noexcept
    {
        return MSerial;
    }

    // Opaque handles for the capture monitor; never exposed as public types.
    [[nodiscard]] void* Context() const noexcept
    {
        return MContext;
    }
    [[nodiscard]] const std::uint8_t* RawDeviceId(AudioDeviceKind InKind, std::uint32_t InIndex) const noexcept;
    // Byte size of the raw backend id returned by RawDeviceId.
    [[nodiscard]] std::size_t RawDeviceIdSize() const noexcept;

protected:
    void* MContext = nullptr;
    AudioDeviceList MInputs{};
    AudioDeviceList MOutputs{};
    std::array<std::array<std::uint8_t, kAudioRawIdBytes>, kMaxAudioDevices * 2> MRawIds{};
    std::uint64_t MLastHash = 0;
    std::uint32_t MSerial = 0;
    bool MReady = false;
};

// Opens one capture device and meters it. This is the "test microphone" seam:
// no mixing, no network, just proof that the device opens and produces signal.
// It wraps VoiceCapture, so the engine has one capture implementation: the
// monitor adds a private ring and exposes the peak/RMS of the same callback.
class AudioCaptureMonitor
{
public:
    AudioCaptureMonitor();
    ~AudioCaptureMonitor();

    AudioCaptureMonitor(const AudioCaptureMonitor&) = delete;
    AudioCaptureMonitor& operator=(const AudioCaptureMonitor&) = delete;

    [[nodiscard]] bool Start(const AudioDeviceEnumerator& InDevices, const char* InDeviceId,
                             std::uint32_t InSampleRate = 48000, std::string* InError = nullptr) noexcept;
    void Stop() noexcept;
    [[nodiscard]] bool Active() const noexcept;
    [[nodiscard]] float Peak() const noexcept;
    [[nodiscard]] float Rms() const noexcept;
    [[nodiscard]] AudioDeviceStatus Status() const noexcept;
    // Frames dropped by the monitor's private ring (the capture drift signal).
    [[nodiscard]] std::uint64_t DroppedFrames() const noexcept;

protected:
    struct State;
    std::unique_ptr<State> MState;
};
}
