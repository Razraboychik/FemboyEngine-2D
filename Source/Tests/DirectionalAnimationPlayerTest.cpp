// FemboyEngine2D - directional animation player tests.
#include "Engine/ECS/AnimGraph.hpp"
#include "Engine/ECS/DirectionalAnimationPlayer.hpp"
#include "Engine/Assets/AnimationClipAsset.hpp"
#include "Engine/Assets/VFS.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace
{

int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::printf("FAIL: %s\n", InMessage);
        ++GFailures;
    }
}

void CheckClose(float InActual, float InExpected, float InTolerance, const char* InMessage)
{
    if (std::fabs(InActual - InExpected) > InTolerance)
    {
        std::printf("FAIL: %s (expected %f, got %f)\n", InMessage, static_cast<double>(InExpected),
                    static_cast<double>(InActual));
        ++GFailures;
    }
}

constexpr float kPi = 3.14159265358979323846f;

using Fby::Animation::CompressedClip;
using Fby::Animation::CompressedKey;
using Fby::Animation::CompressedTrack;
using Fby::Animation::DirectionalAnimationPlayer;
using Fby::Animation::DirectionalBlendSpaceDocument;
using Fby::Animation::DirectionalBlendTable;
using Fby::Animation::Interpolation;
using Fby::Animation::TrackKind;

std::int16_t Quantize(float InValue, float InScale)
{
    const float ScopedNormalized = std::clamp(InValue / InScale, -1.0f, 1.0f);
    return static_cast<std::int16_t>(std::lround(ScopedNormalized * 32767.0f));
}

CompressedClip MakeClip(float InRotation, std::uint32_t InSpriteStart, std::uint32_t InSpriteCount)
{
    CompressedClip ScopedClip;
    ScopedClip.Duration = 1.0f;
    CompressedTrack& ScopedRotation = ScopedClip.Tracks[0];
    ScopedRotation.Kind = TrackKind::BoneRotation;
    ScopedRotation.BoneHash = Fby::BoneNameHash("arm");
    ScopedRotation.ScaleX = std::max(1.0f, std::fabs(InRotation));
    ScopedRotation.KeyCount = 1;
    ScopedClip.Keys[ScopedClip.KeyCount++] = CompressedKey{0, Quantize(InRotation, ScopedRotation.ScaleX), 0,
                                                           static_cast<std::uint8_t>(Interpolation::Linear), 0};
    std::uint16_t ScopedTrackCount = 1;
    if (InSpriteCount > 0)
    {
        CompressedTrack& ScopedSprite = ScopedClip.Tracks[1];
        ScopedSprite.Kind = TrackKind::SpriteClip;
        ScopedSprite.ScaleX = std::max(1.0f, static_cast<float>(InSpriteStart));
        ScopedSprite.ScaleY = std::max(1.0f, static_cast<float>(InSpriteCount));
        ScopedSprite.FirstKey = ScopedClip.KeyCount;
        ScopedSprite.KeyCount = 1;
        ScopedClip.Keys[ScopedClip.KeyCount++] =
            CompressedKey{0, Quantize(static_cast<float>(InSpriteStart), ScopedSprite.ScaleX),
                          Quantize(static_cast<float>(InSpriteCount), ScopedSprite.ScaleY),
                          static_cast<std::uint8_t>(Interpolation::Step), 0};
        ++ScopedTrackCount;
    }
    ScopedClip.TrackCount = ScopedTrackCount;
    return ScopedClip;
}

Fby::SkeletonComponent MakeSkeleton()
{
    Fby::SkeletonComponent ScopedSkeleton;
    Fby::SkeletonReset(ScopedSkeleton);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "root", -1, {0.0f, 0.0f}, 0.0f);
    (void)Fby::SkeletonAddBone(ScopedSkeleton, "arm", 0, {10.0f, 0.0f}, 0.0f);
    Fby::SkeletonBindPose(ScopedSkeleton);
    Fby::AnimGraphCaptureRestPose(ScopedSkeleton);
    return ScopedSkeleton;
}

