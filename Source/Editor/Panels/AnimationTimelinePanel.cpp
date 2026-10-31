// FemboyEngine2D - Timeline & Curve editor panel implementation.
#include "Editor/Panels/AnimationTimelinePanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Audio/Audio.hpp"
#include "Engine/Assets/AnimationClipAsset.hpp"
#include "Engine/Core/AssetId.hpp"
#include "Engine/Script/ScriptSystem.hpp"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Fby::Editor
{
namespace
{
constexpr float kMinZoom = 20.0f;
constexpr float kMaxZoom = 2000.0f;

[[nodiscard]] const char* TrackKindLabel(Animation::TrackKind InKind) noexcept
{
    switch (InKind)
    {
        case Animation::TrackKind::BonePosition:
            return "pos";
        case Animation::TrackKind::BoneRotation:
            return "rot";
        case Animation::TrackKind::BoneScale:
            return "scale";
        case Animation::TrackKind::SpriteClip:
            return "clip";
        case Animation::TrackKind::Count:
            break;
    }
    return "?";
}
}

void AnimationTimelinePanel::SetTime(float InTime) noexcept
{
    MTime = std::max(0.0f, InTime);
    if (MContext.timeline != nullptr && MContext.timeline->duration > 0.0f)
    {
        MTime = std::min(MTime, MContext.timeline->duration);
    }
    MPreviousTime = MTime;
    ApplyCurrentPose();
}

void AnimationTimelinePanel::Play() noexcept
{
    if (MContext.timeline == nullptr || MContext.timeline->Empty())
    {
        return;
    }
    MPlaying = true;
}

void AnimationTimelinePanel::Pause() noexcept
{
    MPlaying = false;
}

void AnimationTimelinePanel::Stop() noexcept
{
    MPlaying = false;
    MTime = 0.0f;
    MPreviousTime = 0.0f;
    ApplyCurrentPose();
}

void AnimationTimelinePanel::ApplyCurrentPose() noexcept
{
    if (MContext.timeline == nullptr)
    {
        return;
    }
    if (MContext.skeleton != nullptr)
    {
        MContext.timeline->ApplyPose(*MContext.skeleton, MTime);
    }
    if (MContext.spriteAnimation != nullptr)
    {
        MContext.timeline->ApplySpriteClip(*MContext.spriteAnimation, MTime);
    }
}

void AnimationTimelinePanel::PlayAudioTrigger(void* InUser, std::uint64_t InAssetId, float InVolume) noexcept
{
    auto* ScopedSink = static_cast<SinkUser*>(InUser);
    if (ScopedSink == nullptr || ScopedSink->panel == nullptr || InAssetId == 0)
    {
        return;
    }
    AudioSystem* ScopedAudio = ScopedSink->panel->MContext.audio;
    if (ScopedAudio != nullptr)
    {
        (void)ScopedAudio->Play(AssetId{InAssetId}, AudioBus::Sfx, InVolume);
    }
}

void AnimationTimelinePanel::RunScriptTrigger(void* InUser, std::uint32_t InProgram, std::uint32_t InEntity) noexcept
{
    auto* ScopedSink = static_cast<SinkUser*>(InUser);
    if (ScopedSink == nullptr || ScopedSink->panel == nullptr)
    {
        return;
    }
    TimelinePanelContext& ScopedContext = ScopedSink->panel->MContext;
    if (ScopedContext.scripts == nullptr || ScopedContext.registry == nullptr)
    {
        return;
    }
    if (!ScopedContext.registry->valid(static_cast<Entity>(InEntity)))
    {
        return;
    }
    Script::VmContext ScopedVmContext{*ScopedContext.registry};
    ScopedVmContext.rng = &ScopedContext.scripts->Rng();
    (void)RunScriptOnce(*ScopedContext.scripts, InProgram, static_cast<Entity>(InEntity), Script::ScriptEvent::Start,
                        ScopedVmContext);
}

void AnimationTimelinePanel::Dispatch(float InPreviousTime, float InTime) noexcept
{
    if (MContext.timeline == nullptr)
    {
        return;
    }
    Animation::TimelineTriggerSink ScopedSink;
    ScopedSink.user = &MSinkUser;
    ScopedSink.playAudio = &PlayAudioTrigger;
    ScopedSink.runScript = &RunScriptTrigger;
    MContext.timeline->DispatchEvents(InPreviousTime, InTime, MLooping, ScopedSink);
}

void AnimationTimelinePanel::Tick(float InDeltaSeconds) noexcept
{
    if (!MPlaying || MContext.timeline == nullptr)
    {
        return;
    }
    const float ScopedDuration = std::max(0.01f, MContext.timeline->duration);
    const float ScopedPrevious = MTime;
    MTime += InDeltaSeconds * MSpeed;
    bool ScopedLooped = false;
    if (MTime >= ScopedDuration)
    {
        if (MLooping)
        {
            MTime = std::fmod(MTime, ScopedDuration);
            ScopedLooped = true;
        }
        else
        {
            MTime = ScopedDuration;
            MPlaying = false;
        }
    }
    MPreviousTime = ScopedPrevious;
    ApplyCurrentPose();
    Dispatch(ScopedPrevious, MTime);
    (void)ScopedLooped;
}

void AnimationTimelinePanel::AddEventAtPlayhead(Animation::TimelineEventKind InKind) noexcept
{
    if (MContext.timeline == nullptr)
    {
        return;
    }
    Animation::TimelineEvent ScopedEvent;
    ScopedEvent.time = MTime;
    ScopedEvent.kind = InKind;
    ScopedEvent.entity = static_cast<std::uint32_t>(MContext.entity);
    if (InKind == Animation::TimelineEventKind::Audio && MAudioAssetText[0] != '\0')
    {
        std::uint64_t ScopedValue = 0;
        std::string_view ScopedText(MAudioAssetText.data());
        if (ScopedText.starts_with("0x") || ScopedText.starts_with("0X"))
        {
            ScopedText.remove_prefix(2);
        }
        (void)std::from_chars(ScopedText.data(), ScopedText.data() + ScopedText.size(), ScopedValue, 16);
        ScopedEvent.audioAsset = static_cast<std::uint32_t>(ScopedValue);
    }
    if (InKind == Animation::TimelineEventKind::Script && MScriptProgramText[0] != '\0')
    {
        std::uint32_t ScopedValue = 0;
        (void)std::from_chars(MScriptProgramText.data(), MScriptProgramText.data() + MScriptProgramText.size(),
                              ScopedValue);
        ScopedEvent.scriptProgram = ScopedValue;
    }
    if (MContext.timeline->AddEvent(ScopedEvent) != 0xFFFFu)
    {
        std::snprintf(MStatus.data(), MStatus.size(), "event added at %.3f s", static_cast<double>(MTime));
    }
}

void AnimationTimelinePanel::DrawToolbar()
{
    if (ImGui::Button(MPlaying ? "Pause" : "Play"))
    {
        if (MPlaying)
        {
            Pause();
        }
        else
        {
            Play();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
    {
        Stop();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &MLooping);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Speed", &MSpeed, 0.05f, 0.1f, 4.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::DragFloat("Duration", &MContext.timeline->duration, 0.05f, 0.1f, 3600.0f, "%.2fs"))
    {
        MContext.timeline->duration = std::max(0.1f, MContext.timeline->duration);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::DragFloat("FPS", &MContext.timeline->fps, 0.5f, 1.0f, 240.0f, "%.0f");
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &MSnapToFrames);
    ImGui::SameLine();
    ImGui::Checkbox("Curves", &MShowCurves);

    // `.fanim` round trip: the compressed clip is what a build ships, so the
    // authoring panel can write and read it without a separate tool.
    ImGui::SetNextItemWidth(220.0f);
    (void)ImGui::InputText("clip", MClipPath.data(), MClipPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Save clip"))
    {
        (void)SaveClipFile(MClipPath.data());
    }
    ImGui::SameLine();
    if (ImGui::Button("Load clip"))
    {
        (void)LoadClipFile(MClipPath.data());
    }
    if (MStatus[0] != '\0')
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", MStatus.data());
    }
}

bool AnimationTimelinePanel::SaveClipFile(const char* InPath, float InTolerance)
{
    if (MContext.timeline == nullptr || InPath == nullptr || InPath[0] == '\0')
    {
        std::snprintf(MStatus.data(), MStatus.size(), "save: no timeline or path");
        return false;
    }
    Animation::CompressedClip ScopedClip;
    Animation::CompressionReport ScopedReport;
    if (!Animation::CompressTimeline(*MContext.timeline, InTolerance, &ScopedClip, &ScopedReport))
    {
        std::snprintf(MStatus.data(), MStatus.size(), "save: compression failed");
        return false;
    }
    std::vector<std::byte> ScopedBytes;
    std::string ScopedError;
    if (!Animation::Asset::SaveClip(ScopedClip, &ScopedBytes, &ScopedError))
    {
        std::snprintf(MStatus.data(), MStatus.size(), "save: %s", ScopedError.c_str());
        return false;
    }
    const std::filesystem::path ScopedPath(InPath);
    std::error_code ScopedFsError;
    if (!ScopedPath.parent_path().empty())
    {
        std::filesystem::create_directories(ScopedPath.parent_path(), ScopedFsError);
    }
    std::ofstream ScopedFile(ScopedPath, std::ios::binary | std::ios::trunc);
    if (!ScopedFile)
    {
        std::snprintf(MStatus.data(), MStatus.size(), "save: cannot open %s", InPath);
        return false;
    }
    ScopedFile.write(reinterpret_cast<const char*>(ScopedBytes.data()),
                     static_cast<std::streamsize>(ScopedBytes.size()));
    if (!ScopedFile)
    {
        std::snprintf(MStatus.data(), MStatus.size(), "save: write failed");
        return false;
    }
    std::snprintf(MStatus.data(), MStatus.size(), "saved %u keys (max error %u/1000)",
                  static_cast<unsigned>(ScopedClip.KeyCount), ScopedReport.MaxErrorX1000);
    return true;
}

bool AnimationTimelinePanel::LoadClipFile(const char* InPath)
{
    if (MContext.timeline == nullptr || InPath == nullptr || InPath[0] == '\0')
    {
        std::snprintf(MStatus.data(), MStatus.size(), "load: no timeline or path");
        return false;
    }
    std::ifstream ScopedFile(std::filesystem::path(InPath), std::ios::binary | std::ios::ate);
    if (!ScopedFile)
    {
        std::snprintf(MStatus.data(), MStatus.size(), "load: cannot open %s", InPath);
        return false;
    }
    const std::streamoff ScopedSize = ScopedFile.tellg();
    if (ScopedSize <= 0 || ScopedSize > static_cast<std::streamoff>(Animation::Asset::kFanmMaxBytes))
    {
        std::snprintf(MStatus.data(), MStatus.size(), "load: invalid clip size");
        return false;
    }
    ScopedFile.seekg(0);
    std::vector<std::byte> ScopedBytes(static_cast<std::size_t>(ScopedSize));
    ScopedFile.read(reinterpret_cast<char*>(ScopedBytes.data()), static_cast<std::streamsize>(ScopedSize));
    if (!ScopedFile)
    {
        std::snprintf(MStatus.data(), MStatus.size(), "load: read failed");
        return false;
    }
    Animation::CompressedClip ScopedClip;
    std::string ScopedError;
    if (!Animation::Asset::LoadClip(ScopedBytes, &ScopedClip, &ScopedError))
    {
        std::snprintf(MStatus.data(), MStatus.size(), "load: %s", ScopedError.c_str());
        return false;
    }
    if (!Animation::DecompressTimeline(ScopedClip, MContext.timeline))
    {
        std::snprintf(MStatus.data(), MStatus.size(), "load: decompression failed");
        return false;
    }
    MSelectedTrack = -1;
    MSelectedKey = -1;
    MTime = 0.0f;
    std::snprintf(MStatus.data(), MStatus.size(), "loaded %u tracks, %u keys",
                  static_cast<unsigned>(ScopedClip.TrackCount), static_cast<unsigned>(ScopedClip.KeyCount));
    return true;
}

void AnimationTimelinePanel::DrawTrailer()
{
    Animation::AnimationTimeline& ScopedTimeline = *MContext.timeline;
    const ImVec2 ScopedSize(ImGui::GetContentRegionAvail().x, 160.0f);
    const ImVec2 ScopedOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("timeline_canvas", ScopedSize);
    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    ScopedDraw->AddRectFilled(ScopedOrigin, ImVec2(ScopedOrigin.x + ScopedSize.x, ScopedOrigin.y + ScopedSize.y),
                              IM_COL32(24, 24, 28, 255));

    const float ScopedLeft = ScopedOrigin.x + 120.0f;
    const float ScopedRight = ScopedOrigin.x + ScopedSize.x - 8.0f;
    const float ScopedPixelsPerSecond = MZoom;
    const float ScopedVisibleSeconds = (ScopedRight - ScopedLeft) / std::max(1.0f, ScopedPixelsPerSecond);
    MScroll = std::clamp(MScroll, 0.0f, std::max(0.0f, ScopedTimeline.duration - ScopedVisibleSeconds * 0.5f));

    const auto ScopedTimeToX = [&](float InTime) { return ScopedLeft + (InTime - MScroll) * ScopedPixelsPerSecond; };
    // Ruler.
    for (float ScopedSecond = std::floor(MScroll); ScopedSecond <= MScroll + ScopedVisibleSeconds + 1.0f;
         ScopedSecond += 1.0f)
    {
        const float ScopedX = ScopedTimeToX(ScopedSecond);
        if (ScopedX < ScopedLeft || ScopedX > ScopedRight)
        {
            continue;
        }
        ScopedDraw->AddLine(ImVec2(ScopedX, ScopedOrigin.y + 20.0f), ImVec2(ScopedX, ScopedOrigin.y + ScopedSize.y),
                            IM_COL32(60, 60, 70, 255));
        char ScopedLabel[16] = {};
        std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%.0fs", static_cast<double>(ScopedSecond));
        ScopedDraw->AddText(ImVec2(ScopedX + 2.0f, ScopedOrigin.y + 2.0f), IM_COL32(160, 160, 170, 255), ScopedLabel);
    }
    // Tracks.
    float ScopedRowY = ScopedOrigin.y + 26.0f;
    for (std::uint32_t ScopedTrackIndex = 0; ScopedTrackIndex < ScopedTimeline.trackCount; ++ScopedTrackIndex)
    {
        const Animation::TimelineTrack& ScopedTrack = ScopedTimeline.tracks[ScopedTrackIndex];
        const bool ScopedSelected = MSelectedTrack == static_cast<int>(ScopedTrackIndex);
        if (ScopedSelected)
        {
            ScopedDraw->AddRectFilled(ImVec2(ScopedOrigin.x, ScopedRowY - 2.0f),
                                      ImVec2(ScopedOrigin.x + ScopedSize.x, ScopedRowY + 18.0f),
                                      IM_COL32(50, 44, 60, 255));
        }
        char ScopedTrackLabel[64] = {};
        std::snprintf(ScopedTrackLabel, sizeof(ScopedTrackLabel), "%s #%d", TrackKindLabel(ScopedTrack.kind),
                      static_cast<int>(ScopedTrack.boneIndex));
        ScopedDraw->AddText(ImVec2(ScopedOrigin.x + 6.0f, ScopedRowY), IM_COL32(210, 210, 220, 255), ScopedTrackLabel);
        // Curve polyline.
        ImVec2 ScopedPreviousPoint{};
        bool ScopedHasPrevious = false;
        for (float ScopedPixel = ScopedLeft; ScopedPixel <= ScopedRight; ScopedPixel += 3.0f)
        {
            const float ScopedT = MScroll + (ScopedPixel - ScopedLeft) / ScopedPixelsPerSecond;
            const Vector2D ScopedValue =
                ScopedTimeline.EvaluateTrack(static_cast<std::uint16_t>(ScopedTrackIndex), ScopedT, Vector2D{});
            const float ScopedNormalized = std::clamp(ScopedValue.X, -200.0f, 200.0f);
            const ImVec2 ScopedPoint(ScopedPixel, ScopedRowY + 9.0f - ScopedNormalized * 0.02f);
            if (ScopedHasPrevious)
            {
                ScopedDraw->AddLine(ScopedPreviousPoint, ScopedPoint, ScopedTrack.color.rgba, 1.5f);
            }
            ScopedPreviousPoint = ScopedPoint;
            ScopedHasPrevious = true;
        }
        // Keys.
        for (std::uint32_t ScopedKey = 0; ScopedKey < ScopedTrack.keyCount; ++ScopedKey)
        {
            const Animation::TimelineKey& ScopedKeyframe = ScopedTimeline.keys[ScopedTrack.firstKey + ScopedKey];
            const ImVec2 ScopedPoint(ScopedTimeToX(ScopedKeyframe.time), ScopedRowY + 8.0f);
            const bool ScopedKeySelected =
                MSelectedTrack == static_cast<int>(ScopedTrackIndex) && MSelectedKey == static_cast<int>(ScopedKey);
            ScopedDraw->AddCircleFilled(ScopedPoint, ScopedKeySelected ? 5.0f : 3.5f,
                                        ScopedKeySelected ? IM_COL32(255, 255, 140, 255) : ScopedTrack.color.rgba);
        }
        ScopedRowY += 22.0f;
        if (ScopedRowY > ScopedOrigin.y + ScopedSize.y - 8.0f)
        {
            break;
        }
    }
    // Playhead.
    {
        const float ScopedX = ScopedTimeToX(MTime);
        ScopedDraw->AddLine(ImVec2(ScopedX, ScopedOrigin.y + 18.0f), ImVec2(ScopedX, ScopedOrigin.y + ScopedSize.y),
                            IM_COL32(255, 92, 120, 255), 2.0f);
        ScopedDraw->AddTriangleFilled(ImVec2(ScopedX - 5.0f, ScopedOrigin.y + 18.0f),
                                      ImVec2(ScopedX + 5.0f, ScopedOrigin.y + 18.0f),
                                      ImVec2(ScopedX, ScopedOrigin.y + 26.0f), IM_COL32(255, 92, 120, 255));
    }
    // Scrubbing.
    const ImVec2 ScopedMouse = ImGui::GetIO().MousePos;
    const bool ScopedHovered = ImGui::IsItemHovered();
    if (ScopedHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ScopedMouse.x >= ScopedLeft)
    {
        SetTime(std::max(0.0f, MScroll + (ScopedMouse.x - ScopedLeft) / ScopedPixelsPerSecond));
    }
    // Wheel zoom around the cursor.
    if (ScopedHovered && ImGui::GetIO().MouseWheel != 0.0f)
    {
        MZoom = std::clamp(MZoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f), kMinZoom, kMaxZoom);
    }
    if (ScopedHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
    {
        MScroll = std::max(0.0f, MScroll - ImGui::GetIO().MouseDelta.x / std::max(1.0f, ScopedPixelsPerSecond));
    }
    // Track selection by row.
    if (ScopedHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const int ScopedRow = static_cast<int>((ScopedMouse.y - (ScopedOrigin.y + 26.0f)) / 22.0f);
        if (ScopedRow >= 0 && ScopedRow < static_cast<int>(ScopedTimeline.trackCount))
        {
            MSelectedTrack = ScopedRow;
            MSelectedKey = -1;
        }
    }
}

void AnimationTimelinePanel::DrawCurveEditor()
{
    if (!MShowCurves || MContext.timeline == nullptr)
    {
        return;
    }
    Animation::AnimationTimeline& ScopedTimeline = *MContext.timeline;
    if (MSelectedTrack < 0 || MSelectedTrack >= static_cast<int>(ScopedTimeline.trackCount))
    {
        ImGui::TextDisabled("Select a track to edit its Bezier curve.");
        return;
    }
    const Animation::TimelineTrack& ScopedTrack = ScopedTimeline.tracks[MSelectedTrack];
    if (ImPlot::BeginPlot("curve_editor", ImVec2(-1.0f, 200.0f)))
    {
        ImPlot::SetupAxes("time", "value", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        // Sample the evaluated curve.
        std::array<float, 256> ScopedTimes{};
        std::array<float, 256> ScopedValues{};
        for (std::size_t ScopedI = 0; ScopedI < ScopedTimes.size(); ++ScopedI)
        {
            ScopedTimes[ScopedI] =
                ScopedTimeline.duration * static_cast<float>(ScopedI) / static_cast<float>(ScopedTimes.size() - 1);
            ScopedValues[ScopedI] =
                ScopedTimeline
                    .EvaluateTrack(static_cast<std::uint16_t>(MSelectedTrack), ScopedTimes[ScopedI], Vector2D{})
                    .X;
        }
        ImPlot::PlotLine("value", ScopedTimes.data(), ScopedValues.data(), static_cast<int>(ScopedTimes.size()));
        // Keys and Bezier handles.
        for (std::uint32_t ScopedKey = 0; ScopedKey < ScopedTrack.keyCount; ++ScopedKey)
        {
            const Animation::TimelineKey& ScopedKeyframe = ScopedTimeline.keys[ScopedTrack.firstKey + ScopedKey];
            const float ScopedPoint[2] = {ScopedKeyframe.time, ScopedKeyframe.value.X};
            ImPlot::PlotScatter("keys", &ScopedPoint[0], &ScopedPoint[1], 1);
            if (ScopedKeyframe.interpolation == Animation::Interpolation::Bezier)
            {
                const float ScopedHandleIn[2] = {ScopedKeyframe.time + ScopedKeyframe.inTangent.X,
                                                 ScopedKeyframe.value.X + ScopedKeyframe.inTangent.Y};
                const float ScopedHandleOut[2] = {ScopedKeyframe.time + ScopedKeyframe.outTangent.X,
                                                  ScopedKeyframe.value.X + ScopedKeyframe.outTangent.Y};
                ImPlot::PlotLine("in", &ScopedPoint[0], &ScopedPoint[1], 1);
                ImPlot::PlotScatter("handle", &ScopedHandleIn[0], &ScopedHandleIn[1], 1);
                ImPlot::PlotScatter("handle2", &ScopedHandleOut[0], &ScopedHandleOut[1], 1);
            }
        }
        ImPlot::EndPlot();
    }
}

void AnimationTimelinePanel::DrawKeyInspector()
{
    Animation::AnimationTimeline& ScopedTimeline = *MContext.timeline;
    if (MSelectedTrack < 0 || MSelectedTrack >= static_cast<int>(ScopedTimeline.trackCount))
    {
        return;
    }
    Animation::TimelineTrack& ScopedTrack = ScopedTimeline.tracks[MSelectedTrack];
    if (ScopedTrack.keyCount == 0)
    {
        if (ImGui::Button("Add key at playhead"))
        {
            MSelectedKey =
                static_cast<int>(ScopedTimeline.AddKey(static_cast<std::uint16_t>(MSelectedTrack), MTime, Vector2D{}));
        }
        return;
    }
    if (MSelectedKey < 0 || MSelectedKey >= static_cast<int>(ScopedTrack.keyCount))
    {
        MSelectedKey = 0;
    }
    Animation::TimelineKey& ScopedKey = ScopedTimeline.keys[ScopedTrack.firstKey + MSelectedKey];
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Time", &ScopedKey.time, 0.01f, 0.0f, ScopedTimeline.duration, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Value", &ScopedKey.value.X, 0.01f, -10000.0f, 10000.0f, "%.3f");
    if (ScopedTrack.kind == Animation::TrackKind::BonePosition)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat("Value Y", &ScopedKey.value.Y, 0.01f, -10000.0f, 10000.0f, "%.3f");
    }
    const char* ScopedInterpolations[] = {"Step", "Linear", "Bezier"};
    int ScopedInterpolation = static_cast<int>(ScopedKey.interpolation);
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::Combo("Interpolation", &ScopedInterpolation, ScopedInterpolations, 3))
    {
        ScopedKey.interpolation = static_cast<Animation::Interpolation>(ScopedInterpolation);
    }
    if (ScopedKey.interpolation == Animation::Interpolation::Bezier)
    {
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat2("In tangent", &ScopedKey.inTangent.X, 0.002f);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat2("Out tangent", &ScopedKey.outTangent.X, 0.002f);
    }
    if (ImGui::Button("Add key"))
    {
        MSelectedKey =
            static_cast<int>(ScopedTimeline.AddKey(static_cast<std::uint16_t>(MSelectedTrack), MTime, ScopedKey.value));
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete key"))
    {
        (void)ScopedTimeline.RemoveKey(static_cast<std::uint16_t>(MSelectedTrack),
                                       static_cast<std::uint16_t>(MSelectedKey));
        MSelectedKey = std::max(0, MSelectedKey - 1);
    }
}

void AnimationTimelinePanel::Draw()
{
    if (!MOpen)
    {
        return;
    }
    if (!BeginEditorPanel("Timeline & Curves", &MOpen, ImGuiWindowFlags_MenuBar))
    {
        EndEditorPanel();
        return;
    }
    if (MContext.timeline == nullptr)
    {
        ImGui::TextDisabled("No timeline asset bound. Select an animation asset in the Content Browser.");
        EndEditorPanel();
        return;
    }
    Animation::AnimationTimeline& ScopedTimeline = *MContext.timeline;
    if (!ScopedTimeline.Validate())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Timeline data is invalid (overlapping track ranges).");
    }
    DrawToolbar();
    ImGui::Separator();
    // Add-track menu.
    if (ImGui::Button("Add bone track"))
    {
        ImGui::OpenPopup("add_track");
    }
    if (ImGui::BeginPopup("add_track"))
    {
        const Animation::TrackKind ScopedKinds[] = {Animation::TrackKind::BonePosition,
                                                    Animation::TrackKind::BoneRotation, Animation::TrackKind::BoneScale,
                                                    Animation::TrackKind::SpriteClip};
        for (const Animation::TrackKind ScopedKind : ScopedKinds)
        {
            if (ImGui::MenuItem(TrackKindLabel(ScopedKind)))
            {
                (void)ScopedTimeline.AddTrack(ScopedKind, 0, 0xFFFFB4DCu);
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("playhead %.3f s / %.2f s", static_cast<double>(MTime),
                        static_cast<double>(ScopedTimeline.duration));

    DrawTrailer();
    ImGui::Separator();
    if (ImGui::BeginChild("lower", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
    {
        if (ImGui::BeginChild("tracks", ImVec2(340.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            for (std::uint32_t ScopedI = 0; ScopedI < ScopedTimeline.trackCount; ++ScopedI)
            {
                const Animation::TimelineTrack& ScopedTrack = ScopedTimeline.tracks[ScopedI];
                char ScopedLabel[64] = {};
                std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%s##%u", TrackKindLabel(ScopedTrack.kind), ScopedI);
                if (ImGui::Selectable(ScopedLabel, MSelectedTrack == static_cast<int>(ScopedI)))
                {
                    MSelectedTrack = static_cast<int>(ScopedI);
                    MSelectedKey = -1;
                }
            }
            ImGui::Separator();
            if (ImGui::Button("Add key"))
            {
                if (MSelectedTrack >= 0)
                {
                    MSelectedKey = static_cast<int>(
                        ScopedTimeline.AddKey(static_cast<std::uint16_t>(MSelectedTrack), MTime, Vector2D{}));
                }
            }
            ImGui::SameLine();
            ImGui::InputText("Audio AssetId", MAudioAssetText.data(), MAudioAssetText.size());
            if (ImGui::Button("Audio event"))
            {
                AddEventAtPlayhead(Animation::TimelineEventKind::Audio);
            }
            ImGui::SameLine();
            if (ImGui::Button("Script event"))
            {
                AddEventAtPlayhead(Animation::TimelineEventKind::Script);
            }
            ImGui::InputText("Program", MScriptProgramText.data(), MScriptProgramText.size());
            ImGui::TextDisabled("Events: %u", ScopedTimeline.eventCount);
            for (std::uint32_t ScopedI = 0; ScopedI < ScopedTimeline.eventCount; ++ScopedI)
            {
                const Animation::TimelineEvent& ScopedEvent = ScopedTimeline.events[ScopedI];
                ImGui::Text("%.2fs %s", static_cast<double>(ScopedEvent.time),
                            ScopedEvent.kind == Animation::TimelineEventKind::Audio ? "audio" : "script");
                ImGui::SameLine();
                if (ImGui::SmallButton("x"))
                {
                    (void)ScopedTimeline.RemoveEvent(static_cast<std::uint16_t>(ScopedI));
                }
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("curve", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawKeyInspector();
            DrawCurveEditor();
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    if (MStatus[0] != '\0')
    {
        ImGui::TextDisabled("%s", MStatus.data());
    }
    EndEditorPanel();
}
}
