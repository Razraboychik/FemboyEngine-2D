// FemboyEngine2D - Timeline & Curve editor panel.
#pragma once

#include "Engine/ECS/AnimationTimeline.hpp"
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Engine/ECS/Components.hpp"
#include "Engine/ECS/Schedule.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace Fby
{
class AudioSystem;
class ScriptRuntime;
}

namespace Fby::Editor
{

struct TimelinePanelContext
{
    Animation::AnimationTimeline* timeline = nullptr;
    Ecs::Registry* registry = nullptr;
    SkeletonComponent* skeleton = nullptr;
    SpriteAnimationComponent* spriteAnimation = nullptr;
    AudioSystem* audio = nullptr;
    ScriptRuntime* scripts = nullptr;
    CommandStack* commands = nullptr;
    Entity entity = entt::null;
};

class AnimationTimelinePanel
{
public:
    AnimationTimelinePanel() = default;

    void SetContext(const TimelinePanelContext& InContext) noexcept
    {
        MContext = InContext;
    }
    [[nodiscard]] TimelinePanelContext& Context() noexcept
    {
        return MContext;
    }
    [[nodiscard]] const TimelinePanelContext& Context() const noexcept
    {
        return MContext;
    }

    [[nodiscard]] bool& Open() noexcept
    {
        return MOpen;
    }
    [[nodiscard]] bool IsPlaying() const noexcept
    {
        return MPlaying;
    }
    [[nodiscard]] float Time() const noexcept
    {
        return MTime;
    }
    void SetTime(float InTime) noexcept;
    void Play() noexcept;
    void Pause() noexcept;
    void Stop() noexcept;
    void SetLooping(bool InLooping) noexcept
    {
        MLooping = InLooping;
    }
    [[nodiscard]] bool Looping() const noexcept
    {
        return MLooping;
    }

    // Advances the playhead and dispatches timeline events. Call once per editor
    void Tick(float InDeltaSeconds) noexcept;

    // ImGui/ImPlot window. Safe to call without a context: draws a hint.
    void Draw();

    // Test seam: `.fanim` round trip without an ImGui frame.
    [[nodiscard]] bool SaveClipFile(const char* InPath, float InTolerance = 0.001f);
    [[nodiscard]] bool LoadClipFile(const char* InPath);

private:
    struct SinkUser
    {
        AnimationTimelinePanel* panel = nullptr;
    };

    static void PlayAudioTrigger(void* InUser, std::uint64_t InAssetId, float InVolume) noexcept;
    static void RunScriptTrigger(void* InUser, std::uint32_t InProgram, std::uint32_t InEntity) noexcept;
    void Dispatch(float InPreviousTime, float InTime) noexcept;
    void DrawToolbar();
    void DrawTrailer();
    void DrawCurveEditor();
    void DrawKeyInspector();
    void AddEventAtPlayhead(Animation::TimelineEventKind InKind) noexcept;
    void ApplyCurrentPose() noexcept;

    TimelinePanelContext MContext{};
    SinkUser MSinkUser{this};
    bool MOpen = true;
    bool MPlaying = false;
    bool MLooping = true;
    float MTime = 0.0f;
    float MPreviousTime = 0.0f;
    float MSpeed = 1.0f;
    float MScroll = 0.0f;
    // Pixels per second
    float MZoom = 120.0f;
    int MSelectedTrack = -1;
    int MSelectedKey = -1;
    bool MSnapToFrames = true;
    bool MShowCurves = true;
    std::array<char, 96> MStatus{};
    // Event authoring scratch (no allocation in the frame path).
    std::array<char, 24> MAudioAssetText{};
    std::array<char, 16> MScriptProgramText{};
    std::array<char, 260> MClipPath{"assets/clips/clip.fanim"};
};
}
