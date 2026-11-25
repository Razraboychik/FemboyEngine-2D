// FemboyEngine2D - editor round-trip inspector for Pink UI layout assets.
#include "Editor/Panels/UiLayoutPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/ECS/SystemContext.hpp"

#include <imgui.h>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace Fby::Editor
{
namespace
{
const char* WidgetKindName(Fby::Ui::WidgetKind InKind);
}

void UiLayoutPanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(720.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("UI Layout", InOpen))
    {
        EndEditorPanel();
        return;
    }
    (void)InContext;

    if (!MBuilt)
    {
        BuildDemo();
    }
    ImGui::TextDisabled("WidgetLayoutAsset round-trip: serialize a Pink UI canvas, parse it into a second canvas.");
    ImGui::Text("source widgets: %u   reloaded widgets: %u", static_cast<unsigned>(MCanvas.widgetCount),
                static_cast<unsigned>(MReloaded.widgetCount));
    ImGui::Text("card SDF primitives: %u (packed %u)", static_cast<unsigned>(MCardPrimitives),
                static_cast<unsigned>(MCardGpuPrimitives));
    if (!MStatus.empty())
    {
        ImGui::TextWrapped("%s", MStatus.c_str());
    }
    if (ImGui::Button("Re-run round-trip"))
    {
        BuildDemo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(MUndo.empty());
    if (ImGui::Button("Undo"))
    {
        Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(MRedo.empty());
    if (ImGui::Button("Redo"))
    {
        Redo();
    }
    ImGui::EndDisabled();
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
    {
        Undo();
    }
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
    {
        Redo();
    }
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
    {
        CopySelected();
    }
    if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
    {
        PasteClipboard();
    }
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        DeleteSelected();
    }
    ImGui::SameLine();
    (void)ImGui::Checkbox("snap", &MSnapToGrid);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    (void)ImGui::DragFloat("grid", &MGridSize, 1.0f, 1.0f, 128.0f);
    ImGui::SameLine();
    if (ImGui::Button("Add panel"))
    {
        AddWidget(Fby::Ui::WidgetKind::Panel);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add label"))
    {
        AddWidget(Fby::Ui::WidgetKind::Label);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add button"))
    {
        AddWidget(Fby::Ui::WidgetKind::Button);
    }
    ImGui::SameLine();
    if (ImGui::Button("Add image"))
    {
        AddWidget(Fby::Ui::WidgetKind::Image);
    }
    ImGui::Separator();
    if (ImGui::BeginChild("##ui_widgets", ImVec2(260.0f, 180.0f), ImGuiChildFlags_Borders))
    {
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCanvas.widgetCount; ++ScopedIndex)
        {
            std::uint32_t ScopedDepth = 0;
            std::int32_t ScopedParent = MCanvas.parents[ScopedIndex];
            while (ScopedParent >= 0 && ScopedDepth < 32)
            {
                ++ScopedDepth;
                ScopedParent = MCanvas.parents[ScopedParent];
            }
            char ScopedLabel[128] = {};
            std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%*s%u: %s", static_cast<int>(ScopedDepth * 2), "",
                          static_cast<unsigned>(ScopedIndex), WidgetKindName(MCanvas.kinds[ScopedIndex]));
            if (ImGui::Selectable(ScopedLabel, MSelected == static_cast<int>(ScopedIndex)))
            {
                MSelected = static_cast<int>(ScopedIndex);
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                const std::int32_t ScopedDrag = static_cast<std::int32_t>(ScopedIndex);
                ImGui::SetDragDropPayload("FBY_WIDGET", &ScopedDrag, sizeof(ScopedDrag));
                ImGui::Text("move %s", ScopedLabel);
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* ScopedPayload = ImGui::AcceptDragDropPayload("FBY_WIDGET");
                    ScopedPayload != nullptr && ScopedPayload->DataSize == sizeof(std::int32_t))
                {
                    const std::int32_t ScopedDrag = *static_cast<const std::int32_t*>(ScopedPayload->Data);
                    PushUndo();
                    if (MCanvas.ReparentWidget(ScopedDrag, static_cast<std::int32_t>(ScopedIndex)))
                    {
                        MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
                        MStatus = "widget reparented";
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("drag a widget here to move it to root");
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* ScopedPayload = ImGui::AcceptDragDropPayload("FBY_WIDGET");
                ScopedPayload != nullptr && ScopedPayload->DataSize == sizeof(std::int32_t))
            {
                const std::int32_t ScopedDrag = *static_cast<const std::int32_t*>(ScopedPayload->Data);
                PushUndo();
                if (MCanvas.ReparentWidget(ScopedDrag, Fby::Ui::kNoWidget))
                {
                    MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
                    MStatus = "widget moved to root";
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();
    }
    ImGui::SameLine();
    if (ImGui::BeginChild("##ui_props", ImVec2(0.0f, 180.0f), ImGuiChildFlags_Borders))
    {
        DrawProperties();
        ImGui::EndChild();
    }
    DrawCanvasPreview();
    ImGui::InputText("layout path", MPath.data(), MPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Save layout"))
    {
        SaveLayout();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load layout"))
    {
        LoadLayout();
    }
    ImGui::Separator();
    if (ImGui::BeginChild("##ui_layout_toml", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        ImGui::TextUnformatted(MToml.c_str());
    }
    ImGui::EndChild();
    EndEditorPanel();
}

namespace
{
const char* WidgetKindName(Fby::Ui::WidgetKind InKind)
{
    switch (InKind)
    {
        case Fby::Ui::WidgetKind::Panel:
            return "Panel";
        case Fby::Ui::WidgetKind::Button:
            return "Button";
        case Fby::Ui::WidgetKind::Label:
            return "Label";
        case Fby::Ui::WidgetKind::Image:
            return "Image";
        case Fby::Ui::WidgetKind::ProgressBar:
            return "ProgressBar";
        case Fby::Ui::WidgetKind::Hypertext:
            return "Hypertext";
        case Fby::Ui::WidgetKind::Spacer:
            return "Spacer";
        case Fby::Ui::WidgetKind::Border:
            return "Border";
        case Fby::Ui::WidgetKind::Slider:
            return "Slider";
        case Fby::Ui::WidgetKind::Checkbox:
            return "Checkbox";
        case Fby::Ui::WidgetKind::Scrollbar:
            return "Scrollbar";
        case Fby::Ui::WidgetKind::Switcher:
            return "Switcher";
        case Fby::Ui::WidgetKind::Overlay:
            return "Overlay";
        case Fby::Ui::WidgetKind::Custom:
            return "Custom";
        case Fby::Ui::WidgetKind::Tooltip:
            return "Tooltip";
        case Fby::Ui::WidgetKind::TextInput:
            return "TextInput";
        case Fby::Ui::WidgetKind::NineSlice:
            return "NineSlice";
        case Fby::Ui::WidgetKind::ToggleSwitch:
            return "ToggleSwitch";
        case Fby::Ui::WidgetKind::RadioButton:
            return "RadioButton";
        case Fby::Ui::WidgetKind::TextBlock:
            return "TextBlock";
        default:
            return "?";
    }
}
}

void UiLayoutPanel::CopySelected()
{
    if (MSelected < 0 || static_cast<std::uint32_t>(MSelected) >= MCanvas.widgetCount)
    {
        return;
    }
    const auto ScopedIndex = static_cast<std::uint32_t>(MSelected);
    MClipboard = {};
    MClipboard.kind = MCanvas.kinds[ScopedIndex];
    MClipboard.anchorMin = MCanvas.anchorMins[ScopedIndex];
    MClipboard.anchorMax = MCanvas.anchorMaxs[ScopedIndex];
    MClipboard.offsetMin = MCanvas.offsetMins[ScopedIndex];
    MClipboard.offsetMax = MCanvas.offsetMaxs[ScopedIndex];
    MClipboard.tint = MCanvas.tints[ScopedIndex];
    MClipboard.textTint = MCanvas.textTints[ScopedIndex];
    MClipboard.fillTint = MCanvas.fillTints[ScopedIndex];
    MClipboard.layer = MCanvas.layers[ScopedIndex];
    MClipboard.uvMin = MCanvas.uvMins[ScopedIndex];
    MClipboard.uvMax = MCanvas.uvMaxs[ScopedIndex];
    MClipboard.focusPolicy = MCanvas.focusPolicies[ScopedIndex];
    MClipboard.tabIndex = MCanvas.tabIndices[ScopedIndex];
    MClipboard.focusGroup = MCanvas.focusGroups[ScopedIndex];
    MClipboard.sliceInsets = MCanvas.slices[ScopedIndex];
    MClipboard.opacity = MCanvas.opacities[ScopedIndex];
    MClipboardText = std::string(MCanvas.Text(ScopedIndex));
    MClipboardValid = true;
    MStatus = "widget copied";
}

void UiLayoutPanel::PasteClipboard()
{
    if (!MClipboardValid)
    {
        return;
    }
    PushUndo();
    Fby::Ui::WidgetDesc ScopedDesc = MClipboard;
    ScopedDesc.parent = MSelected >= 0 ? MSelected : Fby::Ui::kNoWidget;
    ScopedDesc.offsetMin += Fby::Vector2D{16.0f, 16.0f};
    ScopedDesc.offsetMax += Fby::Vector2D{16.0f, 16.0f};
    ScopedDesc.text = MClipboardText;
    const std::int32_t ScopedNew = MCanvas.AddWidget(ScopedDesc);
    if (ScopedNew >= 0)
    {
        MSelected = ScopedNew;
        MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
        MStatus = "widget pasted";
    }
}

void UiLayoutPanel::DuplicateSelected()
{
    if (MSelected < 0 || static_cast<std::uint32_t>(MSelected) >= MCanvas.widgetCount)
    {
        return;
    }
    PushUndo();
    const auto ScopedIndex = static_cast<std::uint32_t>(MSelected);
    Fby::Ui::WidgetDesc ScopedDesc;
    ScopedDesc.kind = MCanvas.kinds[ScopedIndex];
    ScopedDesc.parent = MCanvas.parents[ScopedIndex];
    ScopedDesc.anchorMin = MCanvas.anchorMins[ScopedIndex];
    ScopedDesc.anchorMax = MCanvas.anchorMaxs[ScopedIndex];
    ScopedDesc.offsetMin = MCanvas.offsetMins[ScopedIndex] + Fby::Vector2D{16.0f, 16.0f};
    ScopedDesc.offsetMax = MCanvas.offsetMaxs[ScopedIndex] + Fby::Vector2D{16.0f, 16.0f};
    ScopedDesc.tint = MCanvas.tints[ScopedIndex];
    ScopedDesc.textTint = MCanvas.textTints[ScopedIndex];
    ScopedDesc.fillTint = MCanvas.fillTints[ScopedIndex];
    ScopedDesc.layer = MCanvas.layers[ScopedIndex];
    ScopedDesc.uvMin = MCanvas.uvMins[ScopedIndex];
    ScopedDesc.uvMax = MCanvas.uvMaxs[ScopedIndex];
    ScopedDesc.text = MCanvas.Text(ScopedIndex);
    ScopedDesc.focusPolicy = MCanvas.focusPolicies[ScopedIndex];
    ScopedDesc.tabIndex = MCanvas.tabIndices[ScopedIndex];
    ScopedDesc.focusGroup = MCanvas.focusGroups[ScopedIndex];
    ScopedDesc.sliceInsets = MCanvas.slices[ScopedIndex];
    ScopedDesc.opacity = MCanvas.opacities[ScopedIndex];
    const std::int32_t ScopedNew = MCanvas.AddWidget(ScopedDesc);
    if (ScopedNew >= 0)
    {
        MSelected = ScopedNew;
        MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
        MStatus = "widget duplicated";
    }
}

void UiLayoutPanel::DeleteSelected()
{
    if (MSelected < 0 || static_cast<std::uint32_t>(MSelected) >= MCanvas.widgetCount)
    {
        return;
    }
    PushUndo();
    const std::uint32_t ScopedRemoved = static_cast<std::uint32_t>(MSelected);
    Fby::Ui::WidgetCanvas ScopedCanvas;
    ScopedCanvas.size = MCanvas.size;
    std::array<std::int32_t, Fby::Ui::kMaxWidgets> ScopedRemap{};
    std::array<std::int32_t, Fby::Ui::kMaxWidgets> ScopedOldParents{};
    ScopedRemap.fill(-1);
    ScopedOldParents.fill(-1);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCanvas.widgetCount; ++ScopedIndex)
    {
        if (ScopedIndex == ScopedRemoved)
        {
            continue;
        }
        Fby::Ui::WidgetDesc ScopedDesc;
        ScopedDesc.kind = MCanvas.kinds[ScopedIndex];
        ScopedDesc.parent = Fby::Ui::kNoWidget;
        ScopedDesc.anchorMin = MCanvas.anchorMins[ScopedIndex];
        ScopedDesc.anchorMax = MCanvas.anchorMaxs[ScopedIndex];
        ScopedDesc.offsetMin = MCanvas.offsetMins[ScopedIndex];
        ScopedDesc.offsetMax = MCanvas.offsetMaxs[ScopedIndex];
        ScopedDesc.tint = MCanvas.tints[ScopedIndex];
        ScopedDesc.textTint = MCanvas.textTints[ScopedIndex];
        ScopedDesc.fillTint = MCanvas.fillTints[ScopedIndex];
        ScopedDesc.layer = MCanvas.layers[ScopedIndex];
        ScopedDesc.uvMin = MCanvas.uvMins[ScopedIndex];
        ScopedDesc.uvMax = MCanvas.uvMaxs[ScopedIndex];
        ScopedDesc.text = MCanvas.Text(ScopedIndex);
        ScopedDesc.focusPolicy = MCanvas.focusPolicies[ScopedIndex];
        ScopedDesc.tabIndex = MCanvas.tabIndices[ScopedIndex];
        ScopedDesc.focusGroup = MCanvas.focusGroups[ScopedIndex];
        ScopedDesc.sliceInsets = MCanvas.slices[ScopedIndex];
        ScopedDesc.opacity = MCanvas.opacities[ScopedIndex];
        const std::int32_t ScopedNew = ScopedCanvas.AddWidget(ScopedDesc);
        ScopedRemap[ScopedIndex] = ScopedNew;
        if (ScopedNew >= 0)
        {
            ScopedOldParents[static_cast<std::uint32_t>(ScopedNew)] = MCanvas.parents[ScopedIndex];
        }
    }
    for (std::uint32_t ScopedNew = 0; ScopedNew < ScopedCanvas.widgetCount; ++ScopedNew)
    {
        const std::int32_t ScopedOldParent = ScopedOldParents[ScopedNew];
        if (ScopedOldParent >= 0 && ScopedOldParent != static_cast<std::int32_t>(ScopedRemoved) &&
            ScopedRemap[static_cast<std::uint32_t>(ScopedOldParent)] >= 0)
        {
            (void)ScopedCanvas.ReparentWidget(static_cast<std::int32_t>(ScopedNew),
                                              ScopedRemap[static_cast<std::uint32_t>(ScopedOldParent)]);
        }
    }
    MCanvas = ScopedCanvas;
    MSelected = 0;
    MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
    MStatus = "widget deleted";
}

void UiLayoutPanel::DrawCanvasPreview()
{
    if (!ImGui::BeginChild("##ui_canvas", ImVec2(0.0f, 240.0f), ImGuiChildFlags_Borders))
    {
        ImGui::EndChild();
        return;
    }
    const ImVec2 ScopedOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 ScopedAvailable = ImGui::GetContentRegionAvail();
    if (MCanvas.size.X <= 0.0f || ScopedAvailable.x <= 0.0f)
    {
        ImGui::EndChild();
        return;
    }
    const float ScopedScale = ScopedAvailable.x / MCanvas.size.X;
    const ImVec2 ScopedSize{MCanvas.size.X * ScopedScale, MCanvas.size.Y * ScopedScale};
    (void)ImGui::InvisibleButton("##ui_canvas_hit", ScopedSize);
    if (MDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && MSelected >= 0 &&
        static_cast<std::uint32_t>(MSelected) < MCanvas.widgetCount)
    {
        const ImVec2 ScopedDelta = ImGui::GetIO().MouseDelta;
        MCanvas.offsetMins[MSelected].X += ScopedDelta.x / ScopedScale;
        MCanvas.offsetMins[MSelected].Y += ScopedDelta.y / ScopedScale;
        MCanvas.offsetMaxs[MSelected].X += ScopedDelta.x / ScopedScale;
        MCanvas.offsetMaxs[MSelected].Y += ScopedDelta.y / ScopedScale;
        MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
    }
    if (MResizing && ImGui::IsMouseDown(ImGuiMouseButton_Left) && MSelected >= 0 &&
        static_cast<std::uint32_t>(MSelected) < MCanvas.widgetCount)
    {
        const ImVec2 ScopedDelta = ImGui::GetIO().MouseDelta;
        MCanvas.offsetMaxs[MSelected].X += ScopedDelta.x / ScopedScale;
        MCanvas.offsetMaxs[MSelected].Y += ScopedDelta.y / ScopedScale;
        MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        MDragging = false;
        MResizing = false;
    }
    const auto SnapValue = [this](float InValue)
    {
        return MSnapToGrid && MGridSize > 0.0f ? std::round(InValue / MGridSize) * MGridSize : InValue;
    };
    if (MDragging || MResizing)
    {
        MCanvas.offsetMins[MSelected].X = SnapValue(MCanvas.offsetMins[MSelected].X);
        MCanvas.offsetMins[MSelected].Y = SnapValue(MCanvas.offsetMins[MSelected].Y);
        MCanvas.offsetMaxs[MSelected].X = SnapValue(MCanvas.offsetMaxs[MSelected].X);
        MCanvas.offsetMaxs[MSelected].Y = SnapValue(MCanvas.offsetMaxs[MSelected].Y);
    }
    ImDrawList* ScopedDraw = ImGui::GetWindowDrawList();
    ScopedDraw->AddRectFilled(ScopedOrigin, ImVec2{ScopedOrigin.x + ScopedSize.x, ScopedOrigin.y + ScopedSize.y},
                              IM_COL32(24, 24, 28, 255));
    if (MSnapToGrid && MGridSize > 1.0f)
    {
        for (float ScopedX = 0.0f; ScopedX <= ScopedSize.x; ScopedX += MGridSize * ScopedScale)
        {
            ScopedDraw->AddLine(ImVec2{ScopedOrigin.x + ScopedX, ScopedOrigin.y},
                                ImVec2{ScopedOrigin.x + ScopedX, ScopedOrigin.y + ScopedSize.y},
                                IM_COL32(50, 50, 58, 255));
        }
        for (float ScopedY = 0.0f; ScopedY <= ScopedSize.y; ScopedY += MGridSize * ScopedScale)
        {
            ScopedDraw->AddLine(ImVec2{ScopedOrigin.x, ScopedOrigin.y + ScopedY},
                                ImVec2{ScopedOrigin.x + ScopedSize.x, ScopedOrigin.y + ScopedY},
                                IM_COL32(50, 50, 58, 255));
        }
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCanvas.widgetCount; ++ScopedIndex)
    {
        const auto& ScopedMin = MCanvas.anchorMins[ScopedIndex];
        const auto& ScopedMax = MCanvas.anchorMaxs[ScopedIndex];
        const float ScopedX0 = ScopedMin.X * ScopedSize.x + MCanvas.offsetMins[ScopedIndex].X * ScopedScale;
        const float ScopedY0 = ScopedMin.Y * ScopedSize.y + MCanvas.offsetMins[ScopedIndex].Y * ScopedScale;
        const float ScopedX1 = ScopedMax.X * ScopedSize.x + MCanvas.offsetMaxs[ScopedIndex].X * ScopedScale;
        const float ScopedY1 = ScopedMax.Y * ScopedSize.y + MCanvas.offsetMaxs[ScopedIndex].Y * ScopedScale;
        const ImVec2 ScopedWidgetMin{ScopedOrigin.x + ScopedX0, ScopedOrigin.y + ScopedY0};
        const ImVec2 ScopedWidgetMax{ScopedOrigin.x + ScopedX1, ScopedOrigin.y + ScopedY1};
        const Fby::Color32 ScopedTint = MCanvas.tints[ScopedIndex];
        const ImU32 ScopedColor = IM_COL32(ScopedTint.R(), ScopedTint.G(), ScopedTint.B(), 180);
        ScopedDraw->AddRectFilled(ScopedWidgetMin, ScopedWidgetMax, ScopedColor);
        ScopedDraw->AddRect(ScopedWidgetMin, ScopedWidgetMax,
                            MSelected == static_cast<int>(ScopedIndex) ? IM_COL32(255, 210, 80, 255)
                                                                      : IM_COL32(160, 160, 170, 255));
        char ScopedLabel[32] = {};
        std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%u", static_cast<unsigned>(ScopedIndex));
        ScopedDraw->AddText(ImVec2{ScopedWidgetMin.x + 4.0f, ScopedWidgetMin.y + 2.0f}, IM_COL32_BLACK, ScopedLabel);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(ScopedWidgetMin, ScopedWidgetMax))
        {
            if (!MDragging && !MResizing)
            {
                PushUndo();
            }
            MSelected = static_cast<int>(ScopedIndex);
            MDragging = true;
            MResizing = false;
        }
        if (MSelected == static_cast<int>(ScopedIndex))
        {
            const ImVec2 ScopedHandleMin{ScopedWidgetMax.x - 5.0f, ScopedWidgetMax.y - 5.0f};
            const ImVec2 ScopedHandleMax{ScopedWidgetMax.x + 5.0f, ScopedWidgetMax.y + 5.0f};
            ScopedDraw->AddRectFilled(ScopedHandleMin, ScopedHandleMax, IM_COL32(255, 210, 80, 255));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                ImGui::IsMouseHoveringRect(ScopedHandleMin, ScopedHandleMax))
            {
                PushUndo();
                MDragging = false;
                MResizing = true;
            }
        }
    }
    ImGui::EndChild();
}

void UiLayoutPanel::PushUndo()
{
    MUndo.push_back(Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas));
    if (MUndo.size() > 32)
    {
        MUndo.erase(MUndo.begin());
    }
    MRedo.clear();
}

void UiLayoutPanel::Undo()
{
    if (MUndo.empty())
    {
        return;
    }
    MRedo.push_back(Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas));
    std::string ScopedText = std::move(MUndo.back());
    MUndo.pop_back();
    Fby::Ui::WidgetCanvas ScopedLoaded;
    std::string ScopedError;
    if (!Fby::Ui::WidgetLayoutAsset::Load(ScopedLoaded, ScopedText, &ScopedError))
    {
        MStatus = "undo: " + ScopedError;
        return;
    }
    MCanvas = ScopedLoaded;
    MSelected = MCanvas.widgetCount > 0 ? 0 : -1;
    MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
    MStatus = "undo";
}

void UiLayoutPanel::Redo()
{
    if (MRedo.empty())
    {
        return;
    }
    MUndo.push_back(Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas));
    std::string ScopedText = std::move(MRedo.back());
    MRedo.pop_back();
    Fby::Ui::WidgetCanvas ScopedLoaded;
    std::string ScopedError;
    if (!Fby::Ui::WidgetLayoutAsset::Load(ScopedLoaded, ScopedText, &ScopedError))
    {
        MStatus = "redo: " + ScopedError;
        return;
    }
    MCanvas = ScopedLoaded;
    MSelected = MCanvas.widgetCount > 0 ? 0 : -1;
    MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
    MStatus = "redo";
}

void UiLayoutPanel::AddWidget(Fby::Ui::WidgetKind InKind)
{
    PushUndo();
    Fby::Ui::WidgetDesc ScopedDesc;
    ScopedDesc.kind = InKind;
    ScopedDesc.parent = Fby::Ui::kNoWidget;
    ScopedDesc.anchorMin = Fby::Vector2D{0.1f, 0.1f};
    ScopedDesc.anchorMax = Fby::Vector2D{0.4f, 0.4f};
    ScopedDesc.tint = Fby::Color32::White();
    ScopedDesc.textTint = Fby::Color32::White();
    const std::int32_t ScopedIndex = MCanvas.AddWidget(ScopedDesc);
    if (ScopedIndex >= 0)
    {
        MSelected = ScopedIndex;
        MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
        MStatus = "widget added";
    }
}

void UiLayoutPanel::DrawProperties()
{
    if (MSelected < 0 || static_cast<std::uint32_t>(MSelected) >= MCanvas.widgetCount)
    {
        ImGui::TextDisabled("select a widget");
        return;
    }
    const std::int32_t ScopedIndex = MSelected;
    ImGui::Text("widget %d: %s", ScopedIndex, WidgetKindName(MCanvas.kinds[ScopedIndex]));
    ImGui::SameLine();
    if (ImGui::SmallButton("delete"))
    {
        DeleteSelected();
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("duplicate"))
    {
        DuplicateSelected();
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("copy"))
    {
        CopySelected();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!MClipboardValid);
    if (ImGui::SmallButton("paste"))
    {
        PasteClipboard();
        return;
    }
    ImGui::EndDisabled();
    if (ImGui::SmallButton("stretch"))
    {
        PushUndo();
        MCanvas.SetAnchors(ScopedIndex, Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{1.0f, 1.0f});
        MCanvas.offsetMins[ScopedIndex] = {};
        MCanvas.offsetMaxs[ScopedIndex] = {};
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("top-left"))
    {
        PushUndo();
        MCanvas.SetAnchors(ScopedIndex, Fby::Vector2D{0.0f, 0.0f}, Fby::Vector2D{0.0f, 0.0f});
        MCanvas.offsetMins[ScopedIndex] = {};
        MCanvas.offsetMaxs[ScopedIndex] = Fby::Vector2D{96.0f, 48.0f};
    }
    (void)ImGui::DragFloat2("anchor min", &MCanvas.anchorMins[ScopedIndex].X, 0.01f, 0.0f, 1.0f);
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        PushUndo();
    }
    (void)ImGui::DragFloat2("anchor max", &MCanvas.anchorMaxs[ScopedIndex].X, 0.01f, 0.0f, 1.0f);
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        PushUndo();
    }
    (void)ImGui::DragFloat2("offset min", &MCanvas.offsetMins[ScopedIndex].X, 0.5f);
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        PushUndo();
    }
    (void)ImGui::DragFloat2("offset max", &MCanvas.offsetMaxs[ScopedIndex].X, 0.5f);
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        PushUndo();
    }
    const char* ScopedParentName = MCanvas.parents[ScopedIndex] >= 0 ? "parent" : "root";
    if (ImGui::BeginCombo("parent", ScopedParentName))
    {
        if (ImGui::Selectable("root", MCanvas.parents[ScopedIndex] == Fby::Ui::kNoWidget))
        {
            PushUndo();
            (void)MCanvas.ReparentWidget(ScopedIndex, Fby::Ui::kNoWidget);
        }
        for (std::uint32_t ScopedParent = 0; ScopedParent < MCanvas.widgetCount; ++ScopedParent)
        {
            if (ScopedParent == static_cast<std::uint32_t>(ScopedIndex))
            {
                continue;
            }
            char ScopedLabel[64] = {};
            std::snprintf(ScopedLabel, sizeof(ScopedLabel), "%u: %s", static_cast<unsigned>(ScopedParent),
                          WidgetKindName(MCanvas.kinds[ScopedParent]));
            if (ImGui::Selectable(ScopedLabel, MCanvas.parents[ScopedIndex] == static_cast<std::int32_t>(ScopedParent)))
            {
                PushUndo();
                (void)MCanvas.ReparentWidget(ScopedIndex, static_cast<std::int32_t>(ScopedParent));
            }
        }
        ImGui::EndCombo();
    }
    MCanvas.SetAnchors(ScopedIndex, MCanvas.anchorMins[ScopedIndex], MCanvas.anchorMaxs[ScopedIndex]);
    const char* ScopedKinds[] = {"Panel", "Button", "Label", "Image", "ProgressBar", "Hypertext", "Spacer",
                                 "Border", "Slider", "Checkbox", "Scrollbar", "Switcher", "Overlay", "Custom",
                                 "Tooltip", "TextInput", "NineSlice", "ToggleSwitch", "RadioButton", "TextBlock"};
    int ScopedKind = static_cast<int>(MCanvas.kinds[ScopedIndex]);
    if (ImGui::Combo("kind", &ScopedKind, ScopedKinds, 20))
    {
        PushUndo();
        MCanvas.kinds[ScopedIndex] = static_cast<Fby::Ui::WidgetKind>(ScopedKind);
    }
    if (ImGui::SliderFloat("opacity", &MCanvas.opacities[ScopedIndex], 0.0f, 1.0f))
    {
        PushUndo();
    }
    float ScopedTint[4] = {static_cast<float>(MCanvas.tints[ScopedIndex].R()) / 255.0f,
                           static_cast<float>(MCanvas.tints[ScopedIndex].G()) / 255.0f,
                           static_cast<float>(MCanvas.tints[ScopedIndex].B()) / 255.0f,
                           static_cast<float>(MCanvas.tints[ScopedIndex].A()) / 255.0f};
    if (ImGui::ColorEdit4("tint", ScopedTint))
    {
        PushUndo();
        MCanvas.SetTint(ScopedIndex, Fby::Color32::FromFloats(ScopedTint[0], ScopedTint[1], ScopedTint[2], ScopedTint[3]));
    }
    float ScopedFill[4] = {static_cast<float>(MCanvas.fillTints[ScopedIndex].R()) / 255.0f,
                           static_cast<float>(MCanvas.fillTints[ScopedIndex].G()) / 255.0f,
                           static_cast<float>(MCanvas.fillTints[ScopedIndex].B()) / 255.0f,
                           static_cast<float>(MCanvas.fillTints[ScopedIndex].A()) / 255.0f};
    if (ImGui::ColorEdit4("fill tint", ScopedFill))
    {
        PushUndo();
        MCanvas.SetFillTint(ScopedIndex,
                            Fby::Color32::FromFloats(ScopedFill[0], ScopedFill[1], ScopedFill[2], ScopedFill[3]));
    }
    float ScopedTextTint[4] = {static_cast<float>(MCanvas.textTints[ScopedIndex].R()) / 255.0f,
                               static_cast<float>(MCanvas.textTints[ScopedIndex].G()) / 255.0f,
                               static_cast<float>(MCanvas.textTints[ScopedIndex].B()) / 255.0f,
                               static_cast<float>(MCanvas.textTints[ScopedIndex].A()) / 255.0f};
    if (ImGui::ColorEdit4("text tint", ScopedTextTint))
    {
        PushUndo();
        MCanvas.SetTextTint(ScopedIndex,
                            Fby::Color32::FromFloats(ScopedTextTint[0], ScopedTextTint[1], ScopedTextTint[2], ScopedTextTint[3]));
    }
    const char* ScopedFocusPolicies[] = {"None", "Pointer", "Tab", "TabAndPointer"};
    int ScopedFocus = static_cast<int>(MCanvas.focusPolicies[ScopedIndex]);
    if (ImGui::Combo("focus", &ScopedFocus, ScopedFocusPolicies, 4))
    {
        PushUndo();
        MCanvas.SetFocusPolicy(ScopedIndex, static_cast<Fby::Ui::FocusPolicy>(ScopedFocus));
    }
    bool ScopedVisible = Fby::Ui::HasFlag(MCanvas.flags[ScopedIndex], Fby::Ui::WidgetFlags::Visible);
    if (ImGui::Checkbox("visible", &ScopedVisible))
    {
        PushUndo();
        MCanvas.SetFlag(ScopedIndex, Fby::Ui::WidgetFlags::Visible, ScopedVisible);
    }
    ImGui::SameLine();
    bool ScopedEnabled = Fby::Ui::HasFlag(MCanvas.flags[ScopedIndex], Fby::Ui::WidgetFlags::Enabled);
    if (ImGui::Checkbox("enabled", &ScopedEnabled))
    {
        PushUndo();
        MCanvas.SetFlag(ScopedIndex, Fby::Ui::WidgetFlags::Enabled, ScopedEnabled);
    }
    bool ScopedClip = Fby::Ui::HasFlag(MCanvas.flags[ScopedIndex], Fby::Ui::WidgetFlags::ClipChildren);
    if (ImGui::Checkbox("clip children", &ScopedClip))
    {
        PushUndo();
        MCanvas.SetFlag(ScopedIndex, Fby::Ui::WidgetFlags::ClipChildren, ScopedClip);
    }
    bool ScopedReadOnly = Fby::Ui::HasFlag(MCanvas.flags[ScopedIndex], Fby::Ui::WidgetFlags::ReadOnly);
    if (ImGui::Checkbox("read only", &ScopedReadOnly))
    {
        PushUndo();
        MCanvas.SetFlag(ScopedIndex, Fby::Ui::WidgetFlags::ReadOnly, ScopedReadOnly);
    }
    int ScopedLayer = static_cast<int>(MCanvas.layers[ScopedIndex]);
    if (ImGui::DragInt("layer", &ScopedLayer, 1.0f, 0, 65535))
    {
        PushUndo();
        MCanvas.layers[ScopedIndex] = static_cast<std::uint32_t>(std::clamp(ScopedLayer, 0, 65535));
    }
    int ScopedZ = MCanvas.zOrders[ScopedIndex];
    if (ImGui::DragInt("z-order", &ScopedZ, 1.0f, -32, 31))
    {
        PushUndo();
        (void)MCanvas.SetZOrder(ScopedIndex, ScopedZ);
    }
    if (MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::Label ||
        MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::Button ||
        MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::TextInput)
    {
        const char* ScopedAligns[] = {"left", "centre", "right"};
        int ScopedAlign = static_cast<int>(MCanvas.textFlags[ScopedIndex] & Fby::Ui::kTextAlignMask);
        if (ImGui::Combo("align", &ScopedAlign, ScopedAligns, 3))
        {
            PushUndo();
            const int ScopedVertical =
                static_cast<int>((MCanvas.textFlags[ScopedIndex] & Fby::Ui::kTextVerticalMask) >> Fby::Ui::kTextVerticalShift);
            MCanvas.textFlags[ScopedIndex] = Fby::Ui::MakeTextFlags(static_cast<Fby::Ui::TextAlign>(ScopedAlign),
                                                                    static_cast<Fby::Ui::TextVerticalAlign>(ScopedVertical),
                                                                    (MCanvas.textFlags[ScopedIndex] & Fby::Ui::kTextWrap) != 0);
        }
        const char* ScopedVerticals[] = {"top", "middle", "bottom"};
        int ScopedVertical =
            static_cast<int>((MCanvas.textFlags[ScopedIndex] & Fby::Ui::kTextVerticalMask) >> Fby::Ui::kTextVerticalShift);
        if (ImGui::Combo("vertical", &ScopedVertical, ScopedVerticals, 3))
        {
            PushUndo();
            const int ScopedAlignValue = static_cast<int>(MCanvas.textFlags[ScopedIndex] & Fby::Ui::kTextAlignMask);
            MCanvas.textFlags[ScopedIndex] = Fby::Ui::MakeTextFlags(static_cast<Fby::Ui::TextAlign>(ScopedAlignValue),
                                                                    static_cast<Fby::Ui::TextVerticalAlign>(ScopedVertical),
                                                                    (MCanvas.textFlags[ScopedIndex] & Fby::Ui::kTextWrap) != 0);
        }
        bool ScopedWrap = (MCanvas.textFlags[ScopedIndex] & Fby::Ui::kTextWrap) != 0;
        if (ImGui::Checkbox("wrap", &ScopedWrap))
        {
            PushUndo();
            const int ScopedAlignValue = static_cast<int>(MCanvas.textFlags[ScopedIndex] & Fby::Ui::kTextAlignMask);
            const int ScopedVerticalValue =
                static_cast<int>((MCanvas.textFlags[ScopedIndex] & Fby::Ui::kTextVerticalMask) >> Fby::Ui::kTextVerticalShift);
            MCanvas.textFlags[ScopedIndex] = Fby::Ui::MakeTextFlags(static_cast<Fby::Ui::TextAlign>(ScopedAlignValue),
                                                                    static_cast<Fby::Ui::TextVerticalAlign>(ScopedVerticalValue),
                                                                    ScopedWrap);
        }
        int ScopedFont = MCanvas.fontSizes[ScopedIndex];
        if (ImGui::DragInt("font size", &ScopedFont, 1.0f, 8, 512))
        {
            PushUndo();
            MCanvas.fontSizes[ScopedIndex] = static_cast<std::uint16_t>(std::clamp(ScopedFont, 8, 512));
        }
    }
    if (MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::ProgressBar)
    {
        float ScopedProgress = MCanvas.progresses[ScopedIndex];
        if (ImGui::SliderFloat("progress", &ScopedProgress, 0.0f, 1.0f))
        {
            PushUndo();
            MCanvas.SetProgress(ScopedIndex, ScopedProgress);
        }
    }
    if (MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::Scrollbar ||
        MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::Slider)
    {
        (void)ImGui::SliderFloat("ratio", &MCanvas.ratios[ScopedIndex], 0.0f, 1.0f);
    }
    if (MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::NineSlice)
    {
        if (ImGui::DragFloat4("slice insets", &MCanvas.slices[ScopedIndex].X, 1.0f, 0.0f, 512.0f))
        {
            PushUndo();
        }
    }
    if (MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::Image)
    {
        if (ImGui::DragFloat2("uv min", &MCanvas.uvMins[ScopedIndex].X, 0.001f, 0.0f, 1.0f))
        {
            PushUndo();
        }
        if (ImGui::DragFloat2("uv max", &MCanvas.uvMaxs[ScopedIndex].X, 0.001f, 0.0f, 1.0f))
        {
            PushUndo();
        }
    }
    if (MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::Label ||
        MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::Button ||
        MCanvas.kinds[ScopedIndex] == Fby::Ui::WidgetKind::TextInput)
    {
        std::array<char, 128> ScopedText{};
        const std::string_view ScopedCurrent = MCanvas.Text(ScopedIndex);
        const std::size_t ScopedLength = ScopedCurrent.size() < ScopedText.size() - 1 ? ScopedCurrent.size()
                                                                                     : ScopedText.size() - 1;
        std::memcpy(ScopedText.data(), ScopedCurrent.data(), ScopedLength);
        if (ImGui::InputText("text", ScopedText.data(), ScopedText.size()))
        {
            PushUndo();
            (void)MCanvas.SetText(ScopedIndex, ScopedText.data());
        }
    }
    MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
}

