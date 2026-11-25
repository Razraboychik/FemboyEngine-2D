// FemboyEngine2D - generated settings panel model.
//
// The model owns grouping, filtering, reset and confirmation state. A renderer
// (ImGui in the editor) only draws Rows() and forwards edits; it never knows a
// setting's type or id ahead of time.
#pragma once

#include "Engine/Core/SettingsRegistry.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Editor
{

struct SettingsRow
{
    std::string Id;
    std::string Category;
    std::string Label;
    std::string LocalizationKey;
    std::string Unit;
    std::string ConfirmationText;
    std::string RecoveryHotkey;
    Settings::Type Kind = Settings::Type::Bool;
    Settings::Value Effective{};
    std::uint32_t Flags = 0;
    bool UserOverride = false;
};

class SettingsPanelModel
{
public:
    struct Config
    {
        bool ShowAdvanced = false;
        bool ShowHidden = false;
        Settings::Scope Scope = Settings::Scope::User;
        std::string Search;
    };

    void Rebuild(const Settings::Registry& InRegistry);
    [[nodiscard]] std::span<const SettingsRow> Rows() const noexcept
    {
        return MRows;
    }
    [[nodiscard]] const SettingsRow* Find(std::string_view InId) const;

    void SetConfig(Config InConfig)
    {
        MConfig = std::move(InConfig);
    }
    [[nodiscard]] const Config& GetConfig() const noexcept
    {
        return MConfig;
    }

    [[nodiscard]] bool Set(Settings::Registry& InRegistry, const std::string& InId,
                           const Settings::Value& InValue, bool InConfirmed, std::string* InError = nullptr);
    [[nodiscard]] bool Reset(Settings::Registry& InRegistry, const std::string& InId,
                             std::string* InError = nullptr);

    [[nodiscard]] bool NeedsConfirmation(std::string_view InId) const;
    [[nodiscard]] std::string ConfirmationText(std::string_view InId) const;
    [[nodiscard]] std::string RecoveryHotkey(std::string_view InId) const;

private:
    [[nodiscard]] bool Matches(const Settings::Descriptor& InDescriptor) const;

    Config MConfig{};
    std::vector<SettingsRow> MRows;
};

}