// FemboyEngine2D - editor round-trip inspector for Pink UI layout assets.
#pragma once

#include "Engine/UI/CardBoard.hpp"
#include "Engine/UI/PinkSdf.hpp"
#include "Engine/UI/WidgetLayoutAsset.hpp"

#include <array>
#include <string>
#include <vector>

namespace Fby::Ecs
{
struct SystemContext;
}

namespace Fby::Editor
{

class UiLayoutPanel
{
public:
    void Draw(Ecs::SystemContext& InContext, bool* InOpen);

private:
    void BuildDemo();
    void AddWidget(Ui::WidgetKind InKind);
    void DrawProperties();
    void DrawCanvasPreview();
    void DeleteSelected();
    void DuplicateSelected();
    void CopySelected();
    void PasteClipboard();
    void PushUndo();
    void Undo();
    void Redo();
    void SaveLayout();
    void LoadLayout();

    Ui::WidgetCanvas MCanvas;
    Ui::WidgetCanvas MReloaded;
    Fby::Ecs::Registry MCardWorld;
    std::uint32_t MCardPrimitives = 0;
    std::uint32_t MCardGpuPrimitives = 0;
    std::string MToml;
    std::string MStatus;
    std::array<char, 260> MPath{"assets/ui/main.toml"};
    int MSelected = 0;
    bool MDragging = false;
    bool MResizing = false;
    bool MSnapToGrid = true;
    float MGridSize = 8.0f;
    Fby::Ui::WidgetDesc MClipboard{};
    std::string MClipboardText;
    bool MClipboardValid = false;
    std::vector<std::string> MUndo;
    std::vector<std::string> MRedo;
    bool MBuilt = false;
};

}