void UiLayoutPanel::SaveLayout()
{
    MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
    const std::filesystem::path ScopedPath(MPath.data());
    std::error_code ScopedCode;
    if (!ScopedPath.parent_path().empty())
    {
        std::filesystem::create_directories(ScopedPath.parent_path(), ScopedCode);
    }
    std::ofstream ScopedFile(ScopedPath, std::ios::binary | std::ios::trunc | std::ios::out);
    if (!ScopedFile)
    {
        MStatus = "layout save failed";
        return;
    }
    ScopedFile << MToml;
    MStatus = "layout saved";
}

void UiLayoutPanel::LoadLayout()
{
    std::ifstream ScopedFile(MPath.data(), std::ios::binary);
    if (!ScopedFile)
    {
        MStatus = "layout load failed";
        return;
    }
    std::string ScopedText((std::istreambuf_iterator<char>(ScopedFile)), std::istreambuf_iterator<char>());
    std::string ScopedError;
    Fby::Ui::WidgetCanvas ScopedLoaded;
    if (!Fby::Ui::WidgetLayoutAsset::Load(ScopedLoaded, ScopedText, &ScopedError))
    {
        MStatus = "layout: " + ScopedError;
        return;
    }
    MCanvas = ScopedLoaded;
    MSelected = 0;
    MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
    MStatus = "layout loaded";
}

