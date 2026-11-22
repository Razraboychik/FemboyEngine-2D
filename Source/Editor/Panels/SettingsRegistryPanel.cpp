// FemboyEngine2D - generated settings panel.
#include "Editor/Panels/SettingsRegistryPanel.hpp"

#include <imgui.h>

#include <cstdio>

namespace Fby::Editor
{
namespace
{
Settings::Value FloatValue(double InValue)
{
    return Settings::Value::MakeFloat(InValue);
}
}

void SettingsRegistryPanel::SetRegistry(Settings::Registry* InRegistry, Settings::Scope InScope)
{
    MRegistry = InRegistry;
    MScope = InScope;
    MModel.SetConfig({.ShowAdvanced = MShowAdvanced, .ShowHidden = MShowHidden, .Scope = MScope, .Search = {}});
    if (MRegistry != nullptr)
    {
        MModel.Rebuild(*MRegistry);
    }
}

void SettingsRegistryPanel::Draw(const char* InTitle, bool* InOpen)
{
    if (!*InOpen || MRegistry == nullptr)
    {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(720.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(InTitle, InOpen))
    {
        ImGui::End();
        return;
    }

    if (ImGui::InputTextWithHint("##settings-search", "Search settings", MSearch.data(), MSearch.size()))
    {
        MModel.SetConfig({.ShowAdvanced = MShowAdvanced,
                          .ShowHidden = MShowHidden,
                          .Scope = MScope,
                          .Search = MSearch.data()});
        MModel.Rebuild(*MRegistry);
    }
    ImGui::SameLine();
    bool ScopedChanged = ImGui::Checkbox("Advanced", &MShowAdvanced);
    ImGui::SameLine();
    ScopedChanged |= ImGui::Checkbox("Hidden", &MShowHidden);
    if (ScopedChanged)
    {
        MModel.SetConfig({.ShowAdvanced = MShowAdvanced,
                          .ShowHidden = MShowHidden,
                          .Scope = MScope,
                          .Search = MSearch.data()});
        MModel.Rebuild(*MRegistry);
    }
    ImGui::Separator();

    std::string ScopedCategory;
    for (const SettingsRow& ScopedRow : MModel.Rows())
    {
        if (ScopedRow.Category != ScopedCategory)
        {
            ScopedCategory = ScopedRow.Category;
            if (!ImGui::CollapsingHeader(ScopedCategory.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                continue;
            }
        }
        DrawRow(ScopedRow);
    }
    if (MModel.Rows().empty())
    {
        ImGui::TextDisabled("No settings match the current filter.");
    }
    if (!MStatus.empty())
    {
        ImGui::Separator();
        ImGui::TextWrapped("%s", MStatus.c_str());
    }
    ImGui::End();
    DrawConfirmation();
}

void SettingsRegistryPanel::DrawRow(const SettingsRow& InRow)
{
    ImGui::PushID(InRow.Id.c_str());
    const Settings::Descriptor* ScopedDescriptor = MRegistry->Find(InRow.Id);
    if (ScopedDescriptor == nullptr)
    {
        ImGui::PopID();
        return;
    }
    const bool ScopedDangerous = (InRow.Flags & Settings::FlagDangerous) != 0u;
    const bool ScopedRestart = (InRow.Flags & Settings::FlagRequiresRestart) != 0u;
    const char* ScopedLabel = InRow.LocalizationKey.empty() ? InRow.Label.c_str() : InRow.LocalizationKey.c_str();

    Settings::Value ScopedValue = InRow.Effective;
    bool ScopedChanged = false;
    switch (InRow.Kind)
    {
        case Settings::Type::Bool:
        {
            bool ScopedBool = ScopedValue.BoolValue;
            ScopedChanged = ImGui::Checkbox(ScopedLabel, &ScopedBool);
            ScopedValue = Settings::Value::MakeBool(ScopedBool);
            break;
        }
        case Settings::Type::Int:
        {
            int ScopedInt = static_cast<int>(ScopedValue.IntValue);
            ScopedChanged = ImGui::SliderInt(ScopedLabel, &ScopedInt,
                                             static_cast<int>(ScopedDescriptor->Minimum),
                                             static_cast<int>(ScopedDescriptor->Maximum));
            ScopedValue = Settings::Value::MakeInt(ScopedInt);
            break;
        }
        case Settings::Type::Float:
        {
            float ScopedFloat = static_cast<float>(ScopedValue.FloatValue);
            ScopedChanged = ImGui::SliderFloat(ScopedLabel, &ScopedFloat,
                                               static_cast<float>(ScopedDescriptor->Minimum),
                                               static_cast<float>(ScopedDescriptor->Maximum), "%.2f");
            ScopedValue = FloatValue(ScopedFloat);
            break;
        }
        case Settings::Type::Color:
        {
            float ScopedColor[4] = {ScopedValue.ColorValue.R, ScopedValue.ColorValue.G, ScopedValue.ColorValue.B,
                                    ScopedValue.ColorValue.A};
            ScopedChanged = ImGui::ColorEdit4(ScopedLabel, ScopedColor);
            ScopedValue = Settings::Value::MakeColor({ScopedColor[0], ScopedColor[1], ScopedColor[2], ScopedColor[3]});
            break;
        }
        case Settings::Type::String:
        {
            std::array<char, 256> ScopedBuffer{};
            std::snprintf(ScopedBuffer.data(), ScopedBuffer.size(), "%s", ScopedValue.StringValue.c_str());
            if (ImGui::InputText(ScopedLabel, ScopedBuffer.data(), ScopedBuffer.size()))
            {
                ScopedChanged = true;
                ScopedValue = Settings::Value::MakeString(ScopedBuffer.data());
            }
            break;
        }
        case Settings::Type::Enum:
            ImGui::TextUnformatted(ScopedLabel);
            break;
    }

    if (ScopedChanged)
    {
        if (ScopedDangerous)
        {
            MPendingId = InRow.Id;
            MPendingValue = ScopedValue;
            MConfirmationOpen = true;
            ImGui::OpenPopup("Confirm setting");
        }
        else
        {
            std::string ScopedError;
            if (MModel.Set(*MRegistry, InRow.Id, ScopedValue, false, &ScopedError))
            {
                MStatus = "applied " + InRow.Id;
                MModel.Rebuild(*MRegistry);
            }
            else
            {
                MStatus = ScopedError;
            }
        }
    }
    if (ScopedRestart)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("restart");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset"))
    {
        std::string ScopedError;
        if (MModel.Reset(*MRegistry, InRow.Id, &ScopedError))
        {
            MStatus = "reset " + InRow.Id;
            MModel.Rebuild(*MRegistry);
        }
        else
        {
            MStatus = ScopedError;
        }
    }
    ImGui::PopID();
}

void SettingsRegistryPanel::DrawConfirmation()
{
    if (!MConfirmationOpen)
    {
        return;
    }
    if (ImGui::BeginPopupModal("Confirm setting", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        const SettingsRow* ScopedRow = MModel.Find(MPendingId);
        ImGui::TextWrapped("%s", ScopedRow != nullptr ? ScopedRow->ConfirmationText.c_str() : "Dangerous setting");
        if (ScopedRow != nullptr && !ScopedRow->RecoveryHotkey.empty())
        {
            ImGui::TextDisabled("Recovery: %s", ScopedRow->RecoveryHotkey.c_str());
        }
        if (ImGui::Button("Apply"))
        {
            std::string ScopedError;
            if (MModel.Set(*MRegistry, MPendingId, MPendingValue, true, &ScopedError))
            {
                MStatus = "applied " + MPendingId;
                MModel.Rebuild(*MRegistry);
            }
            else
            {
                MStatus = ScopedError;
            }
            MConfirmationOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            MConfirmationOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
}