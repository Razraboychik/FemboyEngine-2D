// FemboyEngine2D - animation timeline model tests (no ImGui context).
#include "Engine/ECS/AnimationTimeline.hpp"

#include <cmath>
#include <cstdio>

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

void TestTracksAndKeys()
{
    Fby::Animation::AnimationTimeline ScopedTimeline;
    ScopedTimeline.Clear();
    const std::uint16_t ScopedTrack = ScopedTimeline.AddTrack(Fby::Animation::TrackKind::BoneRotation, 0, 0xFF00FF00u);
    Check(ScopedTrack == 0 && ScopedTimeline.trackCount == 1, "track added");
    const std::uint16_t ScopedFirst =
        ScopedTimeline.AddKey(ScopedTrack, 1.0f, {0.0f, 0.0f}, Fby::Animation::Interpolation::Linear);
    const std::uint16_t ScopedSecond =
        ScopedTimeline.AddKey(ScopedTrack, 0.0f, {0.0f, 0.0f}, Fby::Animation::Interpolation::Linear);
    Check(ScopedFirst != 0xFFFF && ScopedSecond != 0xFFFF, "keys added");
    Check(ScopedTimeline.keys[ScopedTimeline.tracks[ScopedTrack].firstKey].time == 0.0f, "keys stay sorted by time");
    Check(ScopedTimeline.tracks[ScopedTrack].keyCount == 2, "track key count");
    Check(ScopedTimeline.Validate(), "timeline validates");
    Check(!ScopedTimeline.RemoveKey(ScopedTrack, 5), "out-of-range key removal rejected");
    Check(ScopedTimeline.RemoveKey(ScopedTrack, 0), "key removed");
    Check(ScopedTimeline.tracks[ScopedTrack].keyCount == 1, "key count after removal");
    Check(ScopedTimeline.Validate(), "timeline still validates");
}

void TestInterpolation()
{
    Fby::Animation::AnimationTimeline ScopedTimeline;
    ScopedTimeline.Clear();
    const std::uint16_t ScopedTrack = ScopedTimeline.AddTrack(Fby::Animation::TrackKind::BoneRotation, 0, 0xFFFFFFFFu);
    (void)ScopedTimeline.AddKey(ScopedTrack, 0.0f, {0.0f, 0.0f}, Fby::Animation::Interpolation::Linear);
    (void)ScopedTimeline.AddKey(ScopedTrack, 1.0f, {2.0f, 0.0f}, Fby::Animation::Interpolation::Linear);
    CheckClose(ScopedTimeline.EvaluateTrack(ScopedTrack, 0.5f).X, 1.0f, 0.01f, "linear interpolation midpoint");

    // Bezier with flat tangents matches a smooth half-way value.
    ScopedTimeline.Clear();
    const std::uint16_t ScopedBezier = ScopedTimeline.AddTrack(Fby::Animation::TrackKind::BonePosition, 0, 0xFFFFFFFFu);
    (void)ScopedTimeline.AddKey(ScopedBezier, 0.0f, {0.0f, 0.0f}, Fby::Animation::Interpolation::Bezier);
    (void)ScopedTimeline.AddKey(ScopedBezier, 1.0f, {10.0f, 4.0f}, Fby::Animation::Interpolation::Bezier);
    CheckClose(ScopedTimeline.EvaluateTrack(ScopedBezier, 0.0f).X, 0.0f, 0.001f, "bezier start");
    CheckClose(ScopedTimeline.EvaluateTrack(ScopedBezier, 1.0f).X, 10.0f, 0.001f, "bezier end");
    const float ScopedMidpoint = ScopedTimeline.EvaluateTrack(ScopedBezier, 0.5f).X;
    Check(ScopedMidpoint > 1.0f && ScopedMidpoint < 9.0f, "bezier midpoint is inside the segment");
}

