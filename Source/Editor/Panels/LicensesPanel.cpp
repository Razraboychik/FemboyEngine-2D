// FemboyEngine2D - third-party license viewer implementation.
#include "Editor/Panels/LicensesPanel.hpp"

#include "Editor/Infrastructure/EditorTypography.hpp"

#include "Engine/Core/ThirdPartyNotices.hpp"

#include "imgui.h"

#include <span>
#include <string_view>

namespace Fby::Editor
{
namespace
{
// ImGui takes null-terminated strings, while the notice records are views. The
// helper prints a bounded view without copying it.
void TextView(std::string_view InPrefix, std::string_view InValue)
{
    ImGui::TextWrapped("%.*s%.*s", static_cast<int>(InPrefix.size()), InPrefix.data(),
                       static_cast<int>(InValue.size()), InValue.data());
}
}

void LicensesPanel::Draw(bool* InOpen)
{
    if (InOpen != nullptr && !*InOpen)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(820.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!BeginEditorPanel("Licenses", InOpen))
    {
        EndEditorPanel();
        return;
    }

    const std::span<const Notices::NoticeEntry> ScopedEntries = Notices::Entries();
    ImGui::TextDisabled("Generated from third_party/notices.toml: %u component(s) ship with this build.",
                        static_cast<unsigned>(ScopedEntries.size()));
    if (ScopedEntries.empty())
    {
        ImGui::TextWrapped("The notice table is empty. Run Documentation/Tools/GenerateThirdPartyNotices.py and rebuild.");
        EndEditorPanel();
        return;
    }
    if (MSelected >= ScopedEntries.size())
    {
        MSelected = 0;
    }

    if (ImGui::BeginChild("##licenses_list", ImVec2(220.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedEntries.size(); ++ScopedIndex)
        {
            if (ImGui::Selectable(ScopedEntries[ScopedIndex].Name.data(), MSelected == ScopedIndex))
            {
                MSelected = ScopedIndex;
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##licenses_body", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
    {
        const Notices::NoticeEntry& ScopedEntry = ScopedEntries[MSelected];
        TextView("", ScopedEntry.Name);
        ImGui::TextDisabled("version %.*s", static_cast<int>(ScopedEntry.Version.size()), ScopedEntry.Version.data());
        ImGui::Separator();
        TextView("Purpose: ", ScopedEntry.Purpose);
        TextView("Copyright: ", ScopedEntry.Copyright);
        TextView("License: ", ScopedEntry.Spdx);
        ImGui::Separator();
        ImGui::TextUnformatted(ScopedEntry.LicenseText.data(),
                               ScopedEntry.LicenseText.data() + ScopedEntry.LicenseText.size());
    }
    ImGui::EndChild();

    if (ImGui::Button("Copy full notices"))
    {
        const std::string_view ScopedAll = Notices::AllText();
        ImGui::SetClipboardText(ScopedAll.data());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("The packaged game ships THIRD_PARTY_NOTICES.md next to its archive.");

    EndEditorPanel();
}
}
