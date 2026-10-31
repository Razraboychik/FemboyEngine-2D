// FemboyEngine2D - .fanim compressed animation clip asset implementation.
#include "Engine/Assets/AnimationClipAsset.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Animation::Asset
{
namespace
{
[[nodiscard]] std::uint64_t HashPayload(std::span<const std::byte> InBytes) noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (const std::byte ScopedByte : InBytes)
    {
        ScopedHash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ScopedByte));
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

void SetError(std::string* InError, const char* InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}

void SetError(std::string* InError, const std::string& InMessage)
{
    if (InError != nullptr)
    {
        *InError = InMessage;
    }
}
}

bool SaveClip(const CompressedClip& InClip, std::vector<std::byte>* OutBytes, std::string* InError)
{
    if (OutBytes == nullptr)
    {
        SetError(InError, "SaveClip: null output");
        return false;
    }
    OutBytes->clear();
    if (InClip.TrackCount > kMaxTimelineTracks || InClip.KeyCount > kMaxCompressedKeys)
    {
        SetError(InError, "SaveClip: clip tables exceed the format capacity");
        return false;
    }
    if (InClip.Duration <= 0.0f)
    {
        SetError(InError, "SaveClip: clip duration must be positive");
        return false;
    }
    for (std::uint32_t ScopedTrack = 0; ScopedTrack < InClip.TrackCount; ++ScopedTrack)
    {
        const CompressedTrack& Scoped = InClip.Tracks[ScopedTrack];
        if (static_cast<std::uint32_t>(Scoped.FirstKey) + Scoped.KeyCount > InClip.KeyCount)
        {
            SetError(InError, "SaveClip: track key range is out of bounds");
            return false;
        }
        if (Scoped.Kind >= TrackKind::Count)
        {
            SetError(InError, "SaveClip: track kind is invalid");
            return false;
        }
    }

    const std::size_t ScopedPayloadBytes =
        static_cast<std::size_t>(InClip.TrackCount) * sizeof(CompressedTrack) +
        static_cast<std::size_t>(InClip.KeyCount) * sizeof(CompressedKey);
    if (kFanmHeaderSize + ScopedPayloadBytes > kFanmMaxBytes)
    {
        SetError(InError, "SaveClip: clip payload exceeds the format size limit");
        return false;
    }

    FanmHeader ScopedHeader{};
    std::memcpy(ScopedHeader.magic, kFanmMagic.data(), kFanmMagic.size());
    ScopedHeader.version = kFanmVersion;
    ScopedHeader.headerSize = kFanmHeaderSize;
    ScopedHeader.trackCount = InClip.TrackCount;
    ScopedHeader.keyCount = InClip.KeyCount;
    ScopedHeader.duration = InClip.Duration;
    ScopedHeader.fps = InClip.Fps;
    ScopedHeader.flags = InClip.Flags;
    ScopedHeader.sourceHash = InClip.SourceHash;
    std::memcpy(ScopedHeader.name, InClip.Name, sizeof(ScopedHeader.name));

    std::vector<std::byte> ScopedPayload(ScopedPayloadBytes);
    if (InClip.TrackCount > 0)
    {
        std::memcpy(ScopedPayload.data(), InClip.Tracks.data(),
                    static_cast<std::size_t>(InClip.TrackCount) * sizeof(CompressedTrack));
    }
    if (InClip.KeyCount > 0)
    {
        std::memcpy(ScopedPayload.data() + static_cast<std::size_t>(InClip.TrackCount) * sizeof(CompressedTrack),
                    InClip.Keys.data(), static_cast<std::size_t>(InClip.KeyCount) * sizeof(CompressedKey));
    }
    ScopedHeader.contentHash = HashPayload(ScopedPayload);

    OutBytes->resize(kFanmHeaderSize + ScopedPayloadBytes, std::byte{0});
    std::memcpy(OutBytes->data(), &ScopedHeader, sizeof(ScopedHeader));
    std::memcpy(OutBytes->data() + kFanmHeaderSize, ScopedPayload.data(), ScopedPayloadBytes);
    return true;
}

