// FemboyEngine2D - accessibility QA audit implementation.
#include "Editor/Panels/AccessibilityQaPanel.hpp"

#include "Engine/UI/Accessibility.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace Fby::Editor
{
namespace
{
[[nodiscard]] bool IsInteractive(Ui::AccessibilityRole InRole) noexcept
{
    switch (InRole)
    {
        case Ui::AccessibilityRole::Button:
        case Ui::AccessibilityRole::CheckBox:
        case Ui::AccessibilityRole::RadioButton:
        case Ui::AccessibilityRole::ToggleButton:
        case Ui::AccessibilityRole::Slider:
        case Ui::AccessibilityRole::ScrollBar:
        case Ui::AccessibilityRole::TextInput:
        case Ui::AccessibilityRole::ListItem:
        case Ui::AccessibilityRole::TreeItem:
        case Ui::AccessibilityRole::Tab:
        case Ui::AccessibilityRole::MenuItem:
        case Ui::AccessibilityRole::ComboBox:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] std::string Timestamp()
{
    const std::time_t ScopedNow = std::time(nullptr);
    std::tm ScopedLocal{};
#if defined(_WIN32)
    localtime_s(&ScopedLocal, &ScopedNow);
#else
    localtime_r(&ScopedNow, &ScopedLocal);
#endif
    std::array<char, 32> ScopedBuffer{};
    std::strftime(ScopedBuffer.data(), ScopedBuffer.size(), "%Y%m%d-%H%M%S", &ScopedLocal);
    return ScopedBuffer.data();
}
}

void AccessibilityQaPanel::RunAudit(const Ui::AccessibilityTree& InTree, const AccessibilityProfile& InProfile)
{
    MFindings.clear();
    MNodeCount = InTree.Count();
    MUnnamedInteractive = 0;
    MUnknownRoles = 0;
    if (MNodeCount == 0)
    {
        MFindings.push_back({2, "accessibility tree is empty: build the UI before running the audit"});
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MNodeCount; ++ScopedIndex)
    {
        const Ui::AccessibilityNodeView ScopedNode = InTree.At(ScopedIndex);
        if (ScopedNode.Role == Ui::AccessibilityRole::Unknown)
        {
            ++MUnknownRoles;
            MFindings.push_back({1, "node " + std::to_string(ScopedNode.Id) + " has an unknown role"});
        }
        const std::string_view ScopedName = InTree.NameOf(ScopedNode);
        if (IsInteractive(ScopedNode.Role) && ScopedNode.NameId == 0 && ScopedName.empty())
        {
            ++MUnnamedInteractive;
            MFindings.push_back({2, "interactive node " + std::to_string(ScopedNode.Id) + " (" +
                                       Ui::AccessibilityRoleName(ScopedNode.Role) + ") has no accessible name"});
        }
        if (ScopedNode.Bounds.Z <= ScopedNode.Bounds.X || ScopedNode.Bounds.W <= ScopedNode.Bounds.Y)
        {
            MFindings.push_back({1, "node " + std::to_string(ScopedNode.Id) + " has degenerate bounds"});
        }
    }
    if (InTree.FocusedId() != 0 && InTree.FocusedIndex() == Ui::kNoAccessibilityNode)
    {
        MFindings.push_back({2, "focused id " + std::to_string(InTree.FocusedId()) + " is not in the tree"});
    }
    if (AccessibilityHasFlag(InProfile.runtime.flags, AccessibilityBlindMode) && !InProfile.narration.Enabled)
    {
        MFindings.push_back({2, "Blind mode is on but the narrator is disabled"});
    }
    if (AccessibilityHasFlag(InProfile.runtime.flags, AccessibilityDeafMode) && !InProfile.runtime.showSubtitles)
    {
        MFindings.push_back({1, "Deaf mode is on but subtitles are off"});
    }
    if (!AccessibilityHasFlag(InProfile.runtime.flags, AccessibilityFlashGuard))
    {
        MFindings.push_back({0, "Flash guard is off; photosensitive players have no automatic protection"});
    }
    if (InProfile.narration.Language[0] == '\0')
    {
        MFindings.push_back({1, "narrator language is empty; the voice fallback will use the process locale"});
    }
    MAudited = true;
    MStatus = MFindings.empty() ? "audit passed" : "audit found " + std::to_string(MFindings.size()) + " issue(s)";
}

void AccessibilityQaPanel::WriteReport(const AccessibilityProfile& InProfile)
{
    const std::filesystem::path ScopedDirectory =
        std::filesystem::current_path() / "Saved" / "Accessibility";
    std::error_code ScopedError;
    std::filesystem::create_directories(ScopedDirectory, ScopedError);
    if (ScopedError)
    {
        MStatus = "cannot create Saved/Accessibility";
        return;
    }
    const std::filesystem::path ScopedPath = ScopedDirectory / ("qa-" + Timestamp() + ".md");
    std::ofstream ScopedStream(ScopedPath, std::ios::binary | std::ios::trunc);
    if (!ScopedStream)
    {
        MStatus = "cannot write the QA report";
        return;
    }
    ScopedStream << "# Accessibility QA report\n\n"
                 << "- nodes: " << MNodeCount << "\n"
                 << "- unnamed interactive: " << MUnnamedInteractive << "\n"
                 << "- unknown roles: " << MUnknownRoles << "\n"
                 << "- blind mode: " << (AccessibilityHasFlag(InProfile.runtime.flags, AccessibilityBlindMode) ? "on" : "off")
                 << "\n- deaf mode: " << (AccessibilityHasFlag(InProfile.runtime.flags, AccessibilityDeafMode) ? "on" : "off")
                 << "\n- flash guard: "
                 << (AccessibilityHasFlag(InProfile.runtime.flags, AccessibilityFlashGuard) ? "on" : "off")
                 << "\n- narrator: " << (InProfile.narration.Enabled ? "on" : "off") << "\n\n## Findings\n\n";
    if (MFindings.empty())
    {
        ScopedStream << "No findings.\n";
    }
    for (const Finding& ScopedFinding : MFindings)
    {
        ScopedStream << "- [" << (ScopedFinding.Severity >= 2 ? "error" : (ScopedFinding.Severity == 1 ? "warning" : "info"))
                     << "] " << ScopedFinding.Text << "\n";
    }
    const std::string ScopedPathText = ScopedPath.generic_string();
    ScopedStream.close();
    MStatus = "report: " + ScopedPathText;
}

void AccessibilityQaPanel::Draw(Ui::AccessibilityTree& InTree, const AccessibilityProfile& InProfile, bool* InOpen)
{
    if (!ImGui::Begin("Accessibility QA", InOpen))
    {
        ImGui::End();
        return;
    }
    if (ImGui::Button("Run audit"))
    {
        RunAudit(InTree, InProfile);
    }
    ImGui::SameLine();
    if (ImGui::Button("Write QA report"))
    {
        WriteReport(InProfile);
    }
    if (!MStatus.empty())
    {
        ImGui::TextDisabled("%s", MStatus.c_str());
    }
    if (!MAudited)
    {
        ImGui::TextWrapped("The audit checks unnamed interactive nodes, unknown roles, degenerate bounds, focus "
                           "and the project accessibility profile (narrator, subtitles, flash guard, language).");
        ImGui::End();
        return;
    }
    ImGui::Separator();
    ImGui::Text("nodes %u   unnamed interactive %u   unknown roles %u", MNodeCount, MUnnamedInteractive,
                MUnknownRoles);
    if (MFindings.empty())
    {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "No accessibility findings.");
    }
    else
    {
        for (const Finding& ScopedFinding : MFindings)
        {
            const ImVec4 ScopedColor = ScopedFinding.Severity >= 2 ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                                      : ScopedFinding.Severity == 1 ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
                                                                     : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
            ImGui::TextColored(ScopedColor, "%s", ScopedFinding.Text.c_str());
        }
    }
    ImGui::End();
}
}
