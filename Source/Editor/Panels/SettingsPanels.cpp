// FemboyEngine2D - transactional Settings panels.
#include "Editor/Panels/SettingsPanels.hpp"

#include "Editor/Infrastructure/EditorTheme.hpp"
#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Engine/UI/Accessibility.hpp"
#include "Engine/Audio/Audio.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/ViewportModes.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <span>
#include <string>

namespace Fby::Editor
{
namespace
{
void CopyProjectField(char* InDestination, std::size_t InCapacity, const char* InSource)
{
    if (InDestination == nullptr || InCapacity == 0)
    {
        return;
    }
    const std::size_t ScopedLength = std::min<std::size_t>(std::strlen(InSource), InCapacity - 1);
    std::memcpy(InDestination, InSource, ScopedLength);
    InDestination[ScopedLength] = '\0';
}

void Persist(ProjectConfigStore& InStore)
{
    std::string ScopedError;
    if (!InStore.PersistIfChanged(&ScopedError))
    {
        LogWarn("settings", "cannot persist project config: {}", ScopedError);
    }
}

const Assets::TextureFormat kCookFormats[] = {
    Assets::TextureFormat::Bc7Srgb,      Assets::TextureFormat::Bc7Unorm,    Assets::TextureFormat::Astc4x4Srgb,
    Assets::TextureFormat::Astc4x4Unorm, Assets::TextureFormat::Astc6x6Srgb, Assets::TextureFormat::Astc6x6Unorm,
    Assets::TextureFormat::Rgba8,
};

int CookFormatIndex(Assets::TextureFormat InFormat) noexcept
{
    for (int ScopedI = 0; ScopedI < static_cast<int>(std::size(kCookFormats)); ++ScopedI)
    {
        if (kCookFormats[ScopedI] == InFormat)
        {
            return ScopedI;
        }
    }
    return 0;
}

bool DrawCookFormatCombo(Assets::TextureFormat* InFormat)
{
    int ScopedIndex = CookFormatIndex(*InFormat);
    const char* ScopedLabels[std::size(kCookFormats)]{};
    for (std::size_t ScopedI = 0; ScopedI < std::size(kCookFormats); ++ScopedI)
    {
        ScopedLabels[ScopedI] = Assets::TextureFormatName(kCookFormats[ScopedI]);
    }
    if (ImGui::Combo("Cook format", &ScopedIndex, ScopedLabels, static_cast<int>(std::size(ScopedLabels))))
    {
        *InFormat = kCookFormats[static_cast<std::size_t>(ScopedIndex)];
        return true;
    }
    return false;
}

// IDE accessibility
struct IdePalette
{
    std::array<ImVec4, ImGuiCol_COUNT> base{};
    FlashGuard flash{};
    bool captured = false;
};

IdePalette& Palette()
{
    static IdePalette ScopedPalette;
    return ScopedPalette;
}

[[nodiscard]] float LinearToSrgb(float InChannel) noexcept
{
    const float ScopedValue = std::clamp(InChannel, 0.0f, 1.0f);
    if (ScopedValue <= 0.0031308f)
    {
        return 12.92f * ScopedValue;
    }
    return 1.055f * std::pow(ScopedValue, 1.0f / 2.4f) - 0.055f;
}

[[nodiscard]] Color32 ToColor32(const ImVec4& InLinear) noexcept
{
    const auto ScopedByte = [](float InValue)
    {
        const float ScopedSrgb = LinearToSrgb(InValue);
        return static_cast<std::uint8_t>(std::lround(std::clamp(ScopedSrgb, 0.0f, 1.0f) * 255.0f));
    };
    return Color32::FromBytes(ScopedByte(InLinear.x), ScopedByte(InLinear.y), ScopedByte(InLinear.z),
                              static_cast<std::uint8_t>(std::lround(std::clamp(InLinear.w, 0.0f, 1.0f) * 255.0f)));
}

[[nodiscard]] ImVec4 ToLinear(Color32 InColor) noexcept
{
    return ImVec4(SrgbToLinear(static_cast<float>(InColor.R()) / 255.0f),
                  SrgbToLinear(static_cast<float>(InColor.G()) / 255.0f),
                  SrgbToLinear(static_cast<float>(InColor.B()) / 255.0f), static_cast<float>(InColor.A()) / 255.0f);
}

// WCAG 2.2: pick the nearest of black/white that reaches 4.5:1. A high-contrast
[[nodiscard]] ImVec4 EnforceContrast(const ImVec4& InColor, const ImVec4& InBackground, float InMinimumRatio) noexcept
{
    const Color32 ScopedColor = ToColor32(InColor);
    const Color32 ScopedBackground = ToColor32(InBackground);
    if (ContrastRatio(ScopedColor, ScopedBackground) >= InMinimumRatio)
    {
        return InColor;
    }
    const Color32 ScopedWhite{0xFFFFFFFFu};
    const Color32 ScopedBlack{0xFF000000u};
    const bool ScopedWhiteWins =
        ContrastRatio(ScopedWhite, ScopedBackground) >= ContrastRatio(ScopedBlack, ScopedBackground);
    ImVec4 ScopedResult = ToLinear(ScopedWhiteWins ? ScopedWhite : ScopedBlack);
    ScopedResult.w = InColor.w;
    return ScopedResult;
}

void ApplyIdeAccessibility(const AccessibilityEditorSettings& InSettings)
{
    ImGuiStyle& ScopedStyle = ImGui::GetStyle();
    IdePalette& ScopedPalette = Palette();
    if (!ScopedPalette.captured)
    {
        for (int ScopedIndex = 0; ScopedIndex < ImGuiCol_COUNT; ++ScopedIndex)
        {
            ScopedPalette.base[static_cast<std::size_t>(ScopedIndex)] =
                ScopedStyle.Colors[static_cast<std::size_t>(ScopedIndex)];
        }
        ScopedPalette.captured = true;
    }

    // Always start from the authored palette.
    for (int ScopedIndex = 0; ScopedIndex < ImGuiCol_COUNT; ++ScopedIndex)
    {
        ScopedStyle.Colors[static_cast<std::size_t>(ScopedIndex)] =
            ScopedPalette.base[static_cast<std::size_t>(ScopedIndex)];
    }

    const ImVec4& ScopedBackground = ScopedStyle.Colors[ImGuiCol_WindowBg];

    if (InSettings.highContrast)
    {
        constexpr float kMinimumRatio = 4.5f;
        ScopedStyle.Colors[ImGuiCol_Text] =
            EnforceContrast(ScopedStyle.Colors[ImGuiCol_Text], ScopedBackground, kMinimumRatio);
        ScopedStyle.Colors[ImGuiCol_TextDisabled] =
            EnforceContrast(ScopedStyle.Colors[ImGuiCol_TextDisabled], ScopedBackground, kMinimumRatio);
        ScopedStyle.Colors[ImGuiCol_TextSelectedBg] =
            EnforceContrast(ScopedStyle.Colors[ImGuiCol_TextSelectedBg], ScopedBackground, kMinimumRatio);
        ScopedStyle.Colors[ImGuiCol_Border] =
            EnforceContrast(ScopedStyle.Colors[ImGuiCol_Border], ScopedBackground, 3.0f);
        ScopedStyle.Colors[ImGuiCol_NavCursor] = HexColor(kThemeAccentActive, 1.0f);
    }

    if (InSettings.blindMode)
    {
        // BlindMode for the window: keyboard navigation stays on, focus is
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ScopedStyle.Colors[ImGuiCol_NavCursor] = HexColor(kThemeAccentActive, 1.0f);
        ScopedStyle.Colors[ImGuiCol_Header] = HexColor(kThemeHeaderActive, 1.0f);
    }

    const std::uint8_t ScopedDeficiency = std::min<std::uint8_t>(InSettings.colorVision, 3);
    if (ScopedDeficiency != 0)
    {
        const ColorMatrix3 ScopedMatrix = ColorVisionMatrix(static_cast<ColorVisionDeficiency>(ScopedDeficiency));
        const float ScopedStrength = std::clamp(InSettings.colorVisionStrength, 0.0f, 1.0f);
        for (int ScopedIndex = 0; ScopedIndex < ImGuiCol_COUNT; ++ScopedIndex)
        {
            const Color32 ScopedCorrected = ApplyColorVision(
                ToColor32(ScopedStyle.Colors[static_cast<std::size_t>(ScopedIndex)]), ScopedMatrix, ScopedStrength);
            ScopedStyle.Colors[static_cast<std::size_t>(ScopedIndex)] = ToLinear(ScopedCorrected);
        }
    }

    if (InSettings.flashGuard)
    {
        const float ScopedLuminance = SrgbRelativeLuminance(ToColor32(ScopedBackground));
        const float ScopedGain =
            ScopedPalette.flash.Evaluate(ScopedLuminance, std::max(1.0e-4f, ImGui::GetIO().DeltaTime));
        if (ScopedGain < 1.0f)
        {
            const ImU32 ScopedBackgroundTargets[] = {ImGuiCol_WindowBg, ImGuiCol_ChildBg, ImGuiCol_PopupBg};
            for (const ImU32 ScopedColor : ScopedBackgroundTargets)
            {
                ImVec4& ScopedTarget = ScopedStyle.Colors[ScopedColor];
                ScopedTarget.x *= ScopedGain;
                ScopedTarget.y *= ScopedGain;
                ScopedTarget.z *= ScopedGain;
            }
        }
    }
    else
    {
        ScopedPalette.flash.Reset();
    }

    // Reduce motion: no blinking text caret (the single continuous animation the
    ImGui::GetIO().ConfigInputTextCursorBlink = !InSettings.reduceMotion;
}
}

// ProjectSettingsPanel
void ProjectSettingsPanel::Initialize(CommandStack& InCommands, ProjectConfigStore& InStore,
                                      std::uint16_t InCodec) noexcept
{
    MCommands = &InCommands;
    MStore = &InStore;
    MCodec = InCodec;
    MAppliedValid = false;
    MDirty = false;
}

ProjectSettings& ProjectSettingsPanel::Values() noexcept
{
    return MStore->Project();
}

const ProjectSettings& ProjectSettingsPanel::Values() const noexcept
{
    return MStore->Project();
}

void ProjectSettingsPanel::BeginOrRecord(Ecs::SystemContext& InContext,
                                         const std::array<std::byte, sizeof(ProjectSettings)>& InBefore) noexcept
{
    (void)InContext;
    if (MCommands == nullptr || MCodec == 0xFFFFu)
    {
        return;
    }
    std::array<std::byte, sizeof(ProjectSettings)> ScopedCurrent{};
    if (MCommands->CaptureComponent(entt::null, MCodec, ScopedCurrent) != CommandStack::Result::Ok)
    {
        return;
    }
    // Compare the typed image, never the raw CommandStack bytes: padding must
    ProjectSettings ScopedBeforeTyped{};
    ProjectSettings ScopedCurrentTyped{};
    std::memcpy(&ScopedBeforeTyped, InBefore.data(), sizeof(ProjectSettings));
    std::memcpy(&ScopedCurrentTyped, ScopedCurrent.data(), sizeof(ProjectSettings));
    if (ScopedBeforeTyped == ScopedCurrentTyped)
    {
        return;
    }
    if (!MTransaction.Active())
    {
        (void)MTransaction.BeginWithSnapshot(*MCommands, entt::null, MCodec, "Project settings",
                                             InteractionTransaction::Source::Settings,
                                             std::span<const std::byte>(InBefore.data(), InBefore.size()));
    }
    if (MTransaction.Active())
    {
        (void)MTransaction.Record();
        MDirty = true;
    }
}

void ProjectSettingsPanel::CommitIfFinished(Ecs::SystemContext& InContext)
{
    if (!MTransaction.Active() || ImGui::IsAnyItemActive())
    {
        return;
    }
    const CommandStack::Result ScopedResult = MTransaction.Commit();
    if (ScopedResult != CommandStack::Result::Ok)
    {
        LogWarn("settings", "project settings commit failed: {}", CommandStack::ResultName(ScopedResult));
        return;
    }
    MDirty = false;
    Apply(InContext);
    if (MStore != nullptr)
    {
        Persist(*MStore);
    }
}

void ProjectSettingsPanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (MStore == nullptr)
    {
        // No project store: there is nothing to edit. Do not call End() here,
        return;
    }
    if (!BeginEditorPanel("Project Settings", InOpen))
    {
        EndEditorPanel();
        return;
    }

    ProjectSettings& ScopedValues = Values();
    std::array<std::byte, sizeof(ProjectSettings)> ScopedBefore{};
    const bool ScopedTrackable =
        MCommands != nullptr && MCodec != 0xFFFFu &&
        MCommands->CaptureComponent(entt::null, MCodec, ScopedBefore) == CommandStack::Result::Ok;

    ImGui::TextDisabled("Game-facing settings. Commits are one undo step and rewrite project.femproj atomically.");
    ImGui::Separator();

    char ScopedName[kProjectNameCapacity]{};
    std::memcpy(ScopedName, ScopedValues.name, sizeof(ScopedName));
    if (ImGui::InputText("Project name", ScopedName, sizeof(ScopedName)))
    {
        CopyProjectField(ScopedValues.name, sizeof(ScopedValues.name), ScopedName);
    }
    char ScopedVersion[kProjectVersionCapacity]{};
    std::memcpy(ScopedVersion, ScopedValues.version, sizeof(ScopedVersion));
    if (ImGui::InputText("Version", ScopedVersion, sizeof(ScopedVersion)))
    {
        CopyProjectField(ScopedValues.version, sizeof(ScopedValues.version), ScopedVersion);
    }
    std::array<char, sizeof(ScopedValues.studioName)> ScopedStudio{};
    std::memcpy(ScopedStudio.data(), ScopedValues.studioName, sizeof(ScopedValues.studioName));
    if (ImGui::InputText("Studio", ScopedStudio.data(), ScopedStudio.size()))
    {
        CopyProjectField(ScopedValues.studioName, sizeof(ScopedValues.studioName), ScopedStudio.data());
    }
    std::array<char, sizeof(ScopedValues.description)> ScopedDescription{};
    std::memcpy(ScopedDescription.data(), ScopedValues.description, sizeof(ScopedValues.description));
    if (ImGui::InputTextMultiline("Description", ScopedDescription.data(), ScopedDescription.size(),
                                  ImVec2(0.0f, 54.0f)))
    {
        CopyProjectField(ScopedValues.description, sizeof(ScopedValues.description), ScopedDescription.data());
    }
    std::array<char, sizeof(ScopedValues.copyrightHeader)> ScopedCopyright{};
    std::memcpy(ScopedCopyright.data(), ScopedValues.copyrightHeader, sizeof(ScopedValues.copyrightHeader));
    if (ImGui::InputText("Copyright header", ScopedCopyright.data(), ScopedCopyright.size()))
    {
        CopyProjectField(ScopedValues.copyrightHeader, sizeof(ScopedValues.copyrightHeader), ScopedCopyright.data());
    }
    ImGui::Checkbox("Show splash on game launch", &ScopedValues.showSplash);
    ImGui::TextDisabled("The splash shows the logo, engine version, studio and project description.");
    char ScopedScene[kProjectScenePathCapacity]{};
    std::memcpy(ScopedScene, ScopedValues.defaultScene, sizeof(ScopedScene));
    if (ImGui::InputText("Default scene", ScopedScene, sizeof(ScopedScene)))
    {
        CopyProjectField(ScopedValues.defaultScene, sizeof(ScopedValues.defaultScene), ScopedScene);
    }

    ImGui::SeparatorText("Gameplay Tags");
    ImGui::TextDisabled("One project database. Definitions are global; per-entity values live on the entity.");
    ImGui::TextDisabled("Ids follow list order and the runtime registry is rebuilt for every Apply.");
    GameplayTagDatabase& ScopedTagDb = ScopedValues.gameplayTags;
    if (MSelectedTag >= static_cast<int>(ScopedTagDb.Count))
    {
        MSelectedTag = ScopedTagDb.Count == 0 ? -1 : static_cast<int>(ScopedTagDb.Count - 1);
    }

    ImGui::InputTextWithHint("##tag_search", "Filter tags", MTagSearch.data(), MTagSearch.size());
    ImGui::SameLine();
    if (ImGui::Button("Clear filter"))
    {
        MTagSearch = {};
    }

    std::array<std::uint16_t, GameplayTagDatabase::kCapacity> ScopedOrder{};
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedTagDb.Count; ++ScopedI)
    {
        ScopedOrder[ScopedI] = static_cast<std::uint16_t>(ScopedI);
    }
    std::sort(ScopedOrder.begin(), ScopedOrder.begin() + ScopedTagDb.Count,
              [&ScopedTagDb](std::uint16_t InA, std::uint16_t InB)
              {
                  return ScopedTagDb.PathAt(InA) < ScopedTagDb.PathAt(InB);
              });

    ImGui::BeginChild("##gameplay_tag_list", ImVec2(0.0f, 180.0f), true);
    const std::string_view ScopedFilter(MTagSearch.data());
    for (std::uint32_t ScopedRank = 0; ScopedRank < ScopedTagDb.Count; ++ScopedRank)
    {
        const std::uint32_t ScopedIndex = ScopedOrder[ScopedRank];
        const std::string_view ScopedPath = ScopedTagDb.PathAt(ScopedIndex);
        if (!ScopedFilter.empty() && ScopedPath.find(ScopedFilter) == std::string_view::npos)
        {
            continue;
        }
        std::uint32_t ScopedDepth = 0;
        for (const char ScopedChar : ScopedPath)
        {
            ScopedDepth += ScopedChar == '.' ? 1u : 0u;
        }
        ImGui::Indent(static_cast<float>(std::min(ScopedDepth, 8u)) * 10.0f);
        const bool ScopedSelected = MSelectedTag == static_cast<int>(ScopedIndex);
        if (ImGui::Selectable(std::string(ScopedPath).c_str(), ScopedSelected))
        {
            MSelectedTag = static_cast<int>(ScopedIndex);
        }
        ImGui::Unindent(static_cast<float>(std::min(ScopedDepth, 8u)) * 10.0f);
    }
    ImGui::EndChild();

    if (MSelectedTag >= 0 && MSelectedTag < static_cast<int>(ScopedTagDb.Count))
    {
        const std::string_view ScopedSelectedPath = ScopedTagDb.PathAt(static_cast<std::uint32_t>(MSelectedTag));
        if (MSelectedTag != MLastSelectedTag)
        {
            MRenameTagPath = {};
            const std::size_t ScopedLength = std::min(ScopedSelectedPath.size(), MRenameTagPath.size() - 1u);
            std::memcpy(MRenameTagPath.data(), ScopedSelectedPath.data(), ScopedLength);
            MLastSelectedTag = MSelectedTag;
        }
        ImGui::Text("Selected: %.*s  (id %d)", static_cast<int>(ScopedSelectedPath.size()), ScopedSelectedPath.data(),
                    MSelectedTag + 1);
        const std::size_t ScopedDot = ScopedSelectedPath.rfind('.');
        if (ScopedDot == std::string_view::npos)
        {
            ImGui::TextDisabled("Parent: <root>");
        }
        else
        {
            ImGui::TextDisabled("Parent: %.*s", static_cast<int>(ScopedDot), ScopedSelectedPath.data());
        }
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("Rename to", MRenameTagPath.data(), MRenameTagPath.size());
        ImGui::SameLine();
        if (ImGui::Button("Rename"))
        {
            if (ScopedTagDb.Rename(static_cast<std::uint32_t>(MSelectedTag), MRenameTagPath.data()))
            {
                MNewTagPath = {};
                MLastSelectedTag = -1;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Add child"))
        {
            MNewTagPath = {};
            const std::size_t ScopedLength = std::min(ScopedSelectedPath.size(), MNewTagPath.size() - 8u);
            std::memcpy(MNewTagPath.data(), ScopedSelectedPath.data(), ScopedLength);
            std::memcpy(MNewTagPath.data() + ScopedLength, ".NewTag", 7);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete subtree"))
        {
            if (ScopedTagDb.RemoveAt(static_cast<std::uint32_t>(MSelectedTag)))
            {
                MSelectedTag = -1;
                MLastSelectedTag = -1;
                MNewTagPath = {};
            }
        }
        if (ImGui::IsItemHovered())
        {
            EditorTooltip(
                "Deletes the selected tag and every child path.\nThe whole project settings transaction is undoable.");
        }
    }

    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputTextWithHint("##new_tag", "New tag path, e.g. Character.State.Stunned", MNewTagPath.data(),
                             MNewTagPath.size());
    ImGui::SameLine();
    if (ImGui::Button("Add tag"))
    {
        const int ScopedAdded = ScopedTagDb.Add(MNewTagPath.data());
        if (ScopedAdded >= 0)
        {
            MSelectedTag = ScopedAdded;
            MLastSelectedTag = -1;
            MNewTagPath = {};
        }
    }
    if (!ScopedTagDb.LastError().empty())
    {
        ImGui::TextColored(ImVec4(0.95f, 0.36f, 0.31f, 1.0f), "%s", ScopedTagDb.LastError().data());
    }
    else
    {
        ImGui::TextDisabled("%u / %u tags. Add the parent path first or let the editor create it.", ScopedTagDb.Count,
                            GameplayTagDatabase::kCapacity);
    }

    ImGui::SeparatorText("Simulation");
    ImGui::SliderFloat("FixedUpdate", &ScopedValues.fixedUpdateHz, 15.0f, 240.0f, "%.0f Hz");
    int ScopedFixedSteps = static_cast<int>(ScopedValues.maxFixedSteps);
    if (ImGui::SliderInt("Max fixed steps", &ScopedFixedSteps, 1, 16))
    {
        ScopedValues.maxFixedSteps = static_cast<std::uint32_t>(ScopedFixedSteps);
    }

    ImGui::SeparatorText("Mixer");
    ImGui::SliderFloat("Master", &ScopedValues.masterVolume, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("SFX", &ScopedValues.sfxVolume, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Music", &ScopedValues.musicVolume, 0.0f, 2.0f, "%.2f");

    // One profile: the same values reach the game frame, the editor window and
    // the OLED policy, so a project and the person editing it cannot disagree.
    AccessibilityProfile& ScopedProfile = ScopedValues.accessibility;
    ImGui::SeparatorText("Accessibility");
    bool ScopedBlind = AccessibilityHasFlag(ScopedProfile.runtime.flags, AccessibilityBlindMode);
    if (ImGui::Checkbox("Blind mode (render skip + narrative log)", &ScopedBlind))
    {
        AccessibilitySetFlag(&ScopedProfile.runtime.flags, AccessibilityBlindMode, ScopedBlind);
    }
    bool ScopedDeaf = AccessibilityHasFlag(ScopedProfile.runtime.flags, AccessibilityDeafMode);
    if (ImGui::Checkbox("Deaf mode (Master mute + subtitles)", &ScopedDeaf))
    {
        AccessibilitySetFlag(&ScopedProfile.runtime.flags, AccessibilityDeafMode, ScopedDeaf);
    }
    ImGui::Checkbox("Show subtitles without muting", &ScopedProfile.runtime.showSubtitles);
    bool ScopedFlash = AccessibilityHasFlag(ScopedProfile.runtime.flags, AccessibilityFlashGuard);
    if (ImGui::Checkbox("FlashGuard", &ScopedFlash))
    {
        AccessibilitySetFlag(&ScopedProfile.runtime.flags, AccessibilityFlashGuard, ScopedFlash);
    }
    ImGui::BeginDisabled(!ScopedFlash);
    ImGui::SliderFloat("Flash threshold", &ScopedProfile.runtime.flashThreshold, 0.02f, 0.45f, "%.3f");
    ImGui::SliderFloat("Flash residual gain", &ScopedProfile.runtime.flashGain, 0.05f, 1.0f, "%.2f");
    ImGui::EndDisabled();

    const char* ScopedGameVisionNames[] = {"None", "Protanopia", "Deuteranopia", "Tritanopia"};
    int ScopedGameVision =
        static_cast<int>(std::min<std::uint8_t>(static_cast<std::uint8_t>(ScopedProfile.runtime.colorVision), 3));
    if (ImGui::Combo("Colour vision (game frame)", &ScopedGameVision, ScopedGameVisionNames, 4))
    {
        ScopedProfile.runtime.colorVision = static_cast<ColorVisionDeficiency>(ScopedGameVision);
        AccessibilitySetFlag(&ScopedProfile.runtime.flags, AccessibilityColorVisionMode, ScopedGameVision != 0);
    }
    ImGui::BeginDisabled(ScopedGameVision == 0);
    ImGui::SliderFloat("Colour vision strength", &ScopedProfile.runtime.colorVisionStrength, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();

    ImGui::SeparatorText("OLED safety");
    ImGui::Checkbox("Minute OLED Pixel Shifter", &ScopedProfile.presentation.oledPixelShift);
    ImGui::SameLine();
    ImGui::TextDisabled("(integer 2 px orbit)");
    ImGui::BeginDisabled(!ScopedProfile.presentation.oledPixelShift);
    ImGui::SliderFloat("Shift interval", &ScopedProfile.presentation.oledIntervalSeconds, 15.0f, 300.0f, "%.0f s");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Motion, HUD and contrast");
    ImGui::SliderFloat("Motion scale", &ScopedProfile.presentation.motionScale, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("HUD fade after idle", &ScopedProfile.presentation.hudFadeAfterIdleSeconds, 0.0f, 120.0f,
                       "%.0f s");
    ImGui::SliderFloat("Minimum contrast ratio", &ScopedProfile.presentation.minimumContrastRatio, 1.0f, 21.0f, "%.1f");

    ImGui::SeparatorText("Cooker");
    (void)DrawCookFormatCombo(&ScopedValues.textureFormat);
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Art-driven: RGBA8 for pixel art, BC7 for painted desktop art, ASTC for mobile.\n"
                      "Scale only changes max texture size and whether VT is needed.");
    }
    int ScopedMaxTextureSize = static_cast<int>(ScopedValues.maxTextureSize);
    if (ImGui::SliderInt("Max texture size", &ScopedMaxTextureSize, 256, 8192))
    {
        ScopedValues.maxTextureSize = static_cast<std::uint32_t>(ScopedMaxTextureSize);
    }

    ImGui::SeparatorText("Resolution");
    ImGui::SliderFloat("Render scale", &ScopedValues.renderScale, 0.25f, 1.0f, "%.2f");
    const char* ScopedUpscaleNames[5] = {"Nearest", "Bilinear", "Bicubic", "FSR1", "FSR2"};
    const char* ScopedUpscaleTips[5] = {
        "Integer pixel replication. Best for pixel art; blocky, no smoothing.",
        "Linear filter. Cheap and soft; blurs fine detail.",
        "Catmull-Rom style reconstruction. Sharper than bilinear; mild ringing.",
        "AMD FidelityFX Super Resolution 1: spatial edge-aware upscale, no motion vectors.",
        "FSR2-compatible temporal resolve with history and motion vectors."};
    int ScopedUpscale = static_cast<int>(ScopedValues.upscaleMode);
    if (ImGui::Combo("Upscale", &ScopedUpscale, ScopedUpscaleNames, 5))
    {
        ScopedValues.upscaleMode = static_cast<UpscaleMode>(ScopedUpscale);
    }
    if (ScopedValues.upscaleMode == UpscaleMode::Fsr2 && !MBackendCaps.Fsr2)
    {
        ScopedValues.upscaleMode = UpscaleMode::Fsr1;
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip(ScopedUpscaleTips[std::clamp(ScopedUpscale, 0, 4)]);
    }

    const bool ScopedTemporalCapable = MUpscalerCaps.motionVectors && (MUpscalerCaps.fsr2 || MUpscalerCaps.dlss);
    if (ScopedTemporalCapable)
    {
        ImGui::Checkbox("Allow temporal upscaling", &ScopedValues.allowTemporalUpscale);
        if (ImGui::IsItemHovered())
        {
            EditorTooltip("Project policy. Requires a compiled temporal backend and motion vectors.");
        }
        ImGui::BeginDisabled(!ScopedValues.allowTemporalUpscale);
        const char* ScopedTemporalNames[4] = {"Off", "FSR2", "DLSS", "Auto"};
        int ScopedTemporal = static_cast<int>(ScopedValues.temporalUpscale);
        if (ImGui::Combo("Temporal upscaler", &ScopedTemporal, ScopedTemporalNames, 4))
        {
            ScopedValues.temporalUpscale = static_cast<TemporalUpscaleMode>(ScopedTemporal);
        }
        ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Transparency");
    const char* ScopedOitModeNames[3] = {"Off", "Weighted", "Accurate"};
    const char* ScopedOitTips[3] = {
        "Back-to-front alpha blending. Cheapest and correct for most 2D scenes.",
        "Weighted Blended OIT: one order-independent approximation; cheap, but approximate.",
        "Per-pixel linked-list OIT: exact order independence, high bandwidth. Not wired yet."};
    int ScopedOitMode = static_cast<int>(ScopedValues.oitMode);
    if (ImGui::Combo("OIT", &ScopedOitMode, ScopedOitModeNames, 3))
    {
        ScopedValues.oitMode = static_cast<OitMode>(ScopedOitMode);
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip(ScopedOitTips[std::clamp(ScopedOitMode, 0, 2)]);
    }

    ImGui::SeparatorText("Static layer bake");
    ImGui::Checkbox("Enable static layer bake", &ScopedValues.staticBakeEnabled);
    ImGui::BeginDisabled(!ScopedValues.staticBakeEnabled);
    int ScopedBakeTile = static_cast<int>(ScopedValues.staticBakeTileSize);
    if (ImGui::SliderInt("Bake tile size", &ScopedBakeTile, 64, 1024))
    {
        ScopedValues.staticBakeTileSize = static_cast<std::uint32_t>(ScopedBakeTile);
    }
    int ScopedBakeBudget = static_cast<int>(ScopedValues.staticBakeBudgetMB);
    if (ImGui::SliderInt("Bake budget MB", &ScopedBakeBudget, 8, 1024))
    {
        ScopedValues.staticBakeBudgetMB = static_cast<std::uint32_t>(ScopedBakeBudget);
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Opaque main pass");
    const char* ScopedOpaqueModeNames[3] = {"Off", "On", "Auto"};
    int ScopedOpaqueMode = static_cast<int>(ScopedValues.opaqueMode);
    if (ImGui::Combo("Front-to-back opaque", &ScopedOpaqueMode, ScopedOpaqueModeNames, 3))
    {
        ScopedValues.opaqueMode = static_cast<OpaqueRenderMode>(ScopedOpaqueMode);
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Auto enables depth-tested front-to-back drawing only when every sprite is opaque.\n"
                      "Soft alpha edges are rendered as a hard cutoff; use Off for blended art.");
    }
    ImGui::Checkbox("Opaque art uses cutout alpha", &ScopedValues.opaqueArtCutout);
    if (ImGui::IsItemHovered())
    {
        EditorTooltip(
            "Required for Auto. Enable only when opaque textures use binary alpha; soft blended edges need Off.");
    }
    ImGui::SliderFloat("Opaque alpha cutoff", &ScopedValues.opaqueAlphaCutoff, 0.0f, 1.0f, "%.2f");

    // AOT cook profile. The combo is the only place the level is authored;
    ImGui::SeparatorText("AOT Cook / Final Build");
    const char* ScopedOptimizationNames[Build::kOptimizationLevelCount] = {"O1", "O2", "O3", "Ofast", "Os"};
    int ScopedOptimization = static_cast<int>(ScopedValues.build.optimization);
    if (ImGui::Combo("Optimization", &ScopedOptimization, ScopedOptimizationNames, Build::kOptimizationLevelCount))
    {
        ScopedValues.build.optimization = static_cast<Build::OptimizationLevel>(ScopedOptimization);
    }
    ImGui::Checkbox("Link-time optimization (LTO)", &ScopedValues.build.linkTimeOptimization);
    ImGui::Checkbox("Strip debug symbols", &ScopedValues.build.stripDebugInfo);
    ImGui::Checkbox("Unity build", &ScopedValues.build.unityBuild);
    ImGui::Checkbox("Native CPU architecture", &ScopedValues.build.nativeArch);

    const Build::OptimizationAdvice ScopedAdvice =
        Build::Recommend(ScopedValues.build.optimization, true,
                         ScopedValues.textureFormat == Assets::TextureFormat::Astc4x4Srgb ||
                             ScopedValues.textureFormat == Assets::TextureFormat::Astc6x6Srgb);
    ImGui::TextColored(ImVec4(0.91f, 0.36f, 0.62f, 1.0f), "%s", ScopedAdvice.headline.data());
    ImGui::TextWrapped("%.*s", static_cast<int>(ScopedAdvice.detail.size()), ScopedAdvice.detail.data());
    if (!ScopedAdvice.warning.empty())
    {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.31f, 1.0f), "Engine note: %.*s",
                           static_cast<int>(ScopedAdvice.warning.size()), ScopedAdvice.warning.data());
    }
#if defined(_MSC_VER)
    ImGui::TextDisabled("Resolved flags: %s  %s  %s",
                        Build::OptimizationFlagMsvc(ScopedValues.build.optimization).data(),
                        Build::LinkTimeOptimizationFlag(true).data(), Build::StripDebugFlag(true).data());
#else
    ImGui::TextDisabled("Resolved flags: %s  %s  %s", Build::OptimizationFlag(ScopedValues.build.optimization).data(),
                        Build::LinkTimeOptimizationFlag(false).data(), Build::StripDebugFlag(false).data());
#endif
    ImGui::TextWrapped("%.*s", static_cast<int>(Build::LtoAdvice(true).size()), Build::LtoAdvice(true).data());
    if (ScopedValues.build.nativeArch)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.31f, 1.0f),
                           "Native architecture is for local benchmarking only: the archive may trap on an older CPU.");
    }

    // Collision layers. The matrix is part of ProjectSettings, so it is
    ImGui::SeparatorText("Collision Layers");
    CollisionFilterMatrix& ScopedMatrix = ScopedValues.collision;
    if (ImGui::Button("All layers"))
    {
        ScopedMatrix = CollisionFilterMatrix{};
        for (std::uint32_t ScopedA = 0; ScopedA < kNamedCollisionLayers; ++ScopedA)
        {
            for (std::uint32_t ScopedB = 0; ScopedB < kNamedCollisionLayers; ++ScopedB)
            {
                ScopedMatrix.Set(ScopedA, ScopedB, true);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("No collisions"))
    {
        ScopedMatrix = CollisionFilterMatrix{};
    }
    ImGui::SameLine();
    if (ImGui::Button("Defaults"))
    {
        ScopedMatrix = CollisionFilterMatrix::Defaults();
    }
    if (ImGui::BeginTable("CollisionFilterMatrix", static_cast<int>(kNamedCollisionLayers) + 1,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Layer");
        for (std::uint32_t ScopedColumn = 0; ScopedColumn < kNamedCollisionLayers; ++ScopedColumn)
        {
            ImGui::TableSetupColumn(CollisionLayerName(static_cast<CollisionLayer>(ScopedColumn)));
        }
        ImGui::TableHeadersRow();
        for (std::uint32_t ScopedRow = 0; ScopedRow < kNamedCollisionLayers; ++ScopedRow)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(CollisionLayerName(static_cast<CollisionLayer>(ScopedRow)));
            for (std::uint32_t ScopedColumn = 0; ScopedColumn < kNamedCollisionLayers; ++ScopedColumn)
            {
                ImGui::TableNextColumn();
                bool ScopedEnabled = ScopedMatrix.Get(ScopedRow, ScopedColumn);
                ImGui::PushID(static_cast<int>(ScopedRow * kCollisionLayerCount + ScopedColumn));
                if (ImGui::Checkbox("##collides", &ScopedEnabled))
                {
                    ScopedMatrix.Set(ScopedRow, ScopedColumn, ScopedEnabled);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextWrapped("Masks are committed to live b2Shape filters after mouse release. The first six rows are named; "
                       "the remaining 58 are reserved for game layers.");

    ImGui::SeparatorText("Virtual Texture");
    VirtualTextureSettings& ScopedVt = ScopedValues.virtualTexture;
    ImGui::Checkbox("Enable virtual texturing (opt-in)", &ScopedVt.enabled);
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Opt-in: creates VT resources only for projects with streamed textures.\n"
                      "Ordinary sprites keep sampling the atlas.");
    }
    ImGui::TextDisabled("Structural layout is baked into chunks: it applies when VT starts.");
    static constexpr std::uint32_t kLayerChoices[] = {1, 2, 4, 8};
    static constexpr std::uint32_t kPageChoices[] = {128, 256, 512, 1024, 2048, 4096};
    static constexpr std::uint32_t kTileChoices[] = {16, 32, 64, 128};
    static constexpr std::uint32_t kChunkChoices[] = {1, 2, 4, 8};
    static constexpr std::uint32_t kTableChoices[] = {16, 32, 64, 128, 256, 512, 1024};
    int ScopedLayers = static_cast<int>(ScopedVt.layers);
    if (ImGui::Combo("Layers", &ScopedLayers, "1\0 2\0 4\0 8\0"))
    {
        ScopedVt.layers = kLayerChoices[std::clamp(ScopedLayers, 0, 3)];
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Cache depth, not detail: 1-2 layers is enough for most 2D games.\n"
                      "Each layer costs page_size^2 * 4 bytes (4 MiB at 1024).");
    }
    int ScopedPage = static_cast<int>(ScopedVt.pageSize);
    if (ImGui::Combo("Page size", &ScopedPage, "128\0 256\0 512\0 1024\0 2048\0 4096\0"))
    {
        ScopedVt.pageSize = kPageChoices[std::clamp(ScopedPage, 0, 5)];
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Page resolution: 1024-2048 for production, 128 only for tests.\n"
                      "Memory = page^2 * 4 bytes per layer.");
    }
    int ScopedTile = static_cast<int>(ScopedVt.tileSize);
    if (ImGui::Combo("Tile size", &ScopedTile, "16\0 32\0 64\0 128\0"))
    {
        ScopedVt.tileSize = kTileChoices[std::clamp(ScopedTile, 0, 3)];
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Page quantization: 64 is the sweet spot, 32-64 keeps zoom crisp.\n"
                      "Must divide the page size.");
    }
    int ScopedChunk = static_cast<int>(ScopedVt.chunkTiles);
    if (ImGui::Combo("Chunk tiles", &ScopedChunk, "1\0 2\0 4\0 8\0"))
    {
        ScopedVt.chunkTiles = kChunkChoices[std::clamp(ScopedChunk, 0, 3)];
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Tiles per cooked chunk file: 2-4 balances file count and I/O.\n"
                      "Baked into chunk data, so changing it requires a rebake.");
    }
    int ScopedTable = static_cast<int>(ScopedVt.pageTableSize);
    if (ImGui::Combo("Page table", &ScopedTable, "16\0 32\0 64\0 128\0 256\0 512\0 1024\0"))
    {
        ScopedVt.pageTableSize = kTableChoices[std::clamp(ScopedTable, 0, 6)];
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Addressable window: 128-256 is cheap (64-256 KiB).\n"
                      "For bigger worlds shift the table origin, do not grow the table.");
    }
    ImGui::DragFloat("World units / tile", &ScopedVt.worldTileSize, 1.0f, 1.0f, 4096.0f, "%.0f");
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Texel density: smaller streams more pages and looks sharper,\n"
                      "larger streams fewer pages and looks blurrier.");
    }

    ImGui::SeparatorText("Virtual Texture (runtime)");
    int ScopedBudget = static_cast<int>(ScopedVt.budgetLoadsPerFrame);
    if (ImGui::SliderInt("Loads / frame", &ScopedBudget, 1, 32))
    {
        ScopedVt.budgetLoadsPerFrame = static_cast<std::uint32_t>(ScopedBudget);
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Max page uploads per frame: 4-8 is steady.\n"
                      "Raise for fast cameras, lower for tight frame budgets.");
    }
    int ScopedPrefetch = static_cast<int>(ScopedVt.prefetchTiles);
    if (ImGui::SliderInt("Prefetch ring", &ScopedPrefetch, 0, 8))
    {
        ScopedVt.prefetchTiles = static_cast<std::uint32_t>(ScopedPrefetch);
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Extra rings around the camera: 1 for panning, 2 for fast travel.\n"
                      "Each ring costs additional uploads.");
    }
    ImGui::Checkbox("Streaming enabled", &ScopedVt.streamingEnabled);
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Off freezes current residency for debugging: resident pages stay\n"
                      "usable, nothing is evicted or uploaded.");
    }
    int ScopedDebug = static_cast<int>(ScopedVt.debugMode);
    if (ImGui::Combo("Debug view", &ScopedDebug, "Off\0 Page id tint\0 Slot tint\0"))
    {
        ScopedVt.debugMode = static_cast<std::uint32_t>(std::clamp(ScopedDebug, 0, 2));
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Tints resident pages by page id or slot; magenta marks a tile\n"
                      "that has no resident page.");
    }

    ImGui::SeparatorText("Layout readout");
    ImGui::Text("slots / layer %u   page %.2f MiB   total %.2f MiB", ScopedVt.SlotsPerLayer(),
                static_cast<double>(ScopedVt.BytesPerLayer()) / (1024.0 * 1024.0),
                static_cast<double>(ScopedVt.TotalBytes()) / (1024.0 * 1024.0));
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Derived cost: slots = (page/tile)^2, page bytes = page^2 * 4,\n"
                      "total = layers * page bytes.");
    }
    ImGui::Text("page table %.1f KiB   world span %.0f units", static_cast<double>(ScopedVt.PageTableBytes()) / 1024.0,
                static_cast<double>(ScopedVt.WorldSpan()));
    if (const char* ScopedIssue = ScopedVt.Issue(); ScopedIssue != nullptr)
    {
        ImGui::TextColored(ImVec4(0.95f, 0.36f, 0.31f, 1.0f), "Issue: %s", ScopedIssue);
    }
    else
    {
        ImGui::TextColored(ImVec4(0.42f, 0.85f, 0.55f, 1.0f), "Layout valid");
    }

    if (ScopedTrackable)
    {
        BeginOrRecord(InContext, ScopedBefore);
        CommitIfFinished(InContext);
    }

    if (MDirty)
    {
        ImGui::Separator();
        ImGui::TextDisabled("Uncommitted settings transaction - release the slider to commit.");
    }
    EndEditorPanel();
}

