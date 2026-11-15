// FemboyEngine2D - editor viewer for CSV/TSV data tables.
#pragma once

#include "Engine/Core/DataTable.hpp"

#include <array>
#include <string>

namespace Fby::Ecs
{
struct SystemContext;
}

namespace Fby::Editor
{

class DataTablePanel
{
public:
    void Draw(Ecs::SystemContext& InContext, bool* InOpen);

private:
    void Load(Ecs::SystemContext& InContext);

    Fby::DataTable MTable;
    std::array<char, 512> MPath{};
    std::string MStatus;
};

}
