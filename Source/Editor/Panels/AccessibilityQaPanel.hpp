// FemboyEngine2D - accessibility QA panel: tree and project-profile audit.
#pragma once

#include "Engine/UI/AccessibilityTree.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Fby
{
struct AccessibilityProfile;
}

namespace Fby::Editor
{

// Audits the semantic tree the editor publishes and the project accessibility
// profile. The report is a plain Markdown file a tester can attach to a build.
class AccessibilityQaPanel
{
public:
    void Draw(Ui::AccessibilityTree& InTree, const AccessibilityProfile& InProfile, bool* InOpen);

private:
    struct Finding
    {
        int Severity = 0;
        std::string Text;
    };

    void RunAudit(const Ui::AccessibilityTree& InTree, const AccessibilityProfile& InProfile);
    void WriteReport(const AccessibilityProfile& InProfile);

    std::vector<Finding> MFindings;
    std::string MStatus;
    std::uint32_t MNodeCount = 0;
    std::uint32_t MUnnamedInteractive = 0;
    std::uint32_t MUnknownRoles = 0;
    bool MAudited = false;
};
}
