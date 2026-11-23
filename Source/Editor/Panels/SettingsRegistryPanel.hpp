// FemboyEngine2D - generated settings panel.
#pragma once

#include "Editor/Infrastructure/SettingsPanelModel.hpp"
#include "Engine/Core/SettingsRegistry.hpp"

#include <array>
#include <string>
#include <vector>

namespace Fby::Editor
{

// Draws every row produced by SettingsPanelModel. The panel owns no setting ids
// or defaults; a game registers a schema and the panel renders it.
struct SettingsRowHit
{
    std::string Id;
    float MinX = 0.0f;
    float MinY = 0.0f;
    float MaxX = 0.0f;
    float MaxY = 0.0f;
};

class SettingsRegistryPanel
{
public:
    void SetRegistry(Settings::Registry* InRegistry, Settings::Scope InScope);
    void Draw(const char* InTitle, bool* InOpen);
    // Draws the generated rows inside an existing window.
    void DrawInline();
    [[nodiscard]] std::span<const SettingsRowHit> Hits() const noexcept
    {
        return MHits;
    }

private:
    void DrawRows();
    void DrawRow(const SettingsRow& InRow);
    void DrawConfirmation();

    Settings::Registry* MRegistry = nullptr;
    SettingsPanelModel MModel;
    Settings::Scope MScope = Settings::Scope::User;
    std::array<char, 128> MSearch{};
    bool MShowAdvanced = false;
    bool MShowHidden = false;
    std::string MPendingId;
    Settings::Value MPendingValue{};
    bool MConfirmationOpen = false;
    std::string MStatus;
    std::vector<SettingsRowHit> MHits;
};

}