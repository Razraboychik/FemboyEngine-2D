// FemboyEngine2D - visual directional blend space panel implementation.
#include "Editor/Panels/DirectionalBlendSpacePanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Editor/Panels/ContentBrowserPanel.hpp"
#include "Editor/Panels/DirectionalBlendFile.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace Fby::Editor
{
namespace
{

constexpr float kAnchorHitRadius = 10.0f;
constexpr float kDirectionHitRadius = 12.0f;
constexpr float kCanvasPadding = 34.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint32_t kNoClip = 0xFFFFFFFFu;

[[nodiscard]] bool Near(const ImVec2& InA, const ImVec2& InB, float InRadius) noexcept
{
    const float ScopedDx = InA.x - InB.x;
    const float ScopedDy = InA.y - InB.y;
    return ScopedDx * ScopedDx + ScopedDy * ScopedDy <= InRadius * InRadius;
}

[[nodiscard]] ImU32 AnchorColor(bool InSelected, bool InEnabled, bool InActive) noexcept
{
    if (InSelected)
    {
        return IM_COL32(255, 214, 102, 255);
    }
    if (!InEnabled)
    {
        return IM_COL32(120, 120, 130, 255);
    }
    if (InActive)
    {
        return IM_COL32(120, 230, 200, 255);
    }
    return IM_COL32(235, 140, 190, 255);
}

[[nodiscard]] float WeightOfClip(const Animation::DirectionalBlendSample& InSample, std::uint32_t InClip) noexcept
{
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < InSample.Count; ++ScopedIndex)
    {
        if (InSample.Weights[ScopedIndex].ClipId == InClip)
        {
            return InSample.Weights[ScopedIndex].Weight;
        }
    }
    return 0.0f;
}

[[nodiscard]] float NormalizedDegrees(float InRadians) noexcept
{
    float ScopedDegrees = std::fmod(InRadians * (180.0f / kPi), 360.0f);
    if (ScopedDegrees < 0.0f)
    {
        ScopedDegrees += 360.0f;
    }
    return ScopedDegrees;
}

[[nodiscard]] std::string ClipSummary(const std::string& InPath, std::uint32_t InClip)
{
    if (!InPath.empty())
    {
        return std::filesystem::path(InPath).filename().string();
    }
    char ScopedText[32] = {};
    std::snprintf(ScopedText, sizeof(ScopedText), "clip %u", static_cast<unsigned>(InClip));
    return ScopedText;
}
}

DirectionalBlendSpacePanel::DirectionalBlendSpacePanel()
{
    BuildDefaultDocument();
}

DirectionalBlendSpacePanel::~DirectionalBlendSpacePanel()
{
    Shutdown();
}

bool DirectionalBlendSpacePanel::Initialize(CommandStack& InCommands) noexcept
{
    if (MInitialized)
    {
        return true;
    }
    MCommands = &InCommands;
    MCodec =
        InCommands.RegisterExternalBuffer("DirectionalBlendSpaceDocument", &MDocument,
                                          static_cast<std::uint32_t>(sizeof(Animation::DirectionalBlendSpaceDocument)));
    if (MCodec == 0xFFFFu)
    {
        MCommands = nullptr;
        SetStatus("blend space: CommandStack external buffer rejected");
        return false;
    }
    MCodecSize = InCommands.CodecSize(MCodec);
    if (MCodecSize == 0)
    {
        MCodec = 0xFFFFu;
        MCommands = nullptr;
        SetStatus("blend space: document codec has no size");
        return false;
    }
    MInitialized = true;
    BuildDefaultDocument();
    return true;
}

void DirectionalBlendSpacePanel::Shutdown() noexcept
{
    if (MEditing && MCommands != nullptr)
    {
        MCommands->CancelTransaction();
    }
    MEditing = false;
    MCommands = nullptr;
    MCodec = 0xFFFFu;
    MCodecSize = 0;
    MInitialized = false;
}

void DirectionalBlendSpacePanel::BuildDefaultDocument() noexcept
{
    MDocument = Animation::DirectionalBlendSpaceDocument{};
    MDocument.Topology = Animation::DirectionalBlendTopology::Ring;
    MDocument.NormalizeWeights = true;
    MDocument.AnchorCount = 8;
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < 8; ++ScopedIndex)
    {
        const float ScopedAngle = static_cast<float>(ScopedIndex) * (kPi / 4.0f);
        Animation::DirectionalBlendAnchor& ScopedAnchor = MDocument.Anchors[ScopedIndex];
        ScopedAnchor.Position = {std::cos(ScopedAngle), std::sin(ScopedAngle)};
        ScopedAnchor.ClipId = ScopedIndex;
        ScopedAnchor.WeightScale = 1.0f;
        ScopedAnchor.Enabled = true;
        MClipPaths[ScopedIndex].clear();
    }
    MSelectedAnchor = 0;
    MClipPathAnchor = kNoBlendAnchor;
    MPreviewAngleRadians = 0.0f;
    MPreviewPoint = {};
    RebuildTable();
    EvaluatePreview();
}

void DirectionalBlendSpacePanel::SetDocument(const Animation::DirectionalBlendSpaceDocument& InDocument) noexcept
{
    MDocument = InDocument;
    for (std::string& ScopedPath : MClipPaths)
    {
        ScopedPath.clear();
    }
    MSelectedAnchor = MDocument.AnchorCount > 0 ? 0 : kNoBlendAnchor;
    MClipPathAnchor = kNoBlendAnchor;
    MPreviewAngleRadians = 0.0f;
    MPreviewPoint = {};
    RebuildTable();
    EvaluatePreview();
}

void DirectionalBlendSpacePanel::RebuildTable() noexcept
{
    if (!Animation::BakeDirectionalBlendSpace(MDocument, &MTable))
    {
        MTable = Animation::DirectionalBlendTable{};
        SetStatus("blend space: document rejected by the baker");
    }
}

void DirectionalBlendSpacePanel::EvaluatePreview() noexcept
{
    const Vector2D ScopedDirection = MDocument.Topology == Animation::DirectionalBlendTopology::Ring
                                         ? Vector2D{std::cos(MPreviewAngleRadians), std::sin(MPreviewAngleRadians)}
                                         : MPreviewPoint;
    Animation::EvaluateDirectionalBlend(MTable, ScopedDirection, &MSample);
}

