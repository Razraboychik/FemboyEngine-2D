// FemboyEngine2D - editor round-trip inspector for Pink UI layout assets.
#pragma once

#include "Engine/UI/CardBoard.hpp"
#include "Engine/UI/PinkSdf.hpp"
#include "Engine/UI/WidgetLayoutAsset.hpp"

#include <string>

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

    Ui::WidgetCanvas MCanvas;
    Ui::WidgetCanvas MReloaded;
    Fby::Ecs::Registry MCardWorld;
    std::uint32_t MCardPrimitives = 0;
    std::uint32_t MCardGpuPrimitives = 0;
    std::string MToml;
    std::string MStatus;
    bool MBuilt = false;
};

}