void ProjectSettingsPanel::ApplyOledShift(std::span<Ui::WidgetCanvas> InCanvases, bool InEnabled) noexcept
{
    for (Ui::WidgetCanvas& ScopedCanvas : InCanvases)
    {
        ScopedCanvas.SetOledShiftEnabled(InEnabled);
    }
}

void ProjectSettingsPanel::Apply(Ecs::SystemContext& InContext)
{
    if (MStore == nullptr)
    {
        return;
    }
    const ProjectSettings& ScopedValues = Values();
    if (InContext.audio != nullptr)
    {
        InContext.audio->SetBusVolume(AudioBus::Master, ScopedValues.masterVolume);
        InContext.audio->SetBusVolume(AudioBus::Sfx, ScopedValues.sfxVolume);
        InContext.audio->SetBusVolume(AudioBus::Music, ScopedValues.musicVolume);
        InContext.audio->SetMasterMuted(
            AccessibilityHasFlag(ScopedValues.accessibility.runtime.flags, AccessibilityDeafMode));
    }
    if (InContext.accessibility != nullptr)
    {
        // The single apply entry point: the kernel takes the whole profile, so the
        // frame policy, the guard and the OLED/motion sections stay consistent.
        InContext.accessibility->ApplyProfile(ScopedValues.accessibility);
    }
    if (InContext.physics != nullptr)
    {
        InContext.physics->ApplyCollisionFilterMatrix(InContext.registry, ScopedValues.collision);
    }
    if (InContext.gameplayTags != nullptr)
    {
        ScopedValues.gameplayTags.ApplyToRegistry(*InContext.gameplayTags);
    }
    MApplied = ScopedValues;
    MAppliedValid = true;
}