void DirectionalBlendSpacePanel::AfterDocumentChange() noexcept
{
    if (MSelectedAnchor >= MDocument.AnchorCount)
    {
        MSelectedAnchor = MDocument.AnchorCount > 0 ? 0 : kNoBlendAnchor;
    }
    if (MClipPathAnchor >= MDocument.AnchorCount)
    {
        MClipPathAnchor = kNoBlendAnchor;
    }
    RebuildTable();
    EvaluatePreview();
}

void DirectionalBlendSpacePanel::CaptureFrameSnapshot() noexcept
{
    std::memcpy(MFrameSnapshot.data(), &MDocument, sizeof(MDocument));
}

std::uint32_t DirectionalBlendSpacePanel::DominantClip() const noexcept
{
    std::uint32_t ScopedClip = kNoClip;
    float ScopedBest = 0.0f;
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < MSample.Count; ++ScopedIndex)
    {
        if (MSample.Weights[ScopedIndex].Weight > ScopedBest)
        {
            ScopedBest = MSample.Weights[ScopedIndex].Weight;
            ScopedClip = MSample.Weights[ScopedIndex].ClipId;
        }
    }
    return ScopedClip;
}

void DirectionalBlendSpacePanel::SetPreviewAngleDegrees(float InDegrees) noexcept
{
    const float ScopedRadians = InDegrees * (kPi / 180.0f);
    MPreviewAngleRadians = std::atan2(std::sin(ScopedRadians), std::cos(ScopedRadians));
    EvaluatePreview();
}

void DirectionalBlendSpacePanel::SetPreviewDirection(Vector2D InDirection) noexcept
{
    if (!(InDirection.LengthSquared() > 1.0e-12f))
    {
        return;
    }
    MPreviewAngleRadians = std::atan2(InDirection.Y, InDirection.X);
    EvaluatePreview();
}

void DirectionalBlendSpacePanel::SetPreviewPoint(Vector2D InPoint) noexcept
{
    MPreviewPoint = InPoint;
    EvaluatePreview();
}

float DirectionalBlendSpacePanel::PreviewAngleDegrees() const noexcept
{
    return NormalizedDegrees(MPreviewAngleRadians);
}

Vector2D DirectionalBlendSpacePanel::PreviewPoint() const noexcept
{
    return MPreviewPoint;
}

std::uint16_t DirectionalBlendSpacePanel::AddAnchor() noexcept
{
    if (MDocument.AnchorCount >= Animation::kMaxDirectionalAnchors)
    {
        SetStatus("blend space: anchor capacity reached");
        return kNoBlendAnchor;
    }
    Animation::DirectionalBlendAnchor ScopedAnchor;
    if (MDocument.Topology == Animation::DirectionalBlendTopology::Ring)
    {
        ScopedAnchor.Position = {std::cos(MPreviewAngleRadians), std::sin(MPreviewAngleRadians)};
    }
    else
    {
        ScopedAnchor.Position = MPreviewPoint;
    }
    std::uint32_t ScopedNextClip = 0;
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < MDocument.AnchorCount; ++ScopedIndex)
    {
        ScopedNextClip = std::max(ScopedNextClip, MDocument.Anchors[ScopedIndex].ClipId + 1);
    }
    ScopedAnchor.ClipId = ScopedNextClip;
    ScopedAnchor.WeightScale = 1.0f;
    ScopedAnchor.Enabled = true;
    MDocument.Anchors[MDocument.AnchorCount] = ScopedAnchor;
    MClipPaths[MDocument.AnchorCount].clear();
    ++MDocument.AnchorCount;
    MSelectedAnchor = static_cast<std::uint16_t>(MDocument.AnchorCount - 1);
    AfterDocumentChange();
    SetStatus("blend space: anchor added");
    return MSelectedAnchor;
}

bool DirectionalBlendSpacePanel::RemoveAnchor(std::uint16_t InIndex) noexcept
{
    if (InIndex >= MDocument.AnchorCount)
    {
        return false;
    }
    for (std::uint16_t ScopedIndex = InIndex; ScopedIndex + 1 < MDocument.AnchorCount; ++ScopedIndex)
    {
        MDocument.Anchors[ScopedIndex] = MDocument.Anchors[ScopedIndex + 1];
        MClipPaths[ScopedIndex] = std::move(MClipPaths[ScopedIndex + 1]);
    }
    --MDocument.AnchorCount;
    MClipPaths[MDocument.AnchorCount].clear();
    if (MSelectedAnchor == InIndex)
    {
        MSelectedAnchor = MDocument.AnchorCount > 0
                              ? static_cast<std::uint16_t>(std::min<std::uint32_t>(InIndex, MDocument.AnchorCount - 1))
                              : kNoBlendAnchor;
    }
    else if (MSelectedAnchor != kNoBlendAnchor && MSelectedAnchor > InIndex)
    {
        --MSelectedAnchor;
    }
    if (MClipPathAnchor == InIndex)
    {
        MClipPathAnchor = kNoBlendAnchor;
    }
    AfterDocumentChange();
    SetStatus("blend space: anchor removed");
    return true;
}

void DirectionalBlendSpacePanel::SetTopology(Animation::DirectionalBlendTopology InTopology) noexcept
{
    if (MDocument.Topology == InTopology)
    {
        return;
    }
    MDocument.Topology = InTopology;
    if (InTopology == Animation::DirectionalBlendTopology::Ring)
    {
        // A plane anchor may sit at the origin. Ring sees it as a missing
        // heading, so such an anchor falls back to the positive X axis.
        for (std::uint16_t ScopedIndex = 0; ScopedIndex < MDocument.AnchorCount; ++ScopedIndex)
        {
            if (!(MDocument.Anchors[ScopedIndex].Position.LengthSquared() > 1.0e-12f))
            {
                MDocument.Anchors[ScopedIndex].Position = {1.0f, 0.0f};
            }
        }
    }
    AfterDocumentChange();
}

bool DirectionalBlendSpacePanel::SetClipPath(std::uint16_t InAnchor, std::string_view InPath) noexcept
{
    if (InAnchor >= MDocument.AnchorCount)
    {
        return false;
    }
    MClipPaths[InAnchor].assign(InPath);
    if (MClipPathAnchor == InAnchor)
    {
        MClipPathAnchor = kNoBlendAnchor;
    }
    return true;
}