DirectionalBlendTable MakeTable()
{
    DirectionalBlendSpaceDocument ScopedDocument;
    ScopedDocument.AnchorCount = 2;
    ScopedDocument.Anchors[0].Position = {1.0f, 0.0f};
    ScopedDocument.Anchors[0].ClipId = 0;
    ScopedDocument.Anchors[1].Position = {0.0f, 1.0f};
    ScopedDocument.Anchors[1].ClipId = 1;
    DirectionalBlendTable ScopedTable;
    (void)Fby::Animation::BakeDirectionalBlendSpace(ScopedDocument, &ScopedTable);
    return ScopedTable;
}

void TestPoseAndSprite()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeSkeleton();
    DirectionalAnimationPlayer ScopedPlayer;
    Check(ScopedPlayer.SetClip(0, MakeClip(0.0f, 0, 4)), "first clip is stored");
    Check(ScopedPlayer.SetClip(1, MakeClip(0.5f * kPi, 4, 8)), "second clip is stored");
    Check(ScopedPlayer.BuildBindings(ScopedSkeleton), "clip bindings compile");
    Check(ScopedPlayer.SetBlendTable(MakeTable()), "blend table is installed");
    ScopedPlayer.SetPhase(0.5f);
    CheckClose(ScopedPlayer.Phase(), 0.5f, 1.0e-6f, "phase is kept");

    Check(ScopedPlayer.ApplyPose(ScopedSkeleton, {1.0f, 1.0f}), "diagonal facing applies a pose");
    CheckClose(ScopedSkeleton.LocalRotations[1], 0.25f * kPi, 1.0e-3f, "diagonal facing blends both rotations");

    Fby::SpriteAnimationComponent ScopedSprite;
    Check(ScopedPlayer.ApplySprite(ScopedSprite), "dominant clip has a sprite track");
    Check(ScopedSprite.StartFrame == 0 && ScopedSprite.FrameCount == 4, "the dominant clip is the first one");

    ScopedPlayer.SetPhase(0.75f);
    Check(ScopedPlayer.ApplyPose(ScopedSkeleton, {0.0f, 1.0f}), "north facing applies a pose");
    CheckClose(ScopedSkeleton.LocalRotations[1], 0.5f * kPi, 1.0e-3f, "north facing selects the second rotation");
    Check(ScopedPlayer.DominantClip() == 1, "dominant clip follows the facing");
    Check(ScopedPlayer.ApplySprite(ScopedSprite), "second dominant clip has a sprite track");
    Check(ScopedSprite.StartFrame == 4 && ScopedSprite.FrameCount == 8, "the sprite range switches with the facing");
    Check(ScopedSprite.CurrentFrame == 0, "an out-of-range current cell resets");
}

