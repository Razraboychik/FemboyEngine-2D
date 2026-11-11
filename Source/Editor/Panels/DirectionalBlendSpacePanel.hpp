// FemboyEngine2D - visual editor for a directional blend space.
#pragma once

#include "Editor/Infrastructure/CommandStack.hpp"
#include "Engine/ECS/DirectionalBlend.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Fby::Editor
{

inline constexpr std::uint16_t kNoBlendAnchor = 0xFFFFu;

// Ring mode lays the anchors out as headings on a circle and previews one
// normalized direction. Plane mode samples a 2D coordinate, so strafe and
// forward can carry different values. The panel edits the authoring document;
// the baked table and the preview sample are rebuilt after every change.
class DirectionalBlendSpacePanel
{
public:
    DirectionalBlendSpacePanel();
    ~DirectionalBlendSpacePanel();

    DirectionalBlendSpacePanel(const DirectionalBlendSpacePanel&) = delete;
    DirectionalBlendSpacePanel& operator=(const DirectionalBlendSpacePanel&) = delete;

    // Registers the document as a CommandStack external buffer. Without a
    // command stack the panel still edits and previews, but Undo is disabled.
    [[nodiscard]] bool Initialize(CommandStack& InCommands) noexcept;
    void Shutdown() noexcept;

    void Draw(const char* InWindowTitle, bool* InOpen);

    // Test and host seam: replace the whole document, reset clip paths and
    // rebuild the baked table.
    void SetDocument(const Animation::DirectionalBlendSpaceDocument& InDocument) noexcept;
    [[nodiscard]] Animation::DirectionalBlendSpaceDocument& Document() noexcept
    {
        return MDocument;
    }
    [[nodiscard]] const Animation::DirectionalBlendSpaceDocument& Document() const noexcept
    {
        return MDocument;
    }
    [[nodiscard]] const Animation::DirectionalBlendTable& Table() const noexcept
    {
        return MTable;
    }
    [[nodiscard]] const Animation::DirectionalBlendSample& PreviewSample() const noexcept
    {
        return MSample;
    }
    [[nodiscard]] std::uint32_t DominantClip() const noexcept;

    // Preview input. Ring uses the angle; plane uses the point.
    void SetPreviewAngleDegrees(float InDegrees) noexcept;
    void SetPreviewDirection(Vector2D InDirection) noexcept;
    void SetPreviewPoint(Vector2D InPoint) noexcept;
    [[nodiscard]] float PreviewAngleDegrees() const noexcept;
    [[nodiscard]] Vector2D PreviewPoint() const noexcept;

    // Anchor authoring. AddAnchor places the new sample at the preview
    // direction or point and returns its index, or kNoBlendAnchor when full.
    [[nodiscard]] std::uint16_t AddAnchor() noexcept;
    [[nodiscard]] bool RemoveAnchor(std::uint16_t InIndex) noexcept;
    void SetTopology(Animation::DirectionalBlendTopology InTopology) noexcept;
    [[nodiscard]] std::uint16_t SelectedAnchor() const noexcept
    {
        return MSelectedAnchor;
    }
    void SelectAnchor(std::uint16_t InAnchor) noexcept
    {
        MSelectedAnchor = InAnchor < MDocument.AnchorCount ? InAnchor : kNoBlendAnchor;
    }

    // Clip binding is editor metadata: the runtime document stores an opaque
    // ClipId, while the panel remembers the source path a cook step resolves.
    [[nodiscard]] bool SetClipPath(std::uint16_t InAnchor, std::string_view InPath) noexcept;
    [[nodiscard]] std::string_view ClipPath(std::uint16_t InAnchor) const noexcept;

    [[nodiscard]] bool SaveToFile(const std::filesystem::path& InPath);
    [[nodiscard]] bool LoadFromFile(const std::filesystem::path& InPath);
    [[nodiscard]] const char* Status() const noexcept
    {
        return MStatus.data();
    }

    // Undo and redo replace the external document buffer directly, so the host
    // calls this to rebake and re-evaluate.
    void NotifyHistoryChanged() noexcept;
    // Finishes an in-flight drag transaction before the host runs Undo/Redo.
    void CommitPendingEdit() noexcept;

private:
    void BuildDefaultDocument() noexcept;
    void RebuildTable() noexcept;
    void EvaluatePreview() noexcept;
    void AfterDocumentChange() noexcept;
    void CaptureFrameSnapshot() noexcept;

    void DrawToolbar();
    void DrawCanvas();
    void DrawInspector();
    void DrawAnchorList();
    void DrawAnchorInspector();
    void DrawPreview();
    void DrawFile();

    [[nodiscard]] ImVec2 CanvasCenter(const ImVec2& InOrigin, const ImVec2& InSize) const noexcept;
    [[nodiscard]] float CanvasRadius(const ImVec2& InSize) const noexcept;
    [[nodiscard]] ImVec2 DirectionScreen(const ImVec2& InOrigin, const ImVec2& InSize,
                                         Vector2D InDirection) const noexcept;
    [[nodiscard]] ImVec2 AnchorScreen(std::uint16_t InAnchor, const ImVec2& InOrigin,
                                      const ImVec2& InSize) const noexcept;

    struct PlaneView
    {
        ImVec2 ScreenCenter{};
        Vector2D WorldCenter{};
        float Scale = 1.0f;
    };
    [[nodiscard]] PlaneView MakePlaneView(const ImVec2& InOrigin, const ImVec2& InSize) const noexcept;
    [[nodiscard]] ImVec2 WorldToScreen(const PlaneView& InView, Vector2D InPoint) const noexcept;
    [[nodiscard]] Vector2D ScreenToWorld(const PlaneView& InView, const ImVec2& InPoint) const noexcept;

    void HandleCanvasInput(const ImVec2& InOrigin, const ImVec2& InSize);
    [[nodiscard]] std::uint16_t HitAnchor(const ImVec2& InPoint, const ImVec2& InOrigin,
                                          const ImVec2& InSize) const noexcept;
    [[nodiscard]] bool HitDirectionHandle(const ImVec2& InPoint, const ImVec2& InOrigin,
                                          const ImVec2& InSize) const noexcept;
    void SetPreviewFromScreen(const ImVec2& InPoint, const ImVec2& InOrigin, const ImVec2& InSize) noexcept;
    void MoveAnchorFromScreen(std::uint16_t InAnchor, const ImVec2& InPoint, const ImVec2& InOrigin,
                              const ImVec2& InSize) noexcept;

    void DrawRingCanvas(const ImVec2& InOrigin, const ImVec2& InSize);
    void DrawPlaneCanvas(const ImVec2& InOrigin, const ImVec2& InSize);

    [[nodiscard]] bool OpenEdit(const char* InLabel) noexcept;
    void RecordEdit() noexcept;
    void CommitEdit() noexcept;
    void CancelEdit() noexcept;
    void StartFieldEdit(const char* InLabel) noexcept;
    void FinishFieldEdit() noexcept;
    void CommitSnapshotEdit(const char* InLabel) noexcept;
    void SetStatus(const char* InFormat, ...) noexcept;
    void SyncClipPathBuffer() noexcept;

    Animation::DirectionalBlendSpaceDocument MDocument{};
    Animation::DirectionalBlendTable MTable{};
    Animation::DirectionalBlendSample MSample{};
    std::array<std::string, Animation::kMaxDirectionalAnchors> MClipPaths{};
    std::array<char, 260> MClipPathBuffer{};
    std::uint16_t MClipPathAnchor = kNoBlendAnchor;

    Vector2D MPreviewPoint{};
    float MPreviewAngleRadians = 0.0f;
    std::uint16_t MSelectedAnchor = kNoBlendAnchor;
    std::uint16_t MDragAnchor = kNoBlendAnchor;
    bool MDraggingDirection = false;

    CommandStack* MCommands = nullptr;
    std::uint16_t MCodec = 0xFFFFu;
    std::uint32_t MCodecSize = 0;
    Animation::DirectionalBlendSpaceDocument MEditBefore{};
    Animation::DirectionalBlendSpaceDocument MFrameSnapshot{};
    bool MEditing = false;
    bool MInitialized = false;

    std::array<char, 260> MPath{"assets/animation/blend_space.fblend"};
    std::array<char, 128> MStatus{};
};
}