std::string_view DirectionalBlendSpacePanel::ClipPath(std::uint16_t InAnchor) const noexcept
{
    if (InAnchor >= MDocument.AnchorCount)
    {
        return {};
    }
    return MClipPaths[InAnchor];
}

bool DirectionalBlendSpacePanel::SaveToFile(const std::filesystem::path& InPath)
{
    std::string ScopedError;
    if (!DirectionalBlendFile::Save(InPath, MDocument, MClipPaths, &ScopedError))
    {
        SetStatus("blend space: %s", ScopedError.c_str());
        return false;
    }
    SetStatus("blend space: saved %u anchors", static_cast<unsigned>(MDocument.AnchorCount));
    return true;
}

bool DirectionalBlendSpacePanel::LoadFromFile(const std::filesystem::path& InPath)
{
    std::string ScopedError;
    Animation::DirectionalBlendSpaceDocument ScopedDocument;
    DirectionalBlendFile::PathArray ScopedPaths{};
    if (!DirectionalBlendFile::Load(InPath, &ScopedDocument, &ScopedPaths, &ScopedError))
    {
        SetStatus("blend space: %s", ScopedError.c_str());
        return false;
    }
    MDocument = ScopedDocument;
    MClipPaths = ScopedPaths;
    MSelectedAnchor = MDocument.AnchorCount > 0 ? 0 : kNoBlendAnchor;
    MClipPathAnchor = kNoBlendAnchor;
    MPreviewAngleRadians = 0.0f;
    MPreviewPoint = {};
    RebuildTable();
    EvaluatePreview();
    SetStatus("blend space: loaded %u anchors", static_cast<unsigned>(MDocument.AnchorCount));
    return true;
}

void DirectionalBlendSpacePanel::NotifyHistoryChanged() noexcept
{
    if (MSelectedAnchor >= MDocument.AnchorCount)
    {
        MSelectedAnchor = MDocument.AnchorCount > 0 ? 0 : kNoBlendAnchor;
    }
    if (MClipPathAnchor >= MDocument.AnchorCount)
    {
        MClipPathAnchor = kNoBlendAnchor;
    }
    RebuildTable();
    EvaluatePreview();
}

void DirectionalBlendSpacePanel::CommitPendingEdit() noexcept
{
    if (MEditing)
    {
        CommitEdit();
        AfterDocumentChange();
    }
}

