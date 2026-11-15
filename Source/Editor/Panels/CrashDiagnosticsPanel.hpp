// FemboyEngine2D - crash reports and manual diagnostics panel.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Fby::Editor
{

// Shows the crash handler state, writes a report on demand and lists the
// reports this process and previous sessions left under Saved/Crashes.
class CrashDiagnosticsPanel
{
public:
    void SetProject(std::string_view InName, std::string_view InRoot);
    void Draw(bool* InOpen);

private:
    void Refresh();
    void WriteManualReport();

    std::string MProjectName;
    std::string MProjectRoot;
    std::string MStatus;
    std::uint64_t MRefreshStamp = 0;
    std::uint32_t MReportCount = 0;
};
}
