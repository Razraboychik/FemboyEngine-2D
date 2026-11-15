// FemboyEngine2D - editor round-trip inspector for Pink UI layout assets.
#include "Editor/Panels/UiLayoutPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/ECS/SystemContext.hpp"

#include <imgui.h>

#include <cstddef>

namespace Fby::Editor
{

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
    ImGui::Separator();
    if (ImGui::BeginChild("##ui_layout_toml", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        ImGui::TextUnformatted(MToml.c_str());
    }
    ImGui::EndChild();
    EndEditorPanel();
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
