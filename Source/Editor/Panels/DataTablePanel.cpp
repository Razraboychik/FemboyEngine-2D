// FemboyEngine2D - editor viewer for CSV/TSV data tables.
#include "Editor/Panels/DataTablePanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Assets/VFS.hpp"
#include "Engine/ECS/SystemContext.hpp"

#include <imgui.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Fby::Editor
{
namespace
{
constexpr std::size_t kMaxDisplayedRows = 500;
}

void DataTablePanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(820.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Data Table", InOpen))
    {
        EndEditorPanel();
        return;
    }

    ImGui::TextDisabled("Reads a CSV or TSV file through the VFS. The first row is the header.");
    ImGui::SetNextItemWidth(520.0f);
    (void)ImGui::InputText("Path", MPath.data(), MPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
        Load(InContext);
    }
    if (!MStatus.empty())
    {
        ImGui::TextWrapped("%s", MStatus.c_str());
    }
    if (MTable.Empty())
    {
        ImGui::Separator();
        ImGui::TextDisabled("No table is loaded.");
        EndEditorPanel();
        return;
    }

    ImGui::Separator();
    ImGui::TextDisabled("%zu row(s), %zu column(s)", MTable.RowCount(), MTable.ColumnCount());
    const std::size_t ScopedRows = MTable.RowCount() < kMaxDisplayedRows ? MTable.RowCount() : kMaxDisplayedRows;
    if (ScopedRows < MTable.RowCount())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(showing first %zu)", kMaxDisplayedRows);
    }
    constexpr ImGuiTableFlags ScopedFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("DataTableGrid", static_cast<int>(MTable.ColumnCount()), ScopedFlags))
    {
        for (const std::string& ScopedColumn : MTable.Columns)
        {
            ImGui::TableSetupColumn(ScopedColumn.c_str());
        }
        ImGui::TableHeadersRow();
        for (std::size_t ScopedRow = 0; ScopedRow < ScopedRows; ++ScopedRow)
        {
            ImGui::TableNextRow();
            for (std::size_t ScopedColumn = 0; ScopedColumn < MTable.ColumnCount(); ++ScopedColumn)
            {
                ImGui::TableNextColumn();
                const std::string_view ScopedCell = MTable.Cell(ScopedRow, ScopedColumn);
                ImGui::TextUnformatted(ScopedCell.data(), ScopedCell.data() + ScopedCell.size());
            }
        }
        ImGui::EndTable();
    }
    EndEditorPanel();
}

void DataTablePanel::Load(Ecs::SystemContext& InContext)
{
    MTable.Clear();
    if (MPath[0] == '\0')
    {
        MStatus = "Enter a CSV or TSV path first.";
        return;
    }
    if (InContext.assets == nullptr)
    {
        MStatus = "The VFS is not available.";
        return;
    }
    const auto ScopedPinned = InContext.assets->Pin(MPath.data());
    if (!ScopedPinned)
    {
        MStatus = "The file is not present in the VFS.";
        return;
    }
    const std::string_view ScopedText(reinterpret_cast<const char*>(ScopedPinned->bytes.data()),
                                      ScopedPinned->bytes.size());
    const std::string_view ScopedPath(MPath.data());
    const char ScopedDelimiter = ScopedPath.ends_with(".tsv") ? '\t' : ',';
    if (!Fby::ParseDataTable(ScopedText, &MTable, ScopedDelimiter))
    {
        MStatus = "The table could not be parsed.";
        MTable.Clear();
        return;
    }
    MStatus = std::string("loaded ") + MPath.data();
}

}