void DirectionalBlendSpacePanel::Draw(const char* InWindowTitle, bool* InOpen)
{
    if (InWindowTitle == nullptr)
    {
        return;
    }
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }
    CaptureFrameSnapshot();
    if (!BeginEditorPanel(InWindowTitle, InOpen, ImGuiWindowFlags_NoScrollbar))
    {
        EndEditorPanel();
        return;
    }
    DrawToolbar();
    const ImVec2 ScopedAvailable = ImGui::GetContentRegionAvail();
    const bool ScopedSplit = ScopedAvailable.x >= 720.0f;
    if (ScopedSplit)
    {
        const float ScopedCanvasWidth = std::max(320.0f, ScopedAvailable.x - 360.0f);
        if (ImGui::BeginChild("##directional_blend_canvas", ImVec2(ScopedCanvasWidth, 0.0f), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            DrawCanvas();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##directional_blend_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawInspector();
        }
        ImGui::EndChild();
    }
    else
    {
        const float ScopedCanvasHeight = std::max(240.0f, ScopedAvailable.y * 0.55f);
        if (ImGui::BeginChild("##directional_blend_canvas", ImVec2(0.0f, ScopedCanvasHeight), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            DrawCanvas();
        }
        ImGui::EndChild();
        if (ImGui::BeginChild("##directional_blend_inspector", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            DrawInspector();
        }
        ImGui::EndChild();
    }
    EndEditorPanel();
}

void DirectionalBlendSpacePanel::DrawToolbar()
{
    ImGui::TextUnformatted("Directional Blend Space");
    ImGui::SameLine();
    ImGui::TextDisabled("%u / %u anchors", static_cast<unsigned>(MDocument.AnchorCount),
                        static_cast<unsigned>(Animation::kMaxDirectionalAnchors));
    ImGui::SameLine();
    if (ImGui::Button("Add anchor"))
    {
        (void)AddAnchor();
        CommitSnapshotEdit("Add blend anchor");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(MSelectedAnchor == kNoBlendAnchor);
    if (ImGui::Button("Remove anchor"))
    {
        (void)RemoveAnchor(MSelectedAnchor);
        CommitSnapshotEdit("Remove blend anchor");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Re-evaluate"))
    {
        RebuildTable();
        EvaluatePreview();
    }
    if (MStatus[0] != '\0')
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", MStatus.data());
    }
}

void DirectionalBlendSpacePanel::DrawCanvas()
{
    const ImVec2 ScopedAvailable = ImGui::GetContentRegionAvail();
    const ImVec2 ScopedSize{std::max(ScopedAvailable.x, 200.0f), std::max(ScopedAvailable.y, 200.0f)};
    const ImVec2 ScopedOrigin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##directional_blend_field", ScopedSize, ImGuiButtonFlags_MouseButtonLeft);
    HandleCanvasInput(ScopedOrigin, ScopedSize);
    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    ScopedDraw->AddRectFilled(ScopedOrigin, ImVec2(ScopedOrigin.x + ScopedSize.x, ScopedOrigin.y + ScopedSize.y),
                              IM_COL32(20, 19, 26, 255), 6.0f);
    if (MDocument.Topology == Animation::DirectionalBlendTopology::Ring)
    {
        DrawRingCanvas(ScopedOrigin, ScopedSize);
    }
    else
    {
        DrawPlaneCanvas(ScopedOrigin, ScopedSize);
    }
    ScopedDraw->AddRect(ScopedOrigin, ImVec2(ScopedOrigin.x + ScopedSize.x, ScopedOrigin.y + ScopedSize.y),
                        IM_COL32(72, 66, 88, 255), 6.0f);
}

void DirectionalBlendSpacePanel::DrawRingCanvas(const ImVec2& InOrigin, const ImVec2& InSize)
{
    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    const ImVec2 ScopedCenter = CanvasCenter(InOrigin, InSize);
    const float ScopedRadius = CanvasRadius(InSize);
    ScopedDraw->AddCircle(ScopedCenter, ScopedRadius, IM_COL32(92, 84, 112, 255), 96, 2.0f);
    ScopedDraw->AddCircle(ScopedCenter, ScopedRadius * 0.5f, IM_COL32(58, 53, 70, 255), 64, 1.0f);

    for (std::uint16_t ScopedIndex = 0; ScopedIndex < MDocument.AnchorCount; ++ScopedIndex)
    {
        const Animation::DirectionalBlendAnchor& ScopedAnchor = MDocument.Anchors[ScopedIndex];
        Vector2D ScopedDirection = ScopedAnchor.Position;
        const float ScopedLength = std::sqrt(ScopedDirection.LengthSquared());
        ScopedDirection = ScopedLength > 1.0e-6f ? ScopedDirection / ScopedLength : Vector2D{1.0f, 0.0f};
        const ImVec2 ScopedPoint = DirectionScreen(InOrigin, InSize, ScopedDirection);
        const bool ScopedActive = WeightOfClip(MSample, ScopedAnchor.ClipId) > 0.01f;
        ScopedDraw->AddLine(ScopedCenter, ScopedPoint,
                            ScopedActive ? IM_COL32(196, 140, 224, 190) : IM_COL32(64, 59, 76, 140),
                            ScopedActive ? 2.5f : 1.0f);
        const ImU32 ScopedColor = AnchorColor(ScopedIndex == MSelectedAnchor, ScopedAnchor.Enabled, ScopedActive);
        ScopedDraw->AddCircleFilled(ScopedPoint, ScopedIndex == MSelectedAnchor ? 8.0f : 6.0f, ScopedColor);
        char ScopedLabel[24] = {};
        std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%u", static_cast<unsigned>(ScopedIndex));
        ScopedDraw->AddText(ImVec2(ScopedPoint.x + 10.0f, ScopedPoint.y - 16.0f), IM_COL32(210, 208, 220, 255),
                            ScopedLabel);
    }

    const Vector2D ScopedPreview{std::cos(MPreviewAngleRadians), std::sin(MPreviewAngleRadians)};
    const ImVec2 ScopedHandle = DirectionScreen(InOrigin, InSize, ScopedPreview);
    ScopedDraw->AddLine(ScopedCenter, ScopedHandle, IM_COL32(120, 220, 255, 230), 2.0f);
    ScopedDraw->AddCircleFilled(ScopedHandle, 7.0f, IM_COL32(120, 220, 255, 255));
    ScopedDraw->AddCircle(ScopedHandle, 11.0f, IM_COL32(120, 220, 255, 120), 24, 1.5f);

    char ScopedHeading[48] = {};
    std::snprintf(ScopedHeading, sizeof(ScopedHeading), "heading %.1f deg", static_cast<double>(PreviewAngleDegrees()));
    ScopedDraw->AddText(ImVec2(InOrigin.x + 12.0f, InOrigin.y + 10.0f), IM_COL32(196, 196, 208, 255), ScopedHeading);
}

void DirectionalBlendSpacePanel::DrawPlaneCanvas(const ImVec2& InOrigin, const ImVec2& InSize)
{
    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    const PlaneView ScopedView = MakePlaneView(InOrigin, InSize);
    const Vector2D ScopedTopLeft = ScreenToWorld(ScopedView, InOrigin);
    const Vector2D ScopedBottomRight = ScreenToWorld(ScopedView, ImVec2(InOrigin.x + InSize.x, InOrigin.y + InSize.y));

    float ScopedStep = 1.0f;
    if (ScopedView.Scale > 0.0f)
    {
        while (ScopedStep * ScopedView.Scale < 42.0f)
        {
            ScopedStep *= 2.0f;
        }
        while (ScopedStep * ScopedView.Scale > 140.0f)
        {
            ScopedStep *= 0.5f;
        }
    }
    const ImU32 ScopedGrid = IM_COL32(52, 48, 64, 160);
    std::uint32_t ScopedLines = 0;
    for (float ScopedX = std::floor(ScopedTopLeft.X / ScopedStep) * ScopedStep;
         ScopedX <= ScopedBottomRight.X && ScopedLines < 400; ScopedX += ScopedStep, ++ScopedLines)
    {
        const ImVec2 ScopedFrom = WorldToScreen(ScopedView, {ScopedX, ScopedBottomRight.Y});
        const ImVec2 ScopedTo = WorldToScreen(ScopedView, {ScopedX, ScopedTopLeft.Y});
        ScopedDraw->AddLine(ScopedFrom, ScopedTo, ScopedGrid, 1.0f);
    }
    ScopedLines = 0;
    for (float ScopedY = std::floor(ScopedBottomRight.Y / ScopedStep) * ScopedStep;
         ScopedY <= ScopedTopLeft.Y && ScopedLines < 400; ScopedY += ScopedStep, ++ScopedLines)
    {
        const ImVec2 ScopedFrom = WorldToScreen(ScopedView, {ScopedTopLeft.X, ScopedY});
        const ImVec2 ScopedTo = WorldToScreen(ScopedView, {ScopedBottomRight.X, ScopedY});
        ScopedDraw->AddLine(ScopedFrom, ScopedTo, ScopedGrid, 1.0f);
    }
    if (ScopedTopLeft.X <= 0.0f && ScopedBottomRight.X >= 0.0f)
    {
        ScopedDraw->AddLine(WorldToScreen(ScopedView, {0.0f, ScopedBottomRight.Y}),
                            WorldToScreen(ScopedView, {0.0f, ScopedTopLeft.Y}), IM_COL32(84, 78, 100, 220), 1.5f);
    }
    if (ScopedBottomRight.Y <= 0.0f && ScopedTopLeft.Y >= 0.0f)
    {
        ScopedDraw->AddLine(WorldToScreen(ScopedView, {ScopedTopLeft.X, 0.0f}),
                            WorldToScreen(ScopedView, {ScopedBottomRight.X, 0.0f}), IM_COL32(84, 78, 100, 220), 1.5f);
    }

    for (std::uint16_t ScopedIndex = 0; ScopedIndex < MDocument.AnchorCount; ++ScopedIndex)
    {
        const Animation::DirectionalBlendAnchor& ScopedAnchor = MDocument.Anchors[ScopedIndex];
        const ImVec2 ScopedPoint = WorldToScreen(ScopedView, ScopedAnchor.Position);
        const bool ScopedActive = WeightOfClip(MSample, ScopedAnchor.ClipId) > 0.01f;
        const ImU32 ScopedColor = AnchorColor(ScopedIndex == MSelectedAnchor, ScopedAnchor.Enabled, ScopedActive);
        ScopedDraw->AddCircleFilled(ScopedPoint, ScopedIndex == MSelectedAnchor ? 8.0f : 6.0f, ScopedColor);
        char ScopedLabel[24] = {};
        std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%u", static_cast<unsigned>(ScopedIndex));
        ScopedDraw->AddText(ImVec2(ScopedPoint.x + 10.0f, ScopedPoint.y - 16.0f), IM_COL32(210, 208, 220, 255),
                            ScopedLabel);
    }

    const ImVec2 ScopedHandle = WorldToScreen(ScopedView, MPreviewPoint);
    ScopedDraw->AddCircleFilled(ScopedHandle, 7.0f, IM_COL32(120, 220, 255, 255));
    ScopedDraw->AddCircle(ScopedHandle, 11.0f, IM_COL32(120, 220, 255, 120), 24, 1.5f);
    char ScopedText[64] = {};
    std::snprintf(ScopedText, sizeof(ScopedText), "sample %.2f %.2f", static_cast<double>(MPreviewPoint.X),
                  static_cast<double>(MPreviewPoint.Y));
    ScopedDraw->AddText(ImVec2(InOrigin.x + 12.0f, InOrigin.y + 10.0f), IM_COL32(196, 196, 208, 255), ScopedText);
}

void DirectionalBlendSpacePanel::DrawInspector()
{
    ImGui::TextUnformatted("Blend space");
    ImGui::Separator();
    int ScopedTopology = static_cast<int>(MDocument.Topology);
    if (ImGui::Combo("Topology", &ScopedTopology, "Ring\0Plane\0"))
    {
        SetTopology(static_cast<Animation::DirectionalBlendTopology>(ScopedTopology));
        CommitSnapshotEdit("Change blend topology");
    }
    if (ImGui::Checkbox("Normalize weights", &MDocument.NormalizeWeights))
    {
        AfterDocumentChange();
        CommitSnapshotEdit("Toggle blend normalization");
    }
    ImGui::Spacing();
    DrawAnchorList();
    ImGui::Spacing();
    DrawAnchorInspector();
    ImGui::Spacing();
    DrawPreview();
    ImGui::Spacing();
    DrawFile();
}

void DirectionalBlendSpacePanel::DrawAnchorList()
{
    ImGui::TextUnformatted("Anchors");
    ImGui::Separator();
    if (MDocument.AnchorCount == 0)
    {
        ImGui::TextDisabled("No anchors");
        return;
    }
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < MDocument.AnchorCount; ++ScopedIndex)
    {
        const Animation::DirectionalBlendAnchor& ScopedAnchor = MDocument.Anchors[ScopedIndex];
        const float ScopedWeight = WeightOfClip(MSample, ScopedAnchor.ClipId);
        char ScopedLabel[128] = {};
        std::snprintf(ScopedLabel, sizeof(ScopedLabel), "#%u %s%s  %.2f", static_cast<unsigned>(ScopedIndex),
                      ClipSummary(MClipPaths[ScopedIndex], ScopedAnchor.ClipId).c_str(),
                      ScopedAnchor.Enabled ? "" : " (off)", static_cast<double>(ScopedWeight));
        if (ImGui::Selectable(ScopedLabel, MSelectedAnchor == ScopedIndex))
        {
            SelectAnchor(ScopedIndex);
        }
    }
}

void DirectionalBlendSpacePanel::DrawAnchorInspector()
{
    if (MSelectedAnchor == kNoBlendAnchor || MSelectedAnchor >= MDocument.AnchorCount)
    {
        ImGui::TextDisabled("Select an anchor to edit it");
        return;
    }
    Animation::DirectionalBlendAnchor& ScopedAnchor = MDocument.Anchors[MSelectedAnchor];
    ImGui::Text("Anchor %u", static_cast<unsigned>(MSelectedAnchor));
    ImGui::PushID(static_cast<int>(MSelectedAnchor));

    if (ImGui::Checkbox("Enabled", &ScopedAnchor.Enabled))
    {
        AfterDocumentChange();
        CommitSnapshotEdit("Toggle blend anchor");
    }

    int ScopedClip = static_cast<int>(ScopedAnchor.ClipId);
    if (ImGui::InputInt("Clip id", &ScopedClip))
    {
        ScopedAnchor.ClipId = ScopedClip < 0 ? 0u : static_cast<std::uint32_t>(ScopedClip);
        StartFieldEdit("Set blend clip id");
        AfterDocumentChange();
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        FinishFieldEdit();
    }

    float ScopedScale = ScopedAnchor.WeightScale;
    if (ImGui::DragFloat("Weight scale", &ScopedScale, 0.01f, 0.0f, 4.0f, "%.3f"))
    {
        ScopedAnchor.WeightScale = std::max(ScopedScale, 0.0f);
        StartFieldEdit("Set blend anchor weight");
        AfterDocumentChange();
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        FinishFieldEdit();
    }

    if (MDocument.Topology == Animation::DirectionalBlendTopology::Ring)
    {
        float ScopedDegrees = NormalizedDegrees(std::atan2(ScopedAnchor.Position.Y, ScopedAnchor.Position.X));
        if (ImGui::DragFloat("Heading", &ScopedDegrees, 0.5f, 0.0f, 360.0f, "%.1f deg"))
        {
            const float ScopedRadians = ScopedDegrees * (kPi / 180.0f);
            ScopedAnchor.Position = {std::cos(ScopedRadians), std::sin(ScopedRadians)};
            StartFieldEdit("Set blend anchor heading");
            AfterDocumentChange();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            FinishFieldEdit();
        }
    }
    else
    {
        float ScopedPoint[2] = {ScopedAnchor.Position.X, ScopedAnchor.Position.Y};
        if (ImGui::DragFloat2("Position", ScopedPoint, 0.01f))
        {
            ScopedAnchor.Position = {ScopedPoint[0], ScopedPoint[1]};
            StartFieldEdit("Set blend anchor position");
            AfterDocumentChange();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            FinishFieldEdit();
        }
    }

    SyncClipPathBuffer();
    if (ImGui::InputText("Clip path", MClipPathBuffer.data(), MClipPathBuffer.size()))
    {
        MClipPaths[MSelectedAnchor] = MClipPathBuffer.data();
    }
    ImGui::TextDisabled("Clip id is the runtime key; the path is editor metadata");
    ImGui::PopID();
}

void DirectionalBlendSpacePanel::DrawPreview()
{
    ImGui::TextUnformatted("Preview");
    ImGui::Separator();
    if (MDocument.Topology == Animation::DirectionalBlendTopology::Ring)
    {
        float ScopedDegrees = PreviewAngleDegrees();
        if (ImGui::DragFloat("Heading", &ScopedDegrees, 0.25f, 0.0f, 360.0f, "%.1f deg"))
        {
            SetPreviewAngleDegrees(ScopedDegrees);
        }
    }
    else
    {
        float ScopedPoint[2] = {MPreviewPoint.X, MPreviewPoint.Y};
        if (ImGui::DragFloat2("Sample", ScopedPoint, 0.01f))
        {
            SetPreviewPoint({ScopedPoint[0], ScopedPoint[1]});
        }
    }
    ImGui::Text("Sources: %u", static_cast<unsigned>(MSample.Count));
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < MSample.Count; ++ScopedIndex)
    {
        const Animation::DirectionalBlendWeight& ScopedWeight = MSample.Weights[ScopedIndex];
        char ScopedOverlay[64] = {};
        std::snprintf(ScopedOverlay, sizeof(ScopedOverlay), "clip %u  %.3f", static_cast<unsigned>(ScopedWeight.ClipId),
                      static_cast<double>(ScopedWeight.Weight));
        ImGui::ProgressBar(std::clamp(ScopedWeight.Weight, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), ScopedOverlay);
    }
    if (MSample.Count == 0)
    {
        ImGui::TextDisabled("No source at this sample");
    }
    else
    {
        ImGui::Text("Dominant: clip %u", static_cast<unsigned>(DominantClip()));
    }
}

void DirectionalBlendSpacePanel::DrawFile()
{
    ImGui::TextUnformatted("File");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0f);
    (void)ImGui::InputText("##blend_path", MPath.data(), MPath.size());
    if (ImGui::Button("Save"))
    {
        (void)SaveToFile(std::filesystem::path(MPath.data()));
    }
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        (void)LoadFromFile(std::filesystem::path(MPath.data()));
    }
    ImGui::SameLine();
    (void)ImGui::Button("Drop .fblend here");
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* ScopedPayload = ImGui::AcceptDragDropPayload(ContentBrowserPanel::kDragPayloadType))
        {
            if (ScopedPayload->DataSize == static_cast<int>(sizeof(ContentBrowserPanel::DragPayload)) &&
                ScopedPayload->Data != nullptr)
            {
                const auto* ScopedData = static_cast<const ContentBrowserPanel::DragPayload*>(ScopedPayload->Data);
                std::snprintf(MPath.data(), MPath.size(), "%s", ScopedData->path);
                if (!LoadFromFile(std::filesystem::path(MPath.data())))
                {
                    (void)LoadFromFile(std::filesystem::path("assets") / MPath.data());
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (MStatus[0] != '\0')
    {
        ImGui::TextDisabled("%s", MStatus.data());
    }
}

ImVec2 DirectionalBlendSpacePanel::CanvasCenter(const ImVec2& InOrigin, const ImVec2& InSize) const noexcept
{
    return ImVec2(InOrigin.x + InSize.x * 0.5f, InOrigin.y + InSize.y * 0.5f);
}

float DirectionalBlendSpacePanel::CanvasRadius(const ImVec2& InSize) const noexcept
{
    return std::max(40.0f, std::min(InSize.x, InSize.y) * 0.5f - kCanvasPadding);
}

ImVec2 DirectionalBlendSpacePanel::DirectionScreen(const ImVec2& InOrigin, const ImVec2& InSize,
                                                   Vector2D InDirection) const noexcept
{
    const ImVec2 ScopedCenter = CanvasCenter(InOrigin, InSize);
    const float ScopedRadius = CanvasRadius(InSize);
    return ImVec2(ScopedCenter.x + InDirection.X * ScopedRadius, ScopedCenter.y - InDirection.Y * ScopedRadius);
}

ImVec2 DirectionalBlendSpacePanel::AnchorScreen(std::uint16_t InAnchor, const ImVec2& InOrigin,
                                                const ImVec2& InSize) const noexcept
{
    if (InAnchor >= MDocument.AnchorCount)
    {
        return ImVec2(0.0f, 0.0f);
    }
    const Animation::DirectionalBlendAnchor& ScopedAnchor = MDocument.Anchors[InAnchor];
    if (MDocument.Topology == Animation::DirectionalBlendTopology::Ring)
    {
        Vector2D ScopedDirection = ScopedAnchor.Position;
        const float ScopedLength = std::sqrt(ScopedDirection.LengthSquared());
        ScopedDirection = ScopedLength > 1.0e-6f ? ScopedDirection / ScopedLength : Vector2D{1.0f, 0.0f};
        return DirectionScreen(InOrigin, InSize, ScopedDirection);
    }
    return WorldToScreen(MakePlaneView(InOrigin, InSize), ScopedAnchor.Position);
}

DirectionalBlendSpacePanel::PlaneView DirectionalBlendSpacePanel::MakePlaneView(const ImVec2& InOrigin,
                                                                                const ImVec2& InSize) const noexcept
{
    float ScopedMinX = 0.0f;
    float ScopedMaxX = 0.0f;
    float ScopedMinY = 0.0f;
    float ScopedMaxY = 0.0f;
    bool ScopedAny = false;
    const auto ScopedInclude = [&](Vector2D InPoint)
    {
        if (!ScopedAny)
        {
            ScopedMinX = InPoint.X;
            ScopedMaxX = InPoint.X;
            ScopedMinY = InPoint.Y;
            ScopedMaxY = InPoint.Y;
            ScopedAny = true;
            return;
        }
        ScopedMinX = std::min(ScopedMinX, InPoint.X);
        ScopedMaxX = std::max(ScopedMaxX, InPoint.X);
        ScopedMinY = std::min(ScopedMinY, InPoint.Y);
        ScopedMaxY = std::max(ScopedMaxY, InPoint.Y);
    };
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < MDocument.AnchorCount; ++ScopedIndex)
    {
        ScopedInclude(MDocument.Anchors[ScopedIndex].Position);
    }
    ScopedInclude(MPreviewPoint);
    if (!ScopedAny)
    {
        ScopedMinX = -1.0f;
        ScopedMaxX = 1.0f;
        ScopedMinY = -1.0f;
        ScopedMaxY = 1.0f;
    }
    if (ScopedMaxX - ScopedMinX < 1.0e-4f)
    {
        ScopedMinX -= 0.5f;
        ScopedMaxX += 0.5f;
    }
    if (ScopedMaxY - ScopedMinY < 1.0e-4f)
    {
        ScopedMinY -= 0.5f;
        ScopedMaxY += 0.5f;
    }
    const float ScopedScaleX = (InSize.x - 2.0f * kCanvasPadding) / (ScopedMaxX - ScopedMinX);
    const float ScopedScaleY = (InSize.y - 2.0f * kCanvasPadding) / (ScopedMaxY - ScopedMinY);
    PlaneView ScopedView;
    ScopedView.Scale = std::max(0.001f, std::min(ScopedScaleX, ScopedScaleY));
    ScopedView.WorldCenter = {(ScopedMinX + ScopedMaxX) * 0.5f, (ScopedMinY + ScopedMaxY) * 0.5f};
    ScopedView.ScreenCenter = {InOrigin.x + InSize.x * 0.5f, InOrigin.y + InSize.y * 0.5f};
    return ScopedView;
}

ImVec2 DirectionalBlendSpacePanel::WorldToScreen(const PlaneView& InView, Vector2D InPoint) const noexcept
{
    return ImVec2(InView.ScreenCenter.x + (InPoint.X - InView.WorldCenter.X) * InView.Scale,
                  InView.ScreenCenter.y - (InPoint.Y - InView.WorldCenter.Y) * InView.Scale);
}

Vector2D DirectionalBlendSpacePanel::ScreenToWorld(const PlaneView& InView, const ImVec2& InPoint) const noexcept
{
    return Vector2D{InView.WorldCenter.X + (InPoint.x - InView.ScreenCenter.x) / InView.Scale,
                    InView.WorldCenter.Y - (InPoint.y - InView.ScreenCenter.y) / InView.Scale};
}

std::uint16_t DirectionalBlendSpacePanel::HitAnchor(const ImVec2& InPoint, const ImVec2& InOrigin,
                                                    const ImVec2& InSize) const noexcept
{
    for (std::uint16_t ScopedIndex = 0; ScopedIndex < MDocument.AnchorCount; ++ScopedIndex)
    {
        if (Near(InPoint, AnchorScreen(ScopedIndex, InOrigin, InSize), kAnchorHitRadius))
        {
            return ScopedIndex;
        }
    }
    return kNoBlendAnchor;
}

bool DirectionalBlendSpacePanel::HitDirectionHandle(const ImVec2& InPoint, const ImVec2& InOrigin,
                                                    const ImVec2& InSize) const noexcept
{
    if (MDocument.Topology == Animation::DirectionalBlendTopology::Ring)
    {
        const Vector2D ScopedDirection{std::cos(MPreviewAngleRadians), std::sin(MPreviewAngleRadians)};
        return Near(InPoint, DirectionScreen(InOrigin, InSize, ScopedDirection), kDirectionHitRadius);
    }
    return Near(InPoint, WorldToScreen(MakePlaneView(InOrigin, InSize), MPreviewPoint), kDirectionHitRadius);
}

void DirectionalBlendSpacePanel::SetPreviewFromScreen(const ImVec2& InPoint, const ImVec2& InOrigin,
                                                      const ImVec2& InSize) noexcept
{
    if (MDocument.Topology == Animation::DirectionalBlendTopology::Ring)
    {
        const ImVec2 ScopedCenter = CanvasCenter(InOrigin, InSize);
        const float ScopedRadius = std::max(CanvasRadius(InSize), 1.0f);
        Vector2D ScopedDirection{(InPoint.x - ScopedCenter.x) / ScopedRadius,
                                 -(InPoint.y - ScopedCenter.y) / ScopedRadius};
        if (!(ScopedDirection.LengthSquared() > 1.0e-9f))
        {
            return;
        }
        ScopedDirection = ScopedDirection / std::sqrt(ScopedDirection.LengthSquared());
        MPreviewAngleRadians = std::atan2(ScopedDirection.Y, ScopedDirection.X);
        EvaluatePreview();
        return;
    }
    MPreviewPoint = ScreenToWorld(MakePlaneView(InOrigin, InSize), InPoint);
    EvaluatePreview();
}

void DirectionalBlendSpacePanel::MoveAnchorFromScreen(std::uint16_t InAnchor, const ImVec2& InPoint,
                                                      const ImVec2& InOrigin, const ImVec2& InSize) noexcept
{
    if (InAnchor >= MDocument.AnchorCount)
    {
        return;
    }
    Animation::DirectionalBlendAnchor& ScopedAnchor = MDocument.Anchors[InAnchor];
    if (MDocument.Topology == Animation::DirectionalBlendTopology::Ring)
    {
        const ImVec2 ScopedCenter = CanvasCenter(InOrigin, InSize);
        Vector2D ScopedDirection{(InPoint.x - ScopedCenter.x), -(InPoint.y - ScopedCenter.y)};
        if (!(ScopedDirection.LengthSquared() > 1.0e-9f))
        {
            return;
        }
        ScopedDirection = ScopedDirection / std::sqrt(ScopedDirection.LengthSquared());
        ScopedAnchor.Position = ScopedDirection;
    }
    else
    {
        ScopedAnchor.Position = ScreenToWorld(MakePlaneView(InOrigin, InSize), InPoint);
    }
    AfterDocumentChange();
}

void DirectionalBlendSpacePanel::HandleCanvasInput(const ImVec2& InOrigin, const ImVec2& InSize)
{
    const bool ScopedActive = ImGui::IsItemActive();
    const ImVec2 ScopedMouse = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated())
    {
        MDragAnchor = kNoBlendAnchor;
        MDraggingDirection = false;
        if (HitDirectionHandle(ScopedMouse, InOrigin, InSize))
        {
            MDraggingDirection = true;
            SetPreviewFromScreen(ScopedMouse, InOrigin, InSize);
        }
        else
        {
            const std::uint16_t ScopedHit = HitAnchor(ScopedMouse, InOrigin, InSize);
            if (ScopedHit != kNoBlendAnchor)
            {
                SelectAnchor(ScopedHit);
                MDragAnchor = ScopedHit;
                std::memcpy(MEditBefore.data(), MFrameSnapshot.data(), MEditBefore.size());
                (void)OpenEdit("Move blend anchor");
            }
            else
            {
                MDraggingDirection = true;
                SetPreviewFromScreen(ScopedMouse, InOrigin, InSize);
            }
        }
    }
    if (ScopedActive)
    {
        if (MDragAnchor != kNoBlendAnchor)
        {
            MoveAnchorFromScreen(MDragAnchor, ScopedMouse, InOrigin, InSize);
        }
        else if (MDraggingDirection)
        {
            SetPreviewFromScreen(ScopedMouse, InOrigin, InSize);
        }
    }
    if (ImGui::IsItemDeactivated())
    {
        if (MDragAnchor != kNoBlendAnchor)
        {
            CommitEdit();
        }
        MDragAnchor = kNoBlendAnchor;
        MDraggingDirection = false;
    }
}