void TestBonePoseAndSpriteAnimation()
{
    Fby::Animation::AnimationTimeline ScopedTimeline;
    ScopedTimeline.Clear();
    Fby::SkeletonComponent ScopedSkeleton;
    Fby::SkeletonReset(ScopedSkeleton);
    const std::int16_t ScopedRoot = Fby::SkeletonAddBone(ScopedSkeleton, "root", -1, {0.0f, 0.0f}, 0.0f, 1.0f);
    Check(ScopedRoot == 0, "skeleton bone added");
    Fby::SkeletonBindPose(ScopedSkeleton);

    const std::uint16_t ScopedRotation =
        ScopedTimeline.AddTrack(Fby::Animation::TrackKind::BoneRotation, Fby::BoneNameHash("root"), 0xFFFFFFFFu);
    (void)ScopedTimeline.AddKey(ScopedRotation, 0.0f, {0.0f, 0.0f}, Fby::Animation::Interpolation::Linear);
    (void)ScopedTimeline.AddKey(ScopedRotation, 1.0f, {1.0f, 0.0f}, Fby::Animation::Interpolation::Linear);
    ScopedTimeline.ApplyPose(ScopedSkeleton, 0.5f);
    CheckClose(ScopedSkeleton.LocalRotations[0], 0.5f, 0.01f, "bone rotation authored by the timeline");

    Fby::SpriteAnimationComponent ScopedSpriteAnimation;
    ScopedSpriteAnimation.FrameDuration = 0.1f;
    const std::uint16_t ScopedClip = ScopedTimeline.AddTrack(Fby::Animation::TrackKind::SpriteClip, 0, 0xFFFFFFFFu);
    (void)ScopedTimeline.AddKey(ScopedClip, 0.0f, {4.0f, 3.0f}, Fby::Animation::Interpolation::Step);
    ScopedTimeline.ApplySpriteClip(ScopedSpriteAnimation, 0.25f);
    Check(ScopedSpriteAnimation.StartFrame == 4 && ScopedSpriteAnimation.FrameCount == 3, "spriteAnimation clip selected from timeline");
    Check(ScopedSpriteAnimation.CurrentFrame == 6, "spriteAnimation frame advanced inside the clip");
}

void TestEvents()
{
    Fby::Animation::AnimationTimeline ScopedTimeline;
    ScopedTimeline.Clear();
    Fby::Animation::TimelineEvent ScopedAudio;
    ScopedAudio.time = 0.5f;
    ScopedAudio.kind = Fby::Animation::TimelineEventKind::Audio;
    ScopedAudio.audioAsset = 77;
    (void)ScopedTimeline.AddEvent(ScopedAudio);
    Fby::Animation::TimelineEvent ScopedScript = ScopedAudio;
    ScopedScript.time = 0.9f;
    ScopedScript.kind = Fby::Animation::TimelineEventKind::Script;
    ScopedScript.scriptProgram = 3;
    (void)ScopedTimeline.AddEvent(ScopedScript);

    struct Counter
    {
        int audio = 0;
        int script = 0;
        std::uint64_t lastAsset = 0;
    } ScopedCounter;
    Fby::Animation::TimelineTriggerSink ScopedSink;
    ScopedSink.user = &ScopedCounter;
    ScopedSink.playAudio = [](void* InUser, std::uint64_t InAssetId, float) noexcept
    {
        auto* ScopedValue = static_cast<Counter*>(InUser);
        ++ScopedValue->audio;
        ScopedValue->lastAsset = InAssetId;
    };
    ScopedSink.runScript = [](void* InUser, std::uint32_t, std::uint32_t) noexcept
    { ++static_cast<Counter*>(InUser)->script; };

    ScopedTimeline.DispatchEvents(0.0f, 0.6f, false, ScopedSink);
    Check(ScopedCounter.audio == 1 && ScopedCounter.script == 0, "audio event fires in the crossed interval");
    ScopedTimeline.DispatchEvents(0.6f, 1.0f, false, ScopedSink);
    Check(ScopedCounter.audio == 1 && ScopedCounter.script == 1, "script event fires once");
    ScopedTimeline.DispatchEvents(0.2f, 0.3f, false, ScopedSink);
    Check(ScopedCounter.audio == 1, "events outside the interval do not refire");
    Check(ScopedCounter.lastAsset == 77, "audio trigger carries the authored asset id");
}
}

int main()
{
    TestTracksAndKeys();
    TestInterpolation();
    TestBonePoseAndSpriteAnimation();
    TestEvents();
    if (GFailures != 0)
    {
        std::printf("%d animation timeline test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("animation_timeline_test: all tests passed\n");
    return 0;
}