bool LoadClip(std::span<const std::byte> InBytes, CompressedClip* OutClip, std::string* InError)
{
    if (OutClip == nullptr)
    {
        SetError(InError, "LoadClip: null output");
        return false;
    }
    *OutClip = CompressedClip{};
    if (InBytes.size() < kFanmHeaderSize)
    {
        SetError(InError, "LoadClip: payload is shorter than the header");
        return false;
    }
    FanmHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kFanmMagic.data(), kFanmMagic.size()) != 0)
    {
        SetError(InError, "LoadClip: bad magic (not a .fanim clip)");
        return false;
    }
    if (ScopedHeader.version != kFanmVersion || ScopedHeader.headerSize != kFanmHeaderSize)
    {
        SetError(InError, "LoadClip: unsupported version or header size");
        return false;
    }
    if (ScopedHeader.trackCount > kMaxTimelineTracks || ScopedHeader.keyCount > kMaxCompressedKeys ||
        ScopedHeader.duration <= 0.0f)
    {
        SetError(InError, "LoadClip: header counts are out of range");
        return false;
    }
    const std::size_t ScopedPayloadBytes =
        static_cast<std::size_t>(ScopedHeader.trackCount) * sizeof(CompressedTrack) +
        static_cast<std::size_t>(ScopedHeader.keyCount) * sizeof(CompressedKey);
    if (InBytes.size() != kFanmHeaderSize + ScopedPayloadBytes)
    {
        SetError(InError, "LoadClip: payload size does not match the header counts");
        return false;
    }
    const std::span<const std::byte> ScopedPayload = InBytes.subspan(kFanmHeaderSize, ScopedPayloadBytes);
    if (HashPayload(ScopedPayload) != ScopedHeader.contentHash)
    {
        SetError(InError, "LoadClip: content hash mismatch (clip is corrupted)");
        return false;
    }

    CompressedClip ScopedClip;
    ScopedClip.Duration = ScopedHeader.duration;
    ScopedClip.Fps = ScopedHeader.fps;
    ScopedClip.Flags = ScopedHeader.flags;
    ScopedClip.SourceHash = ScopedHeader.sourceHash;
    std::memcpy(ScopedClip.Name, ScopedHeader.name, sizeof(ScopedClip.Name));
    ScopedClip.TrackCount = static_cast<std::uint16_t>(ScopedHeader.trackCount);
    ScopedClip.KeyCount = static_cast<std::uint16_t>(ScopedHeader.keyCount);
    if (ScopedHeader.trackCount > 0)
    {
        std::memcpy(ScopedClip.Tracks.data(), ScopedPayload.data(),
                    static_cast<std::size_t>(ScopedHeader.trackCount) * sizeof(CompressedTrack));
    }
    if (ScopedHeader.keyCount > 0)
    {
        std::memcpy(ScopedClip.Keys.data(),
                    ScopedPayload.data() + static_cast<std::size_t>(ScopedHeader.trackCount) * sizeof(CompressedTrack),
                    static_cast<std::size_t>(ScopedHeader.keyCount) * sizeof(CompressedKey));
    }
    for (std::uint32_t ScopedTrack = 0; ScopedTrack < ScopedHeader.trackCount; ++ScopedTrack)
    {
        const CompressedTrack& Scoped = ScopedClip.Tracks[ScopedTrack];
        if (static_cast<std::uint32_t>(Scoped.FirstKey) + Scoped.KeyCount > ScopedHeader.keyCount ||
            Scoped.Kind >= TrackKind::Count || Scoped.ScaleX <= 0.0f || Scoped.ScaleY <= 0.0f)
        {
            SetError(InError, "LoadClip: track table is inconsistent");
            return false;
        }
    }
    *OutClip = ScopedClip;
    return true;
}
}
