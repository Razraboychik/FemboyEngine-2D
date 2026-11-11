// FemboyEngine2D - replay session: recorded frames, keyframes and verification.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace Fby::Core
{

inline constexpr std::uint32_t kReplayVersion = 1;

struct ReplayKeyframe
{
    std::uint32_t Tick = 0;
    std::uint64_t StateHash = 0;
};

// A recorded session: one payload per tick plus periodic keyframes. The container
// stores the bytes a tick needs (input/commands) and the state hash at keyframes,
// so a replay can prove it reproduced the same simulation.
class ReplaySession
{
public:
    void BeginRecord(std::uint32_t InKeyframeInterval = 60) noexcept
    {
        MRecording = true;
        MKeyframeInterval = InKeyframeInterval == 0 ? 1u : InKeyframeInterval;
        MFrames.clear();
        MKeyframes.clear();
        MStateHash = 1469598103934665603ull;
    }

    void EndRecord() noexcept
    {
        MRecording = false;
    }

    [[nodiscard]] bool IsRecording() const noexcept
    {
        return MRecording;
    }

    // Appends one tick. The keyframe (tick + state hash) is stored every interval.
    [[nodiscard]] bool RecordFrame(std::uint32_t InTick, std::span<const std::byte> InPayload,
                                   std::uint64_t InStateHash)
    {
        if (!MRecording)
        {
            return false;
        }
        ReplayFrame ScopedFrame;
        ScopedFrame.Tick = InTick;
        ScopedFrame.Payload.assign(InPayload.begin(), InPayload.end());
        MFrames.push_back(std::move(ScopedFrame));
        MStateHash = HashBytes(MStateHash, InPayload);
        if (MKeyframes.empty() || InTick >= MKeyframes.back().Tick + MKeyframeInterval)
        {
            MKeyframes.push_back(ReplayKeyframe{InTick, InStateHash});
        }
        return true;
    }

    [[nodiscard]] std::uint32_t FrameCount() const noexcept
    {
        return static_cast<std::uint32_t>(MFrames.size());
    }
    [[nodiscard]] std::uint32_t KeyframeCount() const noexcept
    {
        return static_cast<std::uint32_t>(MKeyframes.size());
    }
    [[nodiscard]] std::span<const ReplayKeyframe> Keyframes() const noexcept
    {
        return MKeyframes;
    }
    [[nodiscard]] std::uint64_t RollupHash() const noexcept
    {
        return MStateHash;
    }

    // Payload recorded for a tick, or an empty span when the tick is not recorded.
    [[nodiscard]] std::span<const std::byte> PayloadAtTick(std::uint32_t InTick) const noexcept
    {
        for (const ReplayFrame& ScopedFrame : MFrames)
        {
            if (ScopedFrame.Tick == InTick)
            {
                return ScopedFrame.Payload;
            }
        }
        return {};
    }

    // A keyframe matches only when the replay reached the same state hash.
    [[nodiscard]] bool VerifyKeyframe(std::uint32_t InTick, std::uint64_t InStateHash) const noexcept
    {
        for (const ReplayKeyframe& ScopedKeyframe : MKeyframes)
        {
            if (ScopedKeyframe.Tick == InTick)
            {
                return ScopedKeyframe.StateHash == InStateHash;
            }
        }
        return false;
    }

    [[nodiscard]] bool Save(std::vector<std::byte>* OutBytes) const
    {
        if (OutBytes == nullptr)
        {
            return false;
        }
        std::size_t ScopedSize = 32;
        for (const ReplayFrame& ScopedFrame : MFrames)
        {
            ScopedSize += 8 + ScopedFrame.Payload.size();
        }
        ScopedSize += MKeyframes.size() * sizeof(ReplayKeyframe);
        OutBytes->assign(ScopedSize, std::byte{0});
        std::uint8_t* ScopedData = reinterpret_cast<std::uint8_t*>(OutBytes->data());
        std::memcpy(ScopedData, "FBYRPLY1", 8);
        const std::uint32_t ScopedFrameCount = FrameCount();
        const std::uint32_t ScopedKeyCount = KeyframeCount();
        std::memcpy(ScopedData + 8, &kReplayVersion, 4);
        std::memcpy(ScopedData + 12, &MKeyframeInterval, 4);
        std::memcpy(ScopedData + 16, &ScopedFrameCount, 4);
        std::memcpy(ScopedData + 20, &ScopedKeyCount, 4);
        std::memcpy(ScopedData + 24, &MStateHash, 8);
        std::size_t ScopedOffset = 32;
        for (const ReplayKeyframe& ScopedKeyframe : MKeyframes)
        {
            std::memcpy(ScopedData + ScopedOffset, &ScopedKeyframe, sizeof(ScopedKeyframe));
            ScopedOffset += sizeof(ScopedKeyframe);
        }
        for (const ReplayFrame& ScopedFrame : MFrames)
        {
            const std::uint32_t ScopedPayloadSize = static_cast<std::uint32_t>(ScopedFrame.Payload.size());
            std::memcpy(ScopedData + ScopedOffset, &ScopedFrame.Tick, 4);
            std::memcpy(ScopedData + ScopedOffset + 4, &ScopedPayloadSize, 4);
            ScopedOffset += 8;
            if (ScopedPayloadSize > 0)
            {
                std::memcpy(ScopedData + ScopedOffset, ScopedFrame.Payload.data(), ScopedPayloadSize);
                ScopedOffset += ScopedPayloadSize;
            }
        }
        return true;
    }

    [[nodiscard]] bool Load(std::span<const std::byte> InBytes, std::string* InError = nullptr)
    {
        const auto ScopedFail = [InError](const char* InMessage)
        {
            if (InError != nullptr)
            {
                *InError = InMessage;
            }
            return false;
        };
        if (InBytes.size() < 32)
        {
            return ScopedFail("replay: truncated header");
        }
        const auto* ScopedData = reinterpret_cast<const std::uint8_t*>(InBytes.data());
        if (std::memcmp(ScopedData, "FBYRPLY1", 8) != 0)
        {
            return ScopedFail("replay: wrong magic");
        }
        std::uint32_t ScopedVersion = 0;
        std::uint32_t ScopedInterval = 0;
        std::uint32_t ScopedFrameCount = 0;
        std::uint32_t ScopedKeyCount = 0;
        std::uint64_t ScopedRollup = 0;
        std::memcpy(&ScopedVersion, ScopedData + 8, 4);
        std::memcpy(&ScopedInterval, ScopedData + 12, 4);
        std::memcpy(&ScopedFrameCount, ScopedData + 16, 4);
        std::memcpy(&ScopedKeyCount, ScopedData + 20, 4);
        std::memcpy(&ScopedRollup, ScopedData + 24, 8);
        if (ScopedVersion != kReplayVersion)
        {
            return ScopedFail("replay: unsupported version");
        }
        std::size_t ScopedOffset = 32;
        const std::size_t ScopedKeyBytes = static_cast<std::size_t>(ScopedKeyCount) * sizeof(ReplayKeyframe);
        if (InBytes.size() < ScopedOffset + ScopedKeyBytes)
        {
            return ScopedFail("replay: truncated keyframes");
        }
        std::vector<ReplayKeyframe> ScopedKeyframes(ScopedKeyCount);
        if (ScopedKeyBytes > 0)
        {
            std::memcpy(ScopedKeyframes.data(), ScopedData + ScopedOffset, ScopedKeyBytes);
        }
        ScopedOffset += ScopedKeyBytes;
        // Every frame needs at least an 8-byte header. Reject a header count that
        // cannot fit before reserve() allocates on its behalf.
        const std::size_t ScopedMaxFrames = (InBytes.size() - ScopedOffset) / 8u;
        if (ScopedFrameCount > ScopedMaxFrames)
        {
            return ScopedFail("replay: frame count exceeds its payload");
        }
        std::vector<ReplayFrame> ScopedFrames;
        ScopedFrames.reserve(ScopedFrameCount);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedFrameCount; ++ScopedIndex)
        {
            if (InBytes.size() < ScopedOffset + 8)
            {
                return ScopedFail("replay: truncated frame header");
            }
            std::uint32_t ScopedTick = 0;
            std::uint32_t ScopedPayloadSize = 0;
            std::memcpy(&ScopedTick, ScopedData + ScopedOffset, 4);
            std::memcpy(&ScopedPayloadSize, ScopedData + ScopedOffset + 4, 4);
            ScopedOffset += 8;
            if (InBytes.size() < ScopedOffset + ScopedPayloadSize)
            {
                return ScopedFail("replay: truncated frame payload");
            }
            ReplayFrame ScopedFrame;
            ScopedFrame.Tick = ScopedTick;
            ScopedFrame.Payload.resize(ScopedPayloadSize);
            if (ScopedPayloadSize > 0)
            {
                std::memcpy(ScopedFrame.Payload.data(), ScopedData + ScopedOffset, ScopedPayloadSize);
            }
            ScopedOffset += ScopedPayloadSize;
            ScopedFrames.push_back(std::move(ScopedFrame));
        }
        if (ScopedOffset != InBytes.size())
        {
            return ScopedFail("replay: trailing bytes");
        }
        MFrames = std::move(ScopedFrames);
        MKeyframes = std::move(ScopedKeyframes);
        MKeyframeInterval = ScopedInterval == 0 ? 1u : ScopedInterval;
        MStateHash = ScopedRollup;
        MRecording = false;
        return true;
    }

private:
    struct ReplayFrame
    {
        std::uint32_t Tick = 0;
        std::vector<std::byte> Payload;
    };

    [[nodiscard]] static std::uint64_t HashBytes(std::uint64_t InSeed, std::span<const std::byte> InBytes) noexcept
    {
        std::uint64_t ScopedHash = InSeed;
        for (const std::byte ScopedByte : InBytes)
        {
            ScopedHash ^= static_cast<std::uint64_t>(ScopedByte);
            ScopedHash *= 1099511628211ull;
        }
        return ScopedHash;
    }

    bool MRecording = false;
    std::uint32_t MKeyframeInterval = 60;
    std::uint64_t MStateHash = 0;
    std::vector<ReplayFrame> MFrames;
    std::vector<ReplayKeyframe> MKeyframes;
};

}