void UiLayoutPanel::BuildDemo()
{
    MBuilt = true;
    MStatus.clear();
    MCanvas.Clear();
    MCanvas.size = Fby::Vector2D{1280.0f, 720.0f};

    Fby::Ui::WidgetDesc ScopedPanel;
    ScopedPanel.kind = Fby::Ui::WidgetKind::Panel;
    ScopedPanel.anchorMin = Fby::Vector2D{0.1f, 0.1f};
    ScopedPanel.anchorMax = Fby::Vector2D{0.9f, 0.9f};
    ScopedPanel.offsetMin = Fby::Vector2D{0.0f, 0.0f};
    ScopedPanel.offsetMax = Fby::Vector2D{0.0f, 0.0f};
    ScopedPanel.tint = Fby::Color32::FromBytes(32, 34, 40);
    const std::int32_t ScopedPanelIndex = MCanvas.AddWidget(ScopedPanel);

    Fby::Ui::WidgetDesc ScopedLabel;
    ScopedLabel.kind = Fby::Ui::WidgetKind::Label;
    ScopedLabel.parent = ScopedPanelIndex;
    ScopedLabel.anchorMin = Fby::Vector2D{0.08f, 0.08f};
    ScopedLabel.anchorMax = Fby::Vector2D{0.92f, 0.28f};
    ScopedLabel.offsetMin = Fby::Vector2D{0.0f, 0.0f};
    ScopedLabel.offsetMax = Fby::Vector2D{0.0f, 0.0f};
    ScopedLabel.text = "Pink UI layout round-trip";
    ScopedLabel.textTint = Fby::Color32::White();
    const std::int32_t ScopedLabelIndex = MCanvas.AddWidget(ScopedLabel);

    Fby::Ui::WidgetDesc ScopedButton;
    ScopedButton.kind = Fby::Ui::WidgetKind::Button;
    ScopedButton.parent = ScopedPanelIndex;
    ScopedButton.anchorMin = Fby::Vector2D{0.08f, 0.36f};
    ScopedButton.anchorMax = Fby::Vector2D{0.46f, 0.52f};
    ScopedButton.offsetMin = Fby::Vector2D{0.0f, 0.0f};
    ScopedButton.offsetMax = Fby::Vector2D{0.0f, 0.0f};
    ScopedButton.text = "Action";
    ScopedButton.tint = Fby::Color32::FromBytes(214, 96, 160);
    const std::int32_t ScopedButtonIndex = MCanvas.AddWidget(ScopedButton);

    if (ScopedPanelIndex < 0 || ScopedLabelIndex < 0 || ScopedButtonIndex < 0)
    {
        MStatus = "Could not build the demo canvas.";
        MToml.clear();
        return;
    }

    MToml = Fby::Ui::WidgetLayoutAsset::Serialize(MCanvas);
    MReloaded.Clear();
    std::string ScopedError;
    if (!Fby::Ui::WidgetLayoutAsset::Load(MReloaded, MToml, &ScopedError))
    {
        MStatus = "Layout load failed: " + ScopedError;
        return;
    }
    MStatus = "round-trip ok";

    // Exercise the PinkSdf card-board packer on the same demo scene.
    MCardWorld.clear();
    const Fby::Entity ScopedBoardEntity = MCardWorld.create();
    Fby::Ui::CardBoardComponent ScopedBoard;
    ScopedBoard.BoardId = 0;
    ScopedBoard.Columns = 4;
    ScopedBoard.Rows = 1;
    ScopedBoard.Origin = Fby::Vector2D{0.0f, 0.0f};
    MCardWorld.emplace<Fby::Ui::CardBoardComponent>(ScopedBoardEntity, ScopedBoard);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < 4; ++ScopedIndex)
    {
        const Fby::Entity ScopedSlotEntity = MCardWorld.create();
        Fby::Ui::SlotComponent ScopedSlot;
        ScopedSlot.BoardId = 0;
        ScopedSlot.SlotIndex = ScopedIndex;
        ScopedSlot.Flags = Fby::Ui::SlotFaceUp;
        MCardWorld.emplace<Fby::Ui::SlotComponent>(ScopedSlotEntity, ScopedSlot);
    }
    std::array<Fby::Ui::PinkSdfPrimitive, 32> ScopedPrimitives{};
    MCardPrimitives = Fby::Ui::BuildCardBoardPrimitives(MCardWorld, 0, ScopedPrimitives);
    std::array<Fby::Ui::PinkSdfGpuPrimitive, 32> ScopedGpuPrimitives{};
    MCardGpuPrimitives = Fby::Ui::PackPinkSdfPrimitives(
        std::span<const Fby::Ui::PinkSdfPrimitive>(ScopedPrimitives.data(), MCardPrimitives), ScopedGpuPrimitives);
}

}
