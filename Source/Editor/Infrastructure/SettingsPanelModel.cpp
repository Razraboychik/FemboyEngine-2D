// FemboyEngine2D - generated settings panel model.
#include "Editor/Infrastructure/SettingsPanelModel.hpp"

#include <algorithm>
#include <cctype>

namespace Fby::Editor
{
namespace
{
std::string ToLower(std::string_view InText)
{
    std::string Scoped(InText);
    std::transform(Scoped.begin(), Scoped.end(), Scoped.begin(),
                   [](unsigned char InChar) { return static_cast<char>(std::tolower(InChar)); });
    return Scoped;
}
}

void SettingsPanelModel::Rebuild(const Settings::Registry& InRegistry)
{
    MRows.clear();
    for (const Settings::Descriptor& Scoped : InRegistry.Descriptors())
    {
        if (!Matches(Scoped))
        {
            continue;
        }
        SettingsRow ScopedRow;
        ScopedRow.Id = Scoped.Id;
        ScopedRow.Category = Scoped.Category;
        ScopedRow.Label = Scoped.Label;
        ScopedRow.LocalizationKey = Scoped.LocalizationKey;
        ScopedRow.Unit = Scoped.Unit;
        ScopedRow.ConfirmationText = Scoped.ConfirmationText;
        ScopedRow.RecoveryHotkey = Scoped.RecoveryHotkey;
        ScopedRow.Kind = Scoped.Kind;
        ScopedRow.Flags = Scoped.Flags;
        ScopedRow.Effective = InRegistry.Effective(Scoped.Id);
        ScopedRow.UserOverride = InRegistry.IsOverridden(Scoped.Id, MConfig.Scope);
        MRows.push_back(std::move(ScopedRow));
    }
}

const SettingsRow* SettingsPanelModel::Find(std::string_view InId) const
{
    const auto Scoped = std::find_if(MRows.begin(), MRows.end(),
                                     [InId](const SettingsRow& InRow) { return InRow.Id == InId; });
    return Scoped == MRows.end() ? nullptr : &*Scoped;
}

bool SettingsPanelModel::Set(Settings::Registry& InRegistry, const std::string& InId,
                             const Settings::Value& InValue, bool InConfirmed, std::string* InError)
{
    if (MConfig.Scope == Settings::Scope::Project)
    {
        return InRegistry.SetProject(InId, InValue, InError);
    }
    if (MConfig.Scope == Settings::Scope::Session)
    {
        return InRegistry.SetSession(InId, InValue, InConfirmed, InError);
    }
    return InRegistry.SetUser(InId, InValue, InConfirmed, InError);
}

bool SettingsPanelModel::Reset(Settings::Registry& InRegistry, const std::string& InId, std::string* InError)
{
    return InRegistry.ClearOverride(InId, MConfig.Scope, InError);
}

bool SettingsPanelModel::NeedsConfirmation(std::string_view InId) const
{
    const SettingsRow* Scoped = Find(InId);
    return Scoped != nullptr && (Scoped->Flags & Settings::FlagRequiresConfirmation) != 0u;
}

std::string SettingsPanelModel::ConfirmationText(std::string_view InId) const
{
    const SettingsRow* Scoped = Find(InId);
    return Scoped != nullptr ? Scoped->ConfirmationText : std::string{};
}

std::string SettingsPanelModel::RecoveryHotkey(std::string_view InId) const
{
    const SettingsRow* Scoped = Find(InId);
    return Scoped != nullptr ? Scoped->RecoveryHotkey : std::string{};
}

bool SettingsPanelModel::Matches(const Settings::Descriptor& InDescriptor) const
{
    if (!MConfig.ShowHidden && (InDescriptor.Flags & Settings::FlagHidden) != 0u)
    {
        return false;
    }
    if (!MConfig.ShowAdvanced && (InDescriptor.Flags & Settings::FlagAdvanced) != 0u)
    {
        return false;
    }
    if (MConfig.Search.empty())
    {
        return true;
    }
    const std::string ScopedSearch = ToLower(MConfig.Search);
    return ToLower(InDescriptor.Id).find(ScopedSearch) != std::string::npos ||
           ToLower(InDescriptor.Label).find(ScopedSearch) != std::string::npos ||
           ToLower(InDescriptor.Category).find(ScopedSearch) != std::string::npos;
}
}