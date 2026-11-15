// FemboyEngine2D - crash diagnostics panel implementation.
#include "Editor/Panels/CrashDiagnosticsPanel.hpp"

#include "Engine/Core/CrashHandler.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Fby::Editor
{
namespace
{
struct ReportEntry
{
    std::filesystem::file_time_type Time;
    std::string Name;
    std::uintmax_t Bytes = 0;
};
}

void CrashDiagnosticsPanel::SetProject(std::string_view InName, std::string_view InRoot)
{
    MProjectName = InName;
    MProjectRoot = InRoot;
}

void CrashDiagnosticsPanel::Refresh()
{
    MReportCount = 0;
    const std::string_view ScopedSession = Core::LastCrashDirectory();
    if (ScopedSession.empty())
    {
        return;
    }
    const std::filesystem::path ScopedSessionPath(ScopedSession);
    std::error_code ScopedError;
    if (!std::filesystem::is_directory(ScopedSessionPath, ScopedError))
    {
        return;
    }
    for (const std::filesystem::directory_entry& ScopedEntry : std::filesystem::directory_iterator(ScopedSessionPath, ScopedError))
    {
        if (!ScopedEntry.is_regular_file(ScopedError))
        {
            continue;
        }
        ++MReportCount;
    }
}

void CrashDiagnosticsPanel::WriteManualReport()
{
    if (!Core::CrashHandlerInstalled())
    {
        MStatus = "crash handler is not installed";
        return;
    }
    const std::string ScopedDetails =
        "manual diagnostics\nproject=" + MProjectName + "\nroot=" + MProjectRoot + "\n";
    if (Core::WriteCrashReport("fby_editor", "manual diagnostics", ScopedDetails))
    {
        MStatus = "report written to " + std::string(Core::LastCrashDirectory());
        Refresh();
    }
    else
    {
        MStatus = "report could not be written";
    }
}

void CrashDiagnosticsPanel::Draw(bool* InOpen)
{
    if (!ImGui::Begin("Crash Diagnostics", InOpen))
    {
        ImGui::End();
        return;
    }
    if (MRefreshStamp == 0)
    {
        Refresh();
        MRefreshStamp = 1;
    }
    ImGui::Text("handler  %s", Core::CrashHandlerInstalled() ? "installed" : "not installed");
    ImGui::Text("project  %s", MProjectName.empty() ? "(none)" : MProjectName.c_str());
    if (!MProjectRoot.empty())
    {
        ImGui::TextDisabled("%s", MProjectRoot.c_str());
    }
    const std::string_view ScopedSession = Core::LastCrashDirectory();
    ImGui::Text("session  %s", ScopedSession.empty() ? "(no report yet)" : ScopedSession.data());
    if (!ScopedSession.empty())
    {
        ImGui::Text("reports  %u", MReportCount);
    }
    if (ImGui::Button("Write report now"))
    {
        WriteManualReport();
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
    {
        Refresh();
    }
    if (!MStatus.empty())
    {
        ImGui::TextDisabled("%s", MStatus.c_str());
    }
    ImGui::SeparatorText("Recent sessions");
    if (ScopedSession.empty())
    {
        ImGui::TextDisabled("No crash report has been written in this run.");
        ImGui::End();
        return;
    }
    const std::filesystem::path ScopedRoot = std::filesystem::path(ScopedSession).parent_path();
    std::error_code ScopedError;
    std::vector<ReportEntry> ScopedEntries;
    for (const std::filesystem::directory_entry& ScopedEntry : std::filesystem::directory_iterator(ScopedRoot, ScopedError))
    {
        if (!ScopedEntry.is_directory(ScopedError))
        {
            continue;
        }
        ReportEntry ScopedReport;
        ScopedReport.Time = ScopedEntry.last_write_time(ScopedError);
        ScopedReport.Name = ScopedEntry.path().filename().string();
        for (const std::filesystem::directory_entry& ScopedFile :
             std::filesystem::directory_iterator(ScopedEntry.path(), ScopedError))
        {
            if (ScopedFile.is_regular_file(ScopedError))
            {
                ScopedReport.Bytes += ScopedFile.file_size(ScopedError);
            }
        }
        ScopedEntries.push_back(std::move(ScopedReport));
    }
    std::sort(ScopedEntries.begin(), ScopedEntries.end(),
              [](const ReportEntry& InLeft, const ReportEntry& InRight) { return InLeft.Time > InRight.Time; });
    if (ImGui::BeginTable("crash_sessions", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Session");
        ImGui::TableSetupColumn("Size");
        ImGui::TableHeadersRow();
        const std::size_t ScopedShown = std::min<std::size_t>(ScopedEntries.size(), 16);
        for (std::size_t ScopedI = 0; ScopedI < ScopedShown; ++ScopedI)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(ScopedEntries[ScopedI].Name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f KiB", static_cast<double>(ScopedEntries[ScopedI].Bytes) / 1024.0);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
}