void ProjectSettingsPanel::SyncFromHistory(Ecs::SystemContext& InContext)
{
    if (MStore == nullptr)
    {
        return;
    }
    if (!MAppliedValid || MApplied != Values())
    {
        Apply(InContext);
        if (!MTransaction.Active())
        {
            Persist(*MStore);
        }
    }
}

// EditorSettingsPanel
void EditorSettingsPanel::Initialize(CommandStack& InCommands, ProjectConfigStore& InStore,
                                     std::uint16_t InCodec) noexcept
{
    MCommands = &InCommands;
    MStore = &InStore;
    MCodec = InCodec;
    MAppliedValid = false;
    MDirty = false;
}

EditorSettings& EditorSettingsPanel::Values() noexcept
{
    return MStore->Editor();
}

const EditorSettings& EditorSettingsPanel::Values() const noexcept
{
    return MStore->Editor();
}

bool EditorSettingsPanel::WatchAssetsEnabled() const noexcept
{
    return MStore != nullptr && MStore->Editor().watchAssets;
}

void EditorSettingsPanel::BeginOrRecord(Ecs::SystemContext& InContext,
                                        const std::array<std::byte, sizeof(EditorSettings)>& InBefore) noexcept
{
    (void)InContext;
    if (MCommands == nullptr || MCodec == 0xFFFFu)
    {
        return;
    }
    std::array<std::byte, sizeof(EditorSettings)> ScopedCurrent{};
    if (MCommands->CaptureComponent(entt::null, MCodec, ScopedCurrent) != CommandStack::Result::Ok)
    {
        return;
    }
    // Padding bytes are copied by CommandStack but must never decide whether an
    EditorSettings ScopedBeforeTyped{};
    EditorSettings ScopedCurrentTyped{};
    std::memcpy(&ScopedBeforeTyped, InBefore.data(), sizeof(EditorSettings));
    std::memcpy(&ScopedCurrentTyped, ScopedCurrent.data(), sizeof(EditorSettings));
    if (ScopedBeforeTyped == ScopedCurrentTyped)
    {
        return;
    }
    if (!MTransaction.Active())
    {
        (void)MTransaction.BeginWithSnapshot(*MCommands, entt::null, MCodec, "Editor settings",
                                             InteractionTransaction::Source::Settings,
                                             std::span<const std::byte>(InBefore.data(), InBefore.size()));
    }
    if (MTransaction.Active())
    {
        (void)MTransaction.Record();
        MDirty = true;
    }
}

