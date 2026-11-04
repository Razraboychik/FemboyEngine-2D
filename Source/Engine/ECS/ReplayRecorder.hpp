// FemboyEngine2D - replay recorder and playback bound to ECS snapshots.
#pragma once

#include "Engine/Core/DeterministicRng.hpp"
#include "Engine/Core/Replay.hpp"
#include "Engine/ECS/Snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Fby::Ecs
{

// FNV-1a over arbitrary bytes, used for snapshot hashes in the replay keyframes.
[[nodiscard]] inline std::uint64_t ReplayHashBytes(std::span<const std::byte> InBytes) noexcept
{
    std::uint64_t ScopedHash = 1469598103934665603ull;
    for (const std::byte ScopedByte : InBytes)
    {
        ScopedHash ^= static_cast<std::uint64_t>(ScopedByte);
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

// Records one ECS tick: the tick's commands plus a hash of the full snapshot. The
// keyframe stores the snapshot hash, so a playback can prove it reproduced the
// registry state, not just the command bytes.
class ReplayRecorder
{
public:
    [[nodiscard]] bool Begin(const SnapshotSchema& InSchema, std::uint32_t InKeyframeInterval = 60,
                             DeterministicRng InRng = {})
    {
        if (InSchema.Size() == 0)
        {
            return false;
        }
        MSchema = InSchema;
        MRng = InRng;
        MScratch.clear();
        MSession.BeginRecord(InKeyframeInterval);
        MRecording = true;
        return true;
    }

    void End() noexcept
    {
        MRecording = false;
        MSession.EndRecord();
    }

    [[nodiscard]] bool IsRecording() const noexcept
    {
        return MRecording;
    }

    [[nodiscard]] bool RecordTick(const Registry& InRegistry, std::uint32_t InTick,
                                  std::span<const std::byte> InCommands)
    {
        if (!MRecording)
        {
            return false;
        }
        const std::size_t ScopedSize = SnapshotSize(InRegistry, MSchema);
        if (ScopedSize == 0)
        {
            return false;
        }
        MScratch.resize(ScopedSize);
        std::size_t ScopedWritten = 0;
        if (!CaptureSnapshot(InRegistry, MSchema, MScratch, InTick, MRng, &ScopedWritten))
        {
            return false;
        }
        const std::uint64_t ScopedHash =
            ReplayHashBytes(std::span<const std::byte>(MScratch.data(), ScopedWritten));
        return MSession.RecordFrame(InTick, InCommands, ScopedHash);
    }

    [[nodiscard]] Core::ReplaySession& Session() noexcept
    {
        return MSession;
    }
    [[nodiscard]] std::uint32_t SnapshotBytes() const noexcept
    {
        return static_cast<std::uint32_t>(MScratch.size());
    }

private:
    SnapshotSchema MSchema{};
    DeterministicRng MRng{};
    std::vector<std::byte> MScratch;
    Core::ReplaySession MSession;
    bool MRecording = false;
};

// Loaded replay. VerifyTick captures the current registry and checks it against
// the stored keyframe hash.
class ReplayPlayback
{
public:
    [[nodiscard]] bool Load(std::span<const std::byte> InBytes, const SnapshotSchema& InSchema,
                            std::string* InError = nullptr)
    {
        if (InSchema.Size() == 0)
        {
            if (InError != nullptr)
            {
                *InError = "replay playback: empty schema";
            }
            return false;
        }
        MSchema = InSchema;
        return MSession.Load(InBytes, InError);
    }

    [[nodiscard]] std::uint32_t FrameCount() const noexcept
    {
        return MSession.FrameCount();
    }

    [[nodiscard]] std::span<const std::byte> CommandsAt(std::uint32_t InTick) const noexcept
    {
        return MSession.PayloadAtTick(InTick);
    }

    [[nodiscard]] bool VerifyTick(const Registry& InRegistry, std::uint32_t InTick, DeterministicRng InRng = {})
    {
        const std::size_t ScopedSize = SnapshotSize(InRegistry, MSchema);
        if (ScopedSize == 0)
        {
            return false;
        }
        MScratch.resize(ScopedSize);
        std::size_t ScopedWritten = 0;
        if (!CaptureSnapshot(InRegistry, MSchema, MScratch, InTick, InRng, &ScopedWritten))
        {
            return false;
        }
        const std::uint64_t ScopedHash =
            ReplayHashBytes(std::span<const std::byte>(MScratch.data(), ScopedWritten));
        return MSession.VerifyKeyframe(InTick, ScopedHash);
    }

private:
    SnapshotSchema MSchema{};
    Core::ReplaySession MSession;
    std::vector<std::byte> MScratch;
};

}