void TestPhaseAndFailures()
{
    Fby::SkeletonComponent ScopedSkeleton = MakeSkeleton();
    DirectionalAnimationPlayer ScopedEmpty;
    Check(!ScopedEmpty.ApplyPose(ScopedSkeleton, {1.0f, 0.0f}), "a player without clips does not apply");
    Check(!ScopedEmpty.BuildBindings(ScopedSkeleton), "a player without clips does not bind");

    DirectionalAnimationPlayer ScopedPlayer;
    Check(ScopedPlayer.SetClip(0, MakeClip(0.0f, 0, 4)), "clip is stored");
    Check(!ScopedPlayer.SetClip(0, MakeClip(0.0f, 0, 4)), "duplicate clip id is rejected");
    Check(!ScopedPlayer.ApplyPose(ScopedSkeleton, {1.0f, 0.0f}), "a missing table is rejected");
    Check(ScopedPlayer.BuildBindings(ScopedSkeleton), "clip binding compiles");

    CompressedClip ScopedUnknown = MakeClip(0.0f, 0, 0);
    ScopedUnknown.Tracks[0].BoneHash = Fby::BoneNameHash("missing");
    DirectionalAnimationPlayer ScopedBad;
    Check(ScopedBad.SetClip(0, ScopedUnknown), "unknown-bone clip is stored");
    Check(!ScopedBad.BuildBindings(ScopedSkeleton), "an unknown bone fails the binding");

    Fby::SkeletonComponent ScopedGood = MakeSkeleton();
    DirectionalAnimationPlayer ScopedBound;
    Check(ScopedBound.SetClip(0, MakeClip(0.0f, 0, 0)), "rotation-only clip is stored");
    Check(ScopedBound.BuildBindings(ScopedGood), "rotation-only clip binds");
    Check(ScopedBound.SetBlendTable(MakeTable()), "table is installed");
    Check(ScopedBound.ApplyPose(ScopedGood, {1.0f, 0.0f}), "rotation-only clip applies");
    Fby::SpriteAnimationComponent ScopedSprite;
    Check(!ScopedBound.ApplySprite(ScopedSprite), "a clip without a sprite track reports no sprite");

    ScopedBound.SetPhase(-0.25f);
    CheckClose(ScopedBound.Phase(), 0.75f, 1.0e-5f, "negative phase wraps");
    ScopedBound.SetPhase(1.25f);
    CheckClose(ScopedBound.Phase(), 0.25f, 1.0e-5f, "phase past one wraps");
    ScopedBound.AdvancePhase(0.5f, 1.0f, true);
    CheckClose(ScopedBound.Phase(), 0.75f, 1.0e-5f, "phase advances");
    ScopedBound.AdvancePhase(0.5f, 1.0f, true);
    CheckClose(ScopedBound.Phase(), 0.25f, 1.0e-5f, "phase loops");
    ScopedBound.AdvancePhase(1.0f, 1.0f, false);
    CheckClose(ScopedBound.Phase(), 1.0f, 1.0e-5f, "clamped phase stops at one");

    for (std::uint32_t ScopedIndex = 1; ScopedIndex < Fby::Animation::kMaxDirectionalPlayerClips; ++ScopedIndex)
    {
        Check(ScopedBound.SetClip(ScopedIndex, MakeClip(0.0f, 0, 0)), "clip pool accepts entries up to the capacity");
    }
    Check(!ScopedBound.SetClip(99, MakeClip(0.0f, 0, 0)), "clip pool rejects an entry past the capacity");
}

void TestBytesAndVfs()
{
    const CompressedClip ScopedClip = MakeClip(0.25f * kPi, 0, 2);
    std::vector<std::byte> ScopedBytes;
    std::string ScopedError;
    Check(Fby::Animation::Asset::SaveClip(ScopedClip, &ScopedBytes, &ScopedError), "clip serializes");

    DirectionalAnimationPlayer ScopedPlayer;
    Check(ScopedPlayer.SetClipFromBytes(7, std::span<const std::byte>(ScopedBytes.data(), ScopedBytes.size()),
                                        &ScopedError),
          "clip loads from bytes");
    Check(ScopedPlayer.ClipCount() == 1, "byte clip occupies a slot");

    const std::filesystem::path ScopedDirectory = std::filesystem::current_path() / "directional_player_test";
    std::error_code ScopedCode;
    std::filesystem::remove_all(ScopedDirectory, ScopedCode);
    std::filesystem::create_directories(ScopedDirectory, ScopedCode);
    {
        std::ofstream ScopedFile(ScopedDirectory / "run.fanim", std::ios::binary | std::ios::trunc);
        ScopedFile.write(reinterpret_cast<const char*>(ScopedBytes.data()),
                         static_cast<std::streamsize>(ScopedBytes.size()));
    }
    Fby::VirtualFileSystem ScopedVfs;
    Check(ScopedVfs.MountDirectory(ScopedDirectory.string(), 0, &ScopedError), "clip directory mounts");
    Check(ScopedPlayer.LoadClip(ScopedVfs, 8, "run.fanim", &ScopedError), "clip loads through the VFS");
    Check(ScopedPlayer.ClipCount() == 2, "VFS clip occupies a slot");
    Check(!ScopedPlayer.LoadClip(ScopedVfs, 9, "missing.fanim", &ScopedError), "a missing clip path is reported");

    std::filesystem::remove_all(ScopedDirectory, ScopedCode);
}
}

int main()
{
    TestPoseAndSprite();
    TestPhaseAndFailures();
    TestBytesAndVfs();
    if (GFailures != 0)
    {
        std::printf("%d directional animation player test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("directional_animation_player_test: all tests passed\n");
    return 0;
}