void EditorSettingsPanel::CommitIfFinished(Ecs::SystemContext& InContext)
{
    if (!MTransaction.Active() || ImGui::IsAnyItemActive())
    {
        return;
    }
    const CommandStack::Result ScopedResult = MTransaction.Commit();
    if (ScopedResult != CommandStack::Result::Ok)
    {
        LogWarn("settings", "editor settings commit failed: {}", CommandStack::ResultName(ScopedResult));
        return;
    }
    MDirty = false;
    Apply(InContext);
    if (MStore != nullptr)
    {
        Persist(*MStore);
    }
}

void EditorSettingsPanel::Draw(Ecs::SystemContext& InContext, bool* InOpen)
{
    if (MStore == nullptr)
    {
        return;
    }
    if (!BeginEditorPanel("Editor Settings", InOpen))
    {
        EndEditorPanel();
        return;
    }

    EditorSettings& ScopedValues = Values();
    std::array<std::byte, sizeof(EditorSettings)> ScopedBefore{};
    const bool ScopedTrackable =
        MCommands != nullptr && MCodec != 0xFFFFu &&
        MCommands->CaptureComponent(entt::null, MCodec, ScopedBefore) == CommandStack::Result::Ok;

    ImGui::TextDisabled("Environment settings. Persisted to project.femproj and editor.ini.");
    ImGui::Separator();
    if (MStore->Paths().IsValid())
    {
        ImGui::Text("project  %s", MStore->Paths().root.string().c_str());
        ImGui::Text("ini      %s", MStore->Paths().editorIni.string().c_str());
    }
    ImGui::SeparatorText("Display");
    ImGui::Checkbox("VSync", &ScopedValues.vsync);
    ImGui::SliderFloat("UI scale", &ScopedValues.uiScale, 0.75f, 2.0f, "%.2f");
    ImGui::Checkbox("Show grid", &ScopedValues.showGrid);
    const char* ScopedWindowModeNames[] = {"Windowed", "Borderless fullscreen", "Exclusive fullscreen"};
    int ScopedWindowMode = static_cast<int>(std::min<std::uint8_t>(ScopedValues.windowMode, 2));
    if (ImGui::Combo("Window mode", &ScopedWindowMode, ScopedWindowModeNames, 3))
    {
        ScopedValues.windowMode = static_cast<std::uint8_t>(ScopedWindowMode);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(borderless fullscreen is the default)");
    const std::span<const Rhi::ViewportModeDescriptor> ScopedViewModes = Rhi::AllViewportModes();
    const std::uint32_t ScopedStoredMode = ClampViewportViewMode(ScopedValues.viewportViewMode);
    const Rhi::ViewportModeDescriptor& ScopedActiveMode =
        Rhi::DescribeViewportMode(static_cast<Rhi::ViewportMode>(ScopedStoredMode));
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Viewport mode", ScopedActiveMode.Label))
    {
        for (const Rhi::ViewportModeDescriptor& ScopedDescriptor : ScopedViewModes)
        {
            const bool ScopedSelected = ScopedDescriptor.Mode == ScopedActiveMode.Mode;
            if (ImGui::Selectable(ScopedDescriptor.Label, ScopedSelected))
            {
                ScopedValues.viewportViewMode = static_cast<std::uint32_t>(ScopedDescriptor.Mode);
            }
            if (ScopedSelected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("%s", ScopedActiveMode.Summary);
    }
    int ScopedComplexityBudget = static_cast<int>(ScopedValues.viewportComplexityBudget);
    if (ImGui::SliderInt("Complexity budget", &ScopedComplexityBudget, 1, 64))
    {
        ScopedValues.viewportComplexityBudget = static_cast<std::uint32_t>(std::max(ScopedComplexityBudget, 1));
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Normalization reference of the complexity heat ramp.\n"
                      "A sprite whose overdraw times light count reaches the budget is fully red.");
    }

    ImGui::SeparatorText("Typography (16 px readable floor)");
    ImGui::SliderFloat("Panel titles", &ScopedValues.fontPanelTitle, 16.0f, 36.0f, "%.0f px");
    ImGui::SliderFloat("Panel text", &ScopedValues.fontPanelBody, 16.0f, 30.0f, "%.0f px");
    ImGui::SliderFloat("Tooltips", &ScopedValues.fontTooltip, 16.0f, 30.0f, "%.0f px");
    ImGui::SliderFloat("Script node titles", &ScopedValues.fontScriptNodeTitle, 16.0f, 32.0f, "%.0f px");
    ImGui::SliderFloat("Script input pins", &ScopedValues.fontScriptPinInput, 16.0f, 28.0f, "%.0f px");
    ImGui::SliderFloat("Script output pins", &ScopedValues.fontScriptPinOutput, 16.0f, 28.0f, "%.0f px");
    ImGui::SliderFloat("Scene text", &ScopedValues.fontSceneText, 16.0f, 40.0f, "%.0f px");
    ImGui::TextDisabled("Font sizes are logical px; the window content scale multiplies them. The atlas reloads on "
                        "the next frame.");

    ImGui::SeparatorText("Logs");
    int ScopedLogFiles = static_cast<int>(std::min<std::uint32_t>(ScopedValues.logMaxFiles, 512u));
    if (ImGui::SliderInt("Files per category", &ScopedLogFiles, 1, 512))
    {
        ScopedValues.logMaxFiles = static_cast<std::uint32_t>(std::max(ScopedLogFiles, 1));
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("The newest files of each log category are kept. Older files are deleted when the sink opens "
                      "or when this setting is applied.");
    }
    int ScopedLogLines = static_cast<int>(std::min<std::uint32_t>(ScopedValues.logMaxLinesPerFile, 2000000u));
    if (ImGui::InputInt("Lines per file", &ScopedLogLines, 10000, 50000))
    {
        ScopedValues.logMaxLinesPerFile = static_cast<std::uint32_t>(std::clamp(ScopedLogLines, 1000, 2000000));
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("A file that reaches this many lines is compacted in place: the oldest lines are dropped and "
                      "the newest ninety percent stay.");
    }
    if (ImGui::Button("Use recommended limits"))
    {
        ScopedValues.logMaxFiles = kRecommendedLogRetention.MaxFiles;
        ScopedValues.logMaxLinesPerFile = kRecommendedLogRetention.MaxLinesPerFile;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Recommended: 20 files x 50,000 lines (about 6 MB per file).");

    ImGui::SeparatorText("Authoring");
    ImGui::Checkbox("Watch assets (OS watcher + hot reload)", &ScopedValues.watchAssets);
    ImGui::Checkbox("Auto refresh Content Browser", &ScopedValues.autoRefreshBrowser);
    ImGui::Checkbox("Save editor.ini on commit", &ScopedValues.saveEditorIniOnCommit);
    ImGui::Checkbox("ImGui demo window", &ScopedValues.showDemoWindow);

    // IDE accessibility. These switches apply the engine's accessibility
    ImGui::SeparatorText("Accessibility (editor window)");
    AccessibilityEditorSettings& ScopedIde = MStore->Project().accessibility.editor;
    ImGui::Checkbox("Blind mode: keyboard navigation, always-visible focus, LogBoy narration sink",
                    &ScopedIde.blindMode);
    ImGui::Checkbox("Deaf mode: visual status cues only (no editor audio alerts)", &ScopedIde.deafMode);
    ImGui::Checkbox("High contrast (WCAG 2.2, 4.5:1 minimum)", &ScopedIde.highContrast);
    ImGui::Checkbox("Reduce motion (no blinking caret)", &ScopedIde.reduceMotion);
    ImGui::Checkbox("FlashGuard on editor background", &ScopedIde.flashGuard);
    ImGui::BeginDisabled(!ScopedIde.flashGuard);
    ImGui::SliderFloat("Max luminance change / frame", &ScopedIde.flashThreshold, 0.02f, 0.45f, "%.3f");
    ImGui::EndDisabled();

    const char* ScopedColorVisionNames[] = {"None", "Protanopia", "Deuteranopia", "Tritanopia"};
    int ScopedColorVision = static_cast<int>(std::min<std::uint8_t>(ScopedIde.colorVision, 3));
    if (ImGui::Combo("Colour vision correction", &ScopedColorVision, ScopedColorVisionNames, 4))
    {
        ScopedIde.colorVision = static_cast<std::uint8_t>(ScopedColorVision);
    }
    ImGui::BeginDisabled(ScopedIde.colorVision == 0);
    ImGui::SliderFloat("Correction strength", &ScopedIde.colorVisionStrength, 0.0f, 1.0f, "%.2f");
    ImGui::EndDisabled();
    ImGui::TextDisabled("Machado 2009 matrices are shared with the game display-safety shader.");
    if (ScopedIde.blindMode)
    {
        ImGui::TextWrapped("LogBoy records installed by this mode are fixed-size PODs; a screen reader can drain "
                           "them from the LogBoy panel's ring without blocking the frame.");
    }

    if (ScopedTrackable)
    {
        BeginOrRecord(InContext, ScopedBefore);
        CommitIfFinished(InContext);
    }
    if (MDirty)
    {
        ImGui::Separator();
        ImGui::TextDisabled("Uncommitted settings transaction - release the slider to commit.");
    }
    EndEditorPanel();
}

void EditorSettingsPanel::Apply(Ecs::SystemContext& InContext)
{
    if (MStore == nullptr)
    {
        return;
    }
    const EditorSettings& ScopedValues = Values();
    if (InContext.renderer != nullptr)
    {
        if (InContext.renderer->VSyncEnabled() != ScopedValues.vsync)
        {
            if (!InContext.renderer->SetVSync(ScopedValues.vsync))
            {
                LogWarn("settings", "the RHI backend cannot change VSync at runtime; stored for next launch");
            }
        }
    }
    if (InContext.window != nullptr)
    {
        InContext.window->SetVsyncHint(ScopedValues.vsync);
        const auto ScopedMode = static_cast<WindowMode>(std::min<std::uint8_t>(ScopedValues.windowMode, 2));
        if (InContext.window->Mode() != ScopedMode && !InContext.window->SetWindowMode(ScopedMode))
        {
            LogWarn("settings", "could not switch to {} at runtime; stored for next launch",
                    WindowModeName(ScopedMode));
        }
    }
    // The font set owns the global font scale: EditorSystem reloads the atlas
    // from the new EditorSettings on the next frame, so writing FontScaleMain
    // here would race that path and break the legacy density fallback.
    LogRetention ScopedRetention;
    ScopedRetention.MaxFiles = ScopedValues.logMaxFiles;
    ScopedRetention.MaxLinesPerFile = ScopedValues.logMaxLinesPerFile;
    SetLogRetention(ScopedRetention);
    ApplyIdeAccessibility(MStore->Project().accessibility.editor);
    MApplied = ScopedValues;
    MAppliedValid = true;
}

void EditorSettingsPanel::SyncFromHistory(Ecs::SystemContext& InContext)
{
    if (MStore == nullptr)
    {
        return;
    }
    if (!MAppliedValid || MApplied != Values())
    {
        Apply(InContext);
        if (!MTransaction.Active())
        {
            Persist(*MStore);
        }
    }
}
}