bool DirectionalBlendSpacePanel::OpenEdit(const char* InLabel) noexcept
{
    if (MEditing)
    {
        return true;
    }
    if (MCommands == nullptr || MCodec == 0xFFFFu)
    {
        return false;
    }
    const auto ScopedBegun = MCommands->BeginTransaction(InLabel);
    if (ScopedBegun != CommandStack::Result::Ok)
    {
        SetStatus("blend space: transaction rejected: %s", CommandStack::ResultName(ScopedBegun));
        return false;
    }
    MEditing = true;
    return true;
}

void DirectionalBlendSpacePanel::RecordEdit() noexcept
{
    if (!MEditing || MCommands == nullptr || MCodec == 0xFFFFu)
    {
        return;
    }
    const auto ScopedRecorded = MCommands->Record(
        Fby::kNullEntity, MCodec, std::span<const std::byte>(MEditBefore.data(), sizeof(MDocument)),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&MDocument), sizeof(MDocument)));
    if (ScopedRecorded != CommandStack::Result::Ok)
    {
        SetStatus("blend space: document delta rejected: %s", CommandStack::ResultName(ScopedRecorded));
    }
}

void DirectionalBlendSpacePanel::CommitEdit() noexcept
{
    if (!MEditing)
    {
        return;
    }
    if (std::memcmp(MEditBefore.data(), &MDocument, sizeof(MDocument)) == 0)
    {
        if (MCommands != nullptr)
        {
            MCommands->CancelTransaction();
        }
        MEditing = false;
        return;
    }
    RecordEdit();
    const auto ScopedCommitted = MCommands->CommitTransaction();
    MEditing = false;
    if (ScopedCommitted != CommandStack::Result::Ok)
    {
        SetStatus("blend space: commit rejected: %s", CommandStack::ResultName(ScopedCommitted));
    }
}

void DirectionalBlendSpacePanel::CancelEdit() noexcept
{
    if (MEditing && MCommands != nullptr)
    {
        MCommands->CancelTransaction();
    }
    MEditing = false;
}

void DirectionalBlendSpacePanel::StartFieldEdit(const char* InLabel) noexcept
{
    if (MEditing)
    {
        return;
    }
    std::memcpy(MEditBefore.data(), MFrameSnapshot.data(), MEditBefore.size());
    (void)OpenEdit(InLabel);
}

void DirectionalBlendSpacePanel::FinishFieldEdit() noexcept
{
    if (MEditing)
    {
        CommitEdit();
    }
    AfterDocumentChange();
}

void DirectionalBlendSpacePanel::CommitSnapshotEdit(const char* InLabel) noexcept
{
    if (std::memcmp(MFrameSnapshot.data(), &MDocument, sizeof(MDocument)) == 0)
    {
        return;
    }
    AfterDocumentChange();
    if (MCommands == nullptr || MCodec == 0xFFFFu)
    {
        return;
    }
    if (MEditing)
    {
        CommitEdit();
    }
    const auto ScopedBegun = MCommands->BeginTransaction(InLabel);
    if (ScopedBegun != CommandStack::Result::Ok)
    {
        SetStatus("blend space: transaction rejected: %s", CommandStack::ResultName(ScopedBegun));
        return;
    }
    const auto ScopedRecorded = MCommands->Record(
        Fby::kNullEntity, MCodec, std::span<const std::byte>(MFrameSnapshot.data(), sizeof(MDocument)),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(&MDocument), sizeof(MDocument)));
    if (ScopedRecorded != CommandStack::Result::Ok)
    {
        MCommands->CancelTransaction();
        SetStatus("blend space: document delta rejected: %s", CommandStack::ResultName(ScopedRecorded));
        return;
    }
    const auto ScopedCommitted = MCommands->CommitTransaction();
    if (ScopedCommitted != CommandStack::Result::Ok)
    {
        SetStatus("blend space: commit rejected: %s", CommandStack::ResultName(ScopedCommitted));
    }
}

void DirectionalBlendSpacePanel::SetStatus(const char* InFormat, ...) noexcept
{
    va_list ScopedArguments;
    va_start(ScopedArguments, InFormat);
    std::vsnprintf(MStatus.data(), MStatus.size(), InFormat, ScopedArguments);
    va_end(ScopedArguments);
}

void DirectionalBlendSpacePanel::SyncClipPathBuffer() noexcept
{
    if (MClipPathAnchor == MSelectedAnchor)
    {
        return;
    }
    MClipPathAnchor = MSelectedAnchor;
    const std::string_view ScopedPath = ClipPath(MSelectedAnchor);
    const std::size_t ScopedLength = std::min(ScopedPath.size(), MClipPathBuffer.size() - 1);
    std::memcpy(MClipPathBuffer.data(), ScopedPath.data(), ScopedLength);
    MClipPathBuffer[ScopedLength] = '\0';
}
}
