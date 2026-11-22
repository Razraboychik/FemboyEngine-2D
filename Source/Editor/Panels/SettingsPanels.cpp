// FemboyEngine2D - transactional Settings panels.
#include "Editor/Panels/SettingsPanels.hpp"

#include "Editor/Infrastructure/EditorTheme.hpp"
#include "Editor/Infrastructure/EditorTypography.hpp"
#include "Editor/Infrastructure/TextureThumbnails.hpp"
#include "Engine/UI/Accessibility.hpp"
#include "Engine/Audio/Audio.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/ECS/Schedule.hpp"
#include "Editor/Infrastructure/CommandStack.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/RHI.hpp"
#include "Engine/RHI/DlssRuntimeProbe.hpp"
#include "Engine/RHI/Dx/DxProbe.hpp"
#include "Engine/RHI/ViewportModes.hpp"
#include "Engine/Physics/Physics.hpp"
#include "Engine/Platform/SystemInfo.hpp"
#include "Engine/UI/PinkUI.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <vector>

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
    if (InSource == nullptr)
    {
        InDestination[0] = 0;
        return;
    }
    std::size_t ScopedLength = 0;
    while (ScopedLength + 1 < InCapacity && InSource[ScopedLength] != 0)
    {
        ++ScopedLength;
    }
    std::memcpy(InDestination, InSource, ScopedLength);
    InDestination[ScopedLength] = 0;
}

void Persist(ProjectConfigStore& InStore)
{
    std::string ScopedError;
    if (!InStore.PersistIfChanged(&ScopedError))
    {
        LogWarn("settings", "cannot persist project config: {}", ScopedError);
    }
}

// Installed-voice cache shared by the game and the editor panels. Enumeration
// costs a COM walk on Windows, so it runs once and on an explicit refresh.
struct VoiceListCache
{
    std::array<TtsVoiceInfo, kMaxTtsVoices> Voices{};
    std::array<std::array<char, 128>, kMaxTtsVoices> Labels{};
    std::uint32_t Count = 0;
    bool Loaded = false;
};

VoiceListCache& VoiceCache()
{
    static VoiceListCache ScopedCache;
    return ScopedCache;
}

void RefreshVoiceCache()
{
    VoiceListCache& ScopedCache = VoiceCache();
    ScopedCache.Count = EnumerateNativeTtsVoices(ScopedCache.Voices.data(), kMaxTtsVoices);
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCache.Count; ++ScopedIndex)
    {
        const TtsVoiceInfo& ScopedVoice = ScopedCache.Voices[ScopedIndex];
        std::snprintf(ScopedCache.Labels[ScopedIndex].data(), ScopedCache.Labels[ScopedIndex].size(), "%s (%s)",
                      ScopedVoice.Name[0] != '\0' ? ScopedVoice.Name : "voice",
                      ScopedVoice.Language[0] != '\0' ? ScopedVoice.Language : "unknown");
    }
    ScopedCache.Loaded = true;
}

// "Auto (by language)" plus every installed voice. The id is stored verbatim;
// an empty id means the language policy owns the choice.
void DrawVoiceCombo(const char* InLabel, char* VoiceIdBuffer, std::size_t InCapacity)
{
    VoiceListCache& ScopedCache = VoiceCache();
    if (!ScopedCache.Loaded)
    {
        RefreshVoiceCache();
    }
    const char* ScopedPreview = "Auto (by language)";
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCache.Count; ++ScopedIndex)
    {
        if (std::strcmp(ScopedCache.Voices[ScopedIndex].Id, VoiceIdBuffer) == 0)
        {
            ScopedPreview = ScopedCache.Labels[ScopedIndex].data();
            break;
        }
    }
    if (!ImGui::BeginCombo(InLabel, ScopedPreview))
    {
        return;
    }
    if (ImGui::Selectable("Auto (by language)", VoiceIdBuffer[0] == '\0'))
    {
        VoiceIdBuffer[0] = '\0';
    }
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < ScopedCache.Count; ++ScopedIndex)
    {
        const bool ScopedSelected = std::strcmp(ScopedCache.Voices[ScopedIndex].Id, VoiceIdBuffer) == 0;
        if (ImGui::Selectable(ScopedCache.Labels[ScopedIndex].data(), ScopedSelected))
        {
            CopyProjectField(VoiceIdBuffer, InCapacity, ScopedCache.Voices[ScopedIndex].Id);
        }
    }
    ImGui::Separator();
    if (ImGui::Selectable("Refresh installed voices"))
    {
        RefreshVoiceCache();
    }
    ImGui::EndCombo();
    if (ScopedCache.Count == 0)
    {
        ImGui::TextDisabled("No installed voice was reported; the language tag selects the voice.");
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

bool EditSymbolColor(const char* InLabel, std::uint32_t* InColor)
{
    float ScopedRgb[3] = {static_cast<float>((*InColor >> 16u) & 0xFFu) / 255.0f,
                          static_cast<float>((*InColor >> 8u) & 0xFFu) / 255.0f,
                          static_cast<float>(*InColor & 0xFFu) / 255.0f};
    if (!ImGui::ColorEdit3(InLabel, ScopedRgb, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueBar))
    {
        return false;
    }
    const auto ScopedByte = [](float InValue)
    {
        const float Scoped = std::isfinite(InValue) ? std::clamp(InValue, 0.0f, 1.0f) : 0.0f;
        return static_cast<std::uint32_t>(std::lround(Scoped * 255.0f));
    };
    *InColor = (ScopedByte(ScopedRgb[0]) << 16u) | (ScopedByte(ScopedRgb[1]) << 8u) | ScopedByte(ScopedRgb[2]);
    return true;
}

void SeedSymbolColors(EditorSettings& InSettings, const EditorPalette& InPalette)
{
    InSettings.symbolColors.AxisX = InPalette.AxisX;
    InSettings.symbolColors.AxisY = InPalette.AxisY;
    InSettings.symbolColors.Grid = InPalette.Grid;
    InSettings.symbolColors.Play = InPalette.Play;
    InSettings.symbolColors.Pause = InPalette.Pause;
    InSettings.symbolColors.Step = InPalette.Step;
    InSettings.symbolColors.Stop = InPalette.Stop;
    InSettings.symbolColors.Rotate = InPalette.Rotate;
    InSettings.symbolColors.Multiplayer = InPalette.Multiplayer;
    for (std::size_t ScopedI = 0; ScopedI < InSettings.symbolColors.Plot.size(); ++ScopedI)
    {
        InSettings.symbolColors.Plot[ScopedI] = InPalette.Plot[ScopedI];
    }
}

// IDE accessibility
struct IdePalette
{
    std::array<ImVec4, ImGuiCol_COUNT> base{};
    FlashGuard flash{};
    bool captured = false;
    std::uint64_t generation = 0;
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
        const float ScopedClamped = std::isfinite(ScopedSrgb) ? std::clamp(ScopedSrgb, 0.0f, 1.0f) : 0.0f;
        return static_cast<std::uint8_t>(std::lround(ScopedClamped * 255.0f));
    };
    return Color32::FromBytes(ScopedByte(InLinear.x), ScopedByte(InLinear.y), ScopedByte(InLinear.z),
                              static_cast<std::uint8_t>(std::lround(
                                  (std::isfinite(InLinear.w) ? std::clamp(InLinear.w, 0.0f, 1.0f) : 0.0f) * 255.0f)));
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
    const std::uint64_t ScopedGeneration = EditorThemeGeneration();
    if (!ScopedPalette.captured || ScopedPalette.generation != ScopedGeneration)
    {
        for (int ScopedIndex = 0; ScopedIndex < ImGuiCol_COUNT; ++ScopedIndex)
        {
            ScopedPalette.base[static_cast<std::size_t>(ScopedIndex)] =
                ScopedStyle.Colors[static_cast<std::size_t>(ScopedIndex)];
        }
        ScopedPalette.captured = true;
        ScopedPalette.generation = ScopedGeneration;
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
        // BlindMode for the window: keyboard navigation stays on, focus is drawn.
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        ScopedStyle.Colors[ImGuiCol_NavCursor] = HexColor(kThemeAccentActive, 1.0f);
        ScopedStyle.Colors[ImGuiCol_Header] = HexColor(kThemeHeaderActive, 1.0f);
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

    // Reduce motion: no blinking text caret.
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

void ProjectSettingsPanel::InitializeAudioSettings(Ecs::SystemContext& InContext)
{
    if (MAudioSettingsReady || MStore == nullptr)
    {
        return;
    }
    const ProjectSettings& ScopedProject = MStore->Project();
    const std::uint8_t ScopedBusCount =
        std::min<std::uint8_t>(ScopedProject.audioBusCount, kMaxProjectAudioBuses);
    if (ScopedBusCount > 0 && InContext.audio != nullptr)
    {
        std::array<Fby::Audio::AudioBusDefinition, kMaxProjectAudioBuses> ScopedDefinitions{};
        for (std::uint8_t ScopedIndex = 0; ScopedIndex < ScopedBusCount; ++ScopedIndex)
        {
            const ProjectAudioBus& ScopedBus = ScopedProject.audioBuses[ScopedIndex];
            ScopedDefinitions[ScopedIndex] = {std::string_view(ScopedBus.id), std::string_view(ScopedBus.name),
                                              std::string_view(ScopedBus.parent), ScopedBus.defaultDb};
        }
        if (Fby::Audio::CreateAudioBuses(*InContext.audio,
                                         std::span<const Fby::Audio::AudioBusDefinition>(ScopedDefinitions.data(),
                                                                                         ScopedBusCount),
                                         std::span<Fby::BusId>(MProjectBusIds.data(), ScopedBusCount)))
        {
            Fby::Audio::RegisterAudioBusSettings(
                MAudioSettings,
                std::span<const Fby::Audio::AudioBusDefinition>(ScopedDefinitions.data(), ScopedBusCount),
                std::span<const Fby::BusId>(MProjectBusIds.data(), ScopedBusCount), InContext.audio);
            (void)MAudioSettings.Freeze();
            MAudioPanel.SetRegistry(&MAudioSettings, Fby::Settings::Scope::Project);
            MAudioSettingsReady = true;
            return;
        }
    }

    auto ScopedAdd = [this](const char* InId, const char* InLabel, float ProjectSettings::*InMember)
    {
        MAudioSettings.Float(InId)
            .Category("Mixer")
            .Label(InLabel)
            .Unit("linear")
            .Default(Fby::Settings::Value::MakeFloat(1.0))
            .Range(0.0, 2.0)
            .Scope(Fby::Settings::Scope::Project)
            .Apply(
                [this, InMember](const Fby::Settings::Value& InValue)
                {
                    if (MStore != nullptr)
                    {
                        MStore->Project().*InMember = static_cast<float>(InValue.FloatValue);
                    }
                });
    };
    ScopedAdd("audio.master", "Master", &ProjectSettings::masterVolume);
    ScopedAdd("audio.sfx", "SFX", &ProjectSettings::sfxVolume);
    ScopedAdd("audio.music", "Music", &ProjectSettings::musicVolume);
    (void)MAudioSettings.Freeze();
    (void)MAudioSettings.SetProject("audio.master", Fby::Settings::Value::MakeFloat(ScopedProject.masterVolume));
    (void)MAudioSettings.SetProject("audio.sfx", Fby::Settings::Value::MakeFloat(ScopedProject.sfxVolume));
    (void)MAudioSettings.SetProject("audio.music", Fby::Settings::Value::MakeFloat(ScopedProject.musicVolume));
    MAudioPanel.SetRegistry(&MAudioSettings, Fby::Settings::Scope::Project);
    MAudioSettingsReady = true;
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
    if (MCommands->CaptureComponent(Fby::kNullEntity, MCodec, ScopedCurrent) != CommandStack::Result::Ok)
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
        (void)MTransaction.BeginWithSnapshot(*MCommands, Fby::kNullEntity, MCodec, "Project settings",
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
        // No project store: there is nothing to edit. Do not call End() here.
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
        MCommands->CaptureComponent(Fby::kNullEntity, MCodec, ScopedBefore) == CommandStack::Result::Ok;

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
    ImGui::TextDisabled("Tag ids are stable slots; removing a tag leaves a tombstone and never renumbers later ids.");
    GameplayTagDatabase& ScopedTagDb = ScopedValues.gameplayTags;
    if (MSelectedTag >= static_cast<int>(ScopedTagDb.Count) ||
        (MSelectedTag >= 0 && ScopedTagDb.PathAt(static_cast<std::uint32_t>(MSelectedTag)).empty()))
    {
        MSelectedTag = -1;
        for (std::uint32_t ScopedI = 0; ScopedI < ScopedTagDb.Count; ++ScopedI)
        {
            if (!ScopedTagDb.PathAt(ScopedI).empty())
            {
                MSelectedTag = static_cast<int>(ScopedI);
                break;
            }
        }
    }

    ImGui::InputTextWithHint("##tag_search", "Filter tags", MTagSearch.data(), MTagSearch.size());
    ImGui::SameLine();
    if (ImGui::Button("Clear filter"))
    {
        MTagSearch = {};
    }

    std::array<std::uint16_t, GameplayTagDatabase::kCapacity> ScopedOrder{};
    std::uint32_t ScopedLiveCount = 0;
    for (std::uint32_t ScopedI = 0; ScopedI < ScopedTagDb.Count; ++ScopedI)
    {
        if (!ScopedTagDb.PathAt(ScopedI).empty())
        {
            ScopedOrder[ScopedLiveCount++] = static_cast<std::uint16_t>(ScopedI);
        }
    }
    std::sort(ScopedOrder.begin(), ScopedOrder.begin() + ScopedLiveCount,
              [&ScopedTagDb](std::uint16_t InA, std::uint16_t InB)
              {
                  return ScopedTagDb.PathAt(InA) < ScopedTagDb.PathAt(InB);
              });

    ImGui::BeginChild("##gameplay_tag_list", ImVec2(0.0f, 180.0f), true);
    const std::string_view ScopedFilter(MTagSearch.data());
    for (std::uint32_t ScopedRank = 0; ScopedRank < ScopedLiveCount; ++ScopedRank)
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

    if (MSelectedTag >= 0 && MSelectedTag < static_cast<int>(ScopedTagDb.Count) &&
        !ScopedTagDb.PathAt(static_cast<std::uint32_t>(MSelectedTag)).empty())
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

    ImGui::SeparatorText("Physics");
    constexpr const char* kViewProfileNames[] = {"Default", "Side View", "Top-Down", "Isometric", "Hex Isometric"};
    int ScopedViewProfile = static_cast<int>(ScopedValues.viewProfile);
    if (ImGui::Combo("View profile", &ScopedViewProfile, kViewProfileNames, 5))
    {
        ScopedValues.viewProfile = static_cast<Fby::ViewProfile>(
            std::clamp(ScopedViewProfile, 0, static_cast<int>(Fby::ViewProfile::Hex)));
    }
    ImGui::TextDisabled("Scenes may override the project profile.");
    constexpr const char* kPhysicsBackendNames[] = {"Auto", "Box2D", "DOD"};
    int ScopedPhysicsBackend = static_cast<int>(ScopedValues.physicsBackend);
    if (ImGui::Combo("Backend", &ScopedPhysicsBackend, kPhysicsBackendNames, 3))
    {
        ScopedValues.physicsBackend = static_cast<std::uint8_t>(std::clamp(ScopedPhysicsBackend, 0, 2));
    }
    int ScopedBodyBudget = static_cast<int>(ScopedValues.physicsBodyBudget);
    if (ImGui::InputInt("Body budget", &ScopedBodyBudget, 256, 4096))
    {
        ScopedValues.physicsBodyBudget = static_cast<std::uint32_t>(std::clamp(ScopedBodyBudget, 0, 65536));
    }
    ImGui::TextDisabled("Auto uses DOD when the budget is 4096 or more and the world has no joints.");

    ImGui::SeparatorText("Mixer");
    InitializeAudioSettings(InContext);
    if (MAudioSettingsReady)
    {
        const ProjectSettings& ScopedProjectValues = MStore->Project();
        (void)MAudioSettings.SetProject("audio.master",
                                        Fby::Settings::Value::MakeFloat(ScopedProjectValues.masterVolume));
        (void)MAudioSettings.SetProject("audio.sfx",
                                        Fby::Settings::Value::MakeFloat(ScopedProjectValues.sfxVolume));
        (void)MAudioSettings.SetProject("audio.music",
                                        Fby::Settings::Value::MakeFloat(ScopedProjectValues.musicVolume));
    }
    MAudioPanel.DrawInline();

    // One profile: the same values reach the game frame, the editor window and
    // the OLED policy, so a project and the person editing it cannot disagree.
    AccessibilityProfile& ScopedProfile = ScopedValues.accessibility;
    ImGui::SeparatorText("Accessibility");
    if (!MOutputPolicySettingsReady)
    {
        Fby::Settings::RegisterOutputPolicySettings(MOutputPolicySettings);
        (void)MOutputPolicySettings.Freeze();
        MOutputPolicyPanel.SetRegistry(&MOutputPolicySettings, Fby::Settings::Scope::Project);
        MOutputPolicySettingsReady = true;
    }
    if (!MAccessibilityRegistryReady)
    {
        Fby::Editor::RegisterAccessibilitySettings(MAccessibilityRegistry, MStore->Project().accessibility);
        (void)MAccessibilityRegistry.Freeze();
        MAccessibilityPanel.SetRegistry(&MAccessibilityRegistry, Fby::Settings::Scope::Project);
        MAccessibilityRegistryReady = true;
    }
    if (ImGui::CollapsingHeader("Generated accessibility settings"))
    {
        MAccessibilityPanel.DrawInline();
    }
    if (ImGui::CollapsingHeader("Output policies (safe mode)"))
    {
        MOutputPolicyPanel.DrawInline();
    }
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

    // Spoken output of the shipped game. The editor keeps its own narrator in
    // Editor Settings, so a game's speech policy never changes the editor.
    NarratorSettings& ScopedNarration = ScopedProfile.narration;
    ImGui::SeparatorText("Narrator (game)");
    ImGui::Checkbox("Speak interface and world events", &ScopedNarration.Enabled);
    ImGui::SameLine();
    ImGui::TextDisabled("(no speech when off; captions stay)");
    ImGui::BeginDisabled(!ScopedNarration.Enabled);
    ImGui::Checkbox("Announce focus", &ScopedNarration.AnnounceFocus);
    ImGui::SameLine();
    ImGui::Checkbox("Announce role", &ScopedNarration.AnnounceRole);
    ImGui::SameLine();
    ImGui::Checkbox("Announce state", &ScopedNarration.AnnounceState);
    ImGui::Checkbox("Announce scene on demand", &ScopedNarration.AnnounceScene);
    ImGui::SameLine();
    ImGui::Checkbox("Announce world events", &ScopedNarration.AnnounceWorldEvents);
    ImGui::Checkbox("Focus interrupts speech", &ScopedNarration.InterruptOnFocus);
    const char* ScopedVerbosityNames[] = {"Silent", "Name only", "Name and role", "Name, role and state"};
    int ScopedVerbosity = static_cast<int>(std::min<std::uint8_t>(ScopedNarration.Verbosity, 3));
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("Verbosity", &ScopedVerbosity, ScopedVerbosityNames, 4))
    {
        ScopedNarration.Verbosity = static_cast<std::uint8_t>(std::clamp(ScopedVerbosity, 0, 3));
    }
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Speech rate", &ScopedNarration.Rate, 0.5f, 2.0f, "%.2f");
    int ScopedQueueDepth = static_cast<int>(std::min<std::uint32_t>(ScopedNarration.MaxQueued, 16u));
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderInt("Queued lines", &ScopedQueueDepth, 1, 16))
    {
        ScopedNarration.MaxQueued = static_cast<std::uint32_t>(std::clamp(ScopedQueueDepth, 1, 16));
    }
    char ScopedVoiceLanguage[16]{};
    std::snprintf(ScopedVoiceLanguage, sizeof(ScopedVoiceLanguage), "%s",
                  ScopedNarration.Language);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputText("Voice language (BCP-47)", ScopedVoiceLanguage, sizeof(ScopedVoiceLanguage)))
    {
        std::snprintf(ScopedNarration.Language, sizeof(ScopedNarration.Language), "%s", ScopedVoiceLanguage);
    }
    DrawVoiceCombo("Voice", ScopedNarration.VoiceId, sizeof(ScopedNarration.VoiceId));
    ImGui::TextDisabled("The voice follows the content locale, not the OS locale. A missing voice falls back to "
                        "the system default and is reported in the log.");
    ImGui::TextDisabled("Voice packs: install a pack under Saved/VoicePacks; it appears here as pack:<id>.");
    ImGui::EndDisabled();

    // Gameplay events: wording, categories and spatial grammar. The same
    // profile section the game loads, so the editor previews the shipped policy.
    ActionNarrationSettings& ScopedActions = ScopedProfile.actions;
    ImGui::SeparatorText("Action narration (game)");
    ImGui::Checkbox("Speak gameplay actions", &ScopedActions.Enabled);
    ImGui::BeginDisabled(!ScopedActions.Enabled);
    if (ImGui::TreeNode("Categories"))
    {
        const char* ScopedCategoryNames[] = {"Interaction", "Combat", "Items", "Navigation", "State", "System"};
        for (std::uint32_t ScopedCategory = 0; ScopedCategory < kNarrationCategoryCount; ++ScopedCategory)
        {
            bool ScopedEnabled = (ScopedActions.Categories & (1u << ScopedCategory)) != 0;
            if (ImGui::Checkbox(ScopedCategoryNames[ScopedCategory], &ScopedEnabled))
            {
                AccessibilitySetFlag(&ScopedActions.Categories, 1u << ScopedCategory, ScopedEnabled);
            }
        }
        ImGui::TreePop();
    }
    ImGui::Checkbox("Direction words", &ScopedActions.AnnounceDirection);
    ImGui::SameLine();
    ImGui::Checkbox("Distance words", &ScopedActions.AnnounceDistance);
    int ScopedActionPriority = static_cast<int>(std::min<std::uint8_t>(ScopedActions.MinimumPriority, 3));
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderInt("Lowest spoken priority", &ScopedActionPriority, 0, 3))
    {
        ScopedActions.MinimumPriority = static_cast<std::uint8_t>(std::clamp(ScopedActionPriority, 0, 3));
    }
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Near distance (m)", &ScopedActions.NearDistanceMeters, 0.0f, 30.0f, "%.1f");
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Far distance (m)", &ScopedActions.FarDistanceMeters, 0.0f, 100.0f, "%.1f");
    int ScopedDedup = static_cast<int>(std::min<std::uint16_t>(ScopedActions.DedupWindowMs, 5000));
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderInt("Repeat suppression (ms)", &ScopedDedup, 0, 5000))
    {
        ScopedActions.DedupWindowMs = static_cast<std::uint16_t>(std::clamp(ScopedDedup, 0, 5000));
    }
    ImGui::TextDisabled("Action lines are localized templates; captions can reuse the same text.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Screen reader bridge (game)");
    ImGui::Checkbox("Publish the interface to the OS accessibility tree", &ScopedProfile.bridge.Enabled);
    ImGui::BeginDisabled(!ScopedProfile.bridge.Enabled);
    ImGui::Checkbox("Focus events", &ScopedProfile.bridge.RaiseFocusEvents);
    ImGui::SameLine();
    ImGui::Checkbox("Structure events", &ScopedProfile.bridge.RaiseStructureEvents);
    ImGui::SameLine();
    ImGui::Checkbox("State events", &ScopedProfile.bridge.RaiseStateEvents);
    ImGui::TextDisabled("Windows: UI Automation (Narrator, NVDA, JAWS). The bridge is independent from the "
                        "narrator: either can be on without the other.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Motion comfort (project)");
    ImGui::SliderFloat("Camera shake", &ScopedValues.motionComfort.cameraShakeScale, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Head bob", &ScopedValues.motionComfort.headBobScale, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("FOV comfort", &ScopedValues.motionComfort.fovScale, 0.5f, 1.5f, "%.2f");
    ImGui::SliderFloat("Motion blur", &ScopedValues.motionComfort.motionBlurScale, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Soft vignette", &ScopedValues.motionComfort.vignetteScale, 0.0f, 1.5f, "%.2f");
    ImGui::TextDisabled("Project policy read by the camera and composite pass; no plugin required.");

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
        EditorTooltip("Art-driven: RGBA8 for pixel art, BC7 for painted desktop art, ASTC for compatible GPUs.\n"
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
        Build::Recommend(ScopedValues.build.optimization, true);
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

    // Collision layers. The matrix is part of ProjectSettings.
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
    InitializeAudioSettings(InContext);
    bool ScopedDynamicBuses = false;
    if (ScopedValues.audioBusCount > 0 && MAudioSettingsReady)
    {
        ScopedDynamicBuses =
            MAudioSettings.Find("audio.bus." + std::string(ScopedValues.audioBuses[0].id)) != nullptr;
    }
    if (InContext.audio != nullptr)
    {
        if (ScopedDynamicBuses)
        {
            (void)MAudioSettings.ApplyAll();
        }
        else
        {
            InContext.audio->SetBusVolume(AudioBus::Master, ScopedValues.masterVolume);
            InContext.audio->SetBusVolume(AudioBus::Sfx, ScopedValues.sfxVolume);
            InContext.audio->SetBusVolume(AudioBus::Music, ScopedValues.musicVolume);
        }
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
void EditorSettingsPanel::SetThumbnails(TextureThumbnailCache* InCache) noexcept
{
    MThumbnails = InCache;
}

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
    if (MCommands->CaptureComponent(Fby::kNullEntity, MCodec, ScopedCurrent) != CommandStack::Result::Ok)
    {
        return;
    }
    // Padding bytes are copied by CommandStack but must never decide whether an edit happened.
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
        (void)MTransaction.BeginWithSnapshot(*MCommands, Fby::kNullEntity, MCodec, "Editor settings",
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
        MCommands->CaptureComponent(Fby::kNullEntity, MCodec, ScopedBefore) == CommandStack::Result::Ok;

    ImGui::TextDisabled("Environment settings. Persisted to project.femproj and editor.ini.");
    if (!MGeneratedReady)
    {
        Fby::Editor::RegisterEditorSettings(MGeneratedRegistry, ScopedValues);
        (void)MGeneratedRegistry.Freeze();
        MGeneratedPanel.SetRegistry(&MGeneratedRegistry, Fby::Settings::Scope::Project);
        MGeneratedReady = true;
    }
    if (ImGui::CollapsingHeader("Generated editor settings"))
    {
        MGeneratedPanel.DrawInline();
    }
    ImGui::Separator();
    if (MStore->Paths().IsValid())
    {
        ImGui::Text("project  %s", MStore->Paths().root.string().c_str());
        ImGui::Text("ini      %s", MStore->Paths().editorIni.string().c_str());
    }
    ImGui::SeparatorText("System");
    // The profile walks DXGI and the volume, so it is sampled on demand rather
    // than every frame.
    static Fby::Platform::SystemProfile ScopedSystemProfile;
    static bool ScopedSystemQueried = false;
    if (ImGui::Button("Query hardware"))
    {
        ScopedSystemProfile =
            Fby::Platform::SystemInfo::Query(MStore->Paths().root.string());
        ScopedSystemQueried = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Probe DLSS runtime"))
    {
        Fby::Rhi::DlssRuntimeProbe ScopedDlss = Fby::Rhi::ProbeDlssRuntime(std::filesystem::current_path());
        ImGui::Text("DLSS: present %s, loadable %s%s%s", ScopedDlss.Present ? "yes" : "no",
                    ScopedDlss.Loadable ? "yes" : "no", ScopedDlss.Error.empty() ? "" : " - ",
                    ScopedDlss.Error.c_str());
    }
    ImGui::SameLine();
    static std::vector<Fby::Rhi::DxAdapterInfo> ScopedDxAdapters;
    if (ImGui::Button("Probe D3D adapters"))
    {
        ScopedDxAdapters = Fby::Rhi::ProbeDxAdapters();
    }
    for (const Fby::Rhi::DxAdapterInfo& ScopedAdapter : ScopedDxAdapters)
    {
        ImGui::Text("D3D  %s  VRAM %.1f GiB  D3D12 %u  D3D11 %u", ScopedAdapter.Name,
                    static_cast<double>(ScopedAdapter.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0),
                    static_cast<unsigned>(ScopedAdapter.D3D12FeatureLevel),
                    static_cast<unsigned>(ScopedAdapter.D3D11FeatureLevel));
    }
    if (ScopedSystemQueried)
    {
        constexpr double kGibibyte = 1.0 / (1024.0 * 1024.0 * 1024.0);
        ImGui::Text("CPU  %s (%u logical cores)", ScopedSystemProfile.hardware.cpu.c_str(),
                    ScopedSystemProfile.hardware.logicalCores);
        ImGui::Text("GPU  %s", ScopedSystemProfile.hardware.gpu.c_str());
        ImGui::Text("RAM  %.1f / %.1f GiB", static_cast<double>(ScopedSystemProfile.hardware.memoryAvailableBytes) *
                                                kGibibyte,
                    static_cast<double>(ScopedSystemProfile.hardware.memoryTotalBytes) * kGibibyte);
        ImGui::Text("Disk %.1f / %.1f GiB free", static_cast<double>(ScopedSystemProfile.storage.freeBytes) * kGibibyte,
                    static_cast<double>(ScopedSystemProfile.storage.totalBytes) * kGibibyte);
        for (const Fby::Platform::DisplayInfo& ScopedDisplay : ScopedSystemProfile.displays)
        {
            ImGui::BulletText("%s %ux%u @ %.0f Hz %.2fx%s", ScopedDisplay.name.c_str(), ScopedDisplay.width,
                              ScopedDisplay.height, static_cast<double>(ScopedDisplay.refreshHz),
                              static_cast<double>(ScopedDisplay.scale), ScopedDisplay.primary ? " (primary)" : "");
        }
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

    ImGui::SeparatorText("Appearance: striped lists and cursor");
    ImGui::Checkbox("Stocking list theme", &ScopedValues.stockingTheme);
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Alternates two row colours in every ImGui table. The pair is editable below; "
                      "turning it off restores the normal table colours.");
    }
    const char* ScopedStockingPresets[] = {"Custom", "Dark plum / teal", "Pastel pink / white",
                                           "Rose / white", "Black / white stockings", "Green / black stockings",
                                           "Deuteranopia-safe", "Protanopia-safe", "Tritanopia-safe",
                                           "Astigmatism / low halation", "High contrast"};
    int ScopedStockingPreset = static_cast<int>(std::min<std::uint8_t>(ScopedValues.stockingPreset, 10));
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("Pair preset", &ScopedStockingPreset, ScopedStockingPresets, 11))
    {
        ScopedValues.stockingPreset = static_cast<std::uint8_t>(ScopedStockingPreset);
        if (ScopedStockingPreset > 0)
        {
            struct PresetColors
            {
                std::uint32_t RowA;
                std::uint32_t RowB;
                std::uint32_t Text;
            };
            static constexpr PresetColors kPresets[] = {
                {0x3A2A3F, 0x2F3A35, 0xF2F2F2}, {0xF7C6DA, 0xFFFFFF, 0x1A1A1A},
                {0xE8A5B5, 0xFFFFFF, 0x1A1A1A}, {0x1C1C1C, 0x2E2E2E, 0xF2F2F2},
                {0x1E3A2A, 0x242424, 0xF2F2F2}, {0x1F3A5F, 0x4A3A1F, 0xF2F2F2},
                {0x152A3A, 0x0F3A35, 0xF2F2F2}, {0x3A1F3A, 0x1F3A2A, 0xF2F2F2},
                {0x121212, 0x1C1C1C, 0xE8E8E8}, {0x000000, 0x1A1A1A, 0xFFFFFF}};
            const PresetColors& ScopedPreset = kPresets[ScopedStockingPreset - 1];
            ScopedValues.stockingColorA = ScopedPreset.RowA;
            ScopedValues.stockingColorB = ScopedPreset.RowB;
            ScopedValues.textColor = ScopedPreset.Text;
            ScopedValues.stockingTheme = true;
        }
    }
    if (EditSymbolColor("Row A", &ScopedValues.stockingColorA))
    {
        ScopedValues.stockingPreset = 0;
    }
    ImGui::SameLine();
    if (EditSymbolColor("Row B", &ScopedValues.stockingColorB))
    {
        ScopedValues.stockingPreset = 0;
    }
    const auto Luminance = [](std::uint32_t InRgb)
    {
        const auto Channel = [](std::uint32_t InValue)
        {
            const float Scoped = static_cast<float>(InValue & 0xFFu) / 255.0f;
            return Scoped <= 0.03928f ? Scoped / 12.92f : std::pow((Scoped + 0.055f) / 1.055f, 2.4f);
        };
        return 0.2126f * Channel((InRgb >> 16u) & 0xFFu) + 0.7152f * Channel((InRgb >> 8u) & 0xFFu) +
               0.0722f * Channel(InRgb & 0xFFu);
    };
    const auto Contrast = [Luminance](std::uint32_t InA, std::uint32_t InB)
    {
        const float ScopedA = Luminance(InA);
        const float ScopedB = Luminance(InB);
        return (std::max(ScopedA, ScopedB) + 0.05f) / (std::min(ScopedA, ScopedB) + 0.05f);
    };
    const float ScopedContrastA = Contrast(ScopedValues.textColor, ScopedValues.stockingColorA);
    const float ScopedContrastB = Contrast(ScopedValues.textColor, ScopedValues.stockingColorB);
    const ImVec4 ScopedContrastColor = ScopedContrastA >= 4.5f && ScopedContrastB >= 4.5f
                                           ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f)
                                           : ImVec4(1.0f, 0.65f, 0.25f, 1.0f);
    ImGui::TextColored(ScopedContrastColor, "Text contrast: row A %.2f:1, row B %.2f:1 (WCAG large-text 4.5:1)",
                       static_cast<double>(ScopedContrastA), static_cast<double>(ScopedContrastB));
    ImGui::TextDisabled("Pastel rows need a dark text colour; the contrast line turns green when both pairs pass.");

    ImGui::Checkbox("Themed cursor", &ScopedValues.themedCursor);
    ImGui::SetNextItemWidth(420.0f);
    (void)ImGui::InputText("Cursor BMP", ScopedValues.cursorImage, sizeof(ScopedValues.cursorImage));
    ImGui::SameLine();
    if (ImGui::SmallButton("Use system cursor"))
    {
        ScopedValues.themedCursor = false;
    }
    ImGui::TextDisabled("32-bit BMP with a magenta key; :3, uwu and owo variants are in "
                        "Projects/CursorThemes. Applying happens on commit.");

    ImGui::SeparatorText("Typography (16 px readable floor)");
    ImGui::SliderFloat("Panel titles", &ScopedValues.fontPanelTitle, 16.0f, 36.0f, "%.0f px");
    ImGui::SliderFloat("Panel text", &ScopedValues.fontPanelBody, 16.0f, 30.0f, "%.0f px");
    ImGui::SliderFloat("Tooltips", &ScopedValues.fontTooltip, 16.0f, 30.0f, "%.0f px");
    ImGui::SliderFloat("Script node titles", &ScopedValues.fontScriptNodeTitle, 16.0f, 32.0f, "%.0f px");
    ImGui::SliderFloat("Script input pins", &ScopedValues.fontScriptPinInput, 16.0f, 28.0f, "%.0f px");
    ImGui::SliderFloat("Script output pins", &ScopedValues.fontScriptPinOutput, 16.0f, 28.0f, "%.0f px");
    ImGui::SliderFloat("Scene text", &ScopedValues.fontSceneText, 16.0f, 40.0f, "%.0f px");
    ImGui::SliderFloat("Line spacing", &ScopedValues.textLineSpacing, -4.0f, 24.0f, "%.0f px");
    ImGui::SliderFloat("Letter spacing", &ScopedValues.textLetterSpacing, -2.0f, 12.0f, "%.1f px");
    ImGui::SliderFloat("Word spacing", &ScopedValues.textWordSpacing, -4.0f, 24.0f, "%.1f px");
    ImGui::TextDisabled("Font sizes are logical px; line/letter/word spacing is added per line and glyph. The atlas "
                        "reloads on the next frame when a font size changes.");

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

    ImGui::SeparatorText("Memory and performance");
    const char* ScopedProfileNames[] = {"Custom", "Lite", "Balanced", "High"};
    int ScopedProfile = static_cast<int>(std::min<std::uint8_t>(ScopedValues.performanceProfile, 3));
    if (ImGui::Combo("Performance profile", &ScopedProfile, ScopedProfileNames, 4))
    {
        ScopedValues.performanceProfile = static_cast<std::uint8_t>(ScopedProfile);
        if (ScopedProfile == 1)
        {
            ScopedValues.undoMemoryMB = 2;
            ScopedValues.undoCommandLimit = 128;
            ScopedValues.thumbnailCacheLimit = 64;
            ScopedValues.thumbnailCacheEnabled = true;
            ScopedValues.frameRateLimit = 30;
            ScopedValues.pauseWhenUnfocused = true;
        }
        else if (ScopedProfile == 2)
        {
            ScopedValues.undoMemoryMB = 4;
            ScopedValues.undoCommandLimit = 256;
            ScopedValues.thumbnailCacheLimit = 128;
            ScopedValues.thumbnailCacheEnabled = true;
            ScopedValues.frameRateLimit = 60;
            ScopedValues.pauseWhenUnfocused = true;
        }
        else if (ScopedProfile == 3)
        {
            ScopedValues.undoMemoryMB = 16;
            ScopedValues.undoCommandLimit = 512;
            ScopedValues.thumbnailCacheLimit = 256;
            ScopedValues.thumbnailCacheEnabled = true;
            ScopedValues.frameRateLimit = 0;
            ScopedValues.pauseWhenUnfocused = false;
        }
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Lite caps the editor caches and runs at 30 FPS; Balanced is 60 FPS; High removes the "
                      "frame limit and opens the budgets. Custom keeps the values below.");
    }
    ImGui::Checkbox("Texture thumbnail cache", &ScopedValues.thumbnailCacheEnabled);
    ImGui::BeginDisabled(!ScopedValues.thumbnailCacheEnabled);
    int ScopedThumbnailLimit = static_cast<int>(ScopedValues.thumbnailCacheLimit);
    if (ImGui::SliderInt("Thumbnail entries", &ScopedThumbnailLimit, 0, 256))
    {
        ScopedValues.thumbnailCacheLimit = static_cast<std::uint16_t>(std::clamp(ScopedThumbnailLimit, 0, 256));
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Descriptors kept for the Content Browser previews. Lowering the limit frees the old "
                      "descriptors on Apply; disabling the cache also stops new previews.");
    }
    int ScopedUndoMemory = static_cast<int>(ScopedValues.undoMemoryMB);
    if (ImGui::SliderInt("Undo memory (MB)", &ScopedUndoMemory, 1, 256))
    {
        ScopedValues.undoMemoryMB = static_cast<std::uint32_t>(std::clamp(ScopedUndoMemory, 1, 256));
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("Arena reserved for compressed undo deltas. Applying a smaller value reallocates it and "
                      "clears the undo history.");
    }
    int ScopedUndoCommands = static_cast<int>(ScopedValues.undoCommandLimit);
    if (ImGui::SliderInt("Undo commands", &ScopedUndoCommands, 16, 512))
    {
        ScopedValues.undoCommandLimit = static_cast<std::uint16_t>(std::clamp(ScopedUndoCommands, 16, 512));
    }
    if (MCommands != nullptr && MCommands->Initialized())
    {
        const CommandStack::Stats& ScopedStats = MCommands->GetStats();
        ImGui::TextDisabled("Undo: %u commands, %u / %u KiB payload. Thumbnails: %u.", ScopedStats.commands,
                            static_cast<unsigned>(ScopedStats.bytesUsed / 1024u),
                            static_cast<unsigned>(ScopedStats.bytesCapacity / 1024u),
                            MThumbnails != nullptr ? MThumbnails->Count() : 0u);
    }
    if (ImGui::Button("Free undo history and thumbnails"))
    {
        if (MThumbnails != nullptr)
        {
            MThumbnails->Clear();
        }
        if (MCommands != nullptr)
        {
            if (!MCommands->SetLimits(static_cast<std::size_t>(ScopedValues.undoMemoryMB) * (1u << 20),
                                      ScopedValues.undoCommandLimit))
            {
                LogWarn("settings", "could not reallocate the undo arena; keeping the previous buffer");
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Drops the undo stack and all cached previews now.");
    int ScopedFrameLimit = static_cast<int>(ScopedValues.frameRateLimit);
    if (ImGui::SliderInt("Frame rate limit", &ScopedFrameLimit, 0, 240, ScopedFrameLimit == 0 ? "unlimited" : "%d FPS"))
    {
        ScopedValues.frameRateLimit = static_cast<std::uint16_t>(std::clamp(ScopedFrameLimit, 0, 240));
    }
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("0 runs unthrottled. A limit sleeps the main loop and frees CPU/GPU time while the "
                      "editor is idle; it does not change the authored frame rate.");
    }
    ImGui::Checkbox("Throttle when unfocused", &ScopedValues.pauseWhenUnfocused);
    if (ImGui::IsItemHovered())
    {
        EditorTooltip("When the editor loses focus, cap the loop at 5 FPS until it returns.");
    }

    ImGui::SeparatorText("Authoring");
    ImGui::Checkbox("Watch assets (OS watcher + hot reload)", &ScopedValues.watchAssets);
    ImGui::Checkbox("Auto refresh Content Browser", &ScopedValues.autoRefreshBrowser);
    ImGui::Checkbox("Save editor.ini on commit", &ScopedValues.saveEditorIniOnCommit);
    ImGui::Checkbox("ImGui demo window", &ScopedValues.showDemoWindow);

    ImGui::SeparatorText("Editor theme and symbol colours");
    const char* ScopedThemeIds[] = {"graphite", "very-dark", "midnight", "forest", "plum", "amber", "crimson"};
    const char* ScopedThemeNames[] = {"Graphite", "Very dark (not black)", "Midnight RGB", "Forest RGB", "Plum RGB",
                                      "Amber", "Crimson"};
    int ScopedTheme = static_cast<int>(ParseEditorTheme(ScopedValues.theme));
    if (ImGui::Combo("Panel theme", &ScopedTheme, ScopedThemeNames, 7))
    {
        CopyProjectField(ScopedValues.theme, sizeof(ScopedValues.theme), ScopedThemeIds[ScopedTheme]);
    }
    EditSymbolColor("Text colour", &ScopedValues.textColor);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Text opacity", &ScopedValues.textOpacity, 0.2f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Text contrast", &ScopedValues.textContrast, 0.5f, 2.0f, "%.2f");
    ImGui::Checkbox("Text outline", &ScopedValues.textOutline);
    ImGui::BeginDisabled(!ScopedValues.textOutline);
    EditSymbolColor("Outline colour", &ScopedValues.textOutlineColor);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Outline size", &ScopedValues.textOutlineSize, 0.5f, 4.0f, "%.1f px");
    ImGui::EndDisabled();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Panel glow", &ScopedValues.panelGlow, 0.0f, 1.0f, "%.2f");
    ImGui::TextDisabled("Panel theme controls surface tints; text colour, opacity and contrast are independent. "
                        "Text is authored in shades of white by default.");
    if (ImGui::Checkbox("Fine-tune symbol colours", &ScopedValues.customSymbolColors) && ScopedValues.customSymbolColors)
    {
        SeedSymbolColors(ScopedValues, CurrentEditorPalette());
    }
    if (ImGui::Button("Accessible symbol preset (Okabe-Ito)"))
    {
        ScopedValues.customSymbolColors = true;
        ScopedValues.symbolColors.AxisX = 0x0072B2;
        ScopedValues.symbolColors.AxisY = 0xE69F00;
        ScopedValues.symbolColors.Grid = 0x555555;
        ScopedValues.symbolColors.Play = 0x009E73;
        ScopedValues.symbolColors.Pause = 0xF0E442;
        ScopedValues.symbolColors.Step = 0xCC79A7;
        ScopedValues.symbolColors.Stop = 0xD55E00;
        ScopedValues.symbolColors.Rotate = 0x56B4E9;
        ScopedValues.symbolColors.Multiplayer = 0xFFFFFF;
        ScopedValues.symbolColors.Plot = {0x0072B2, 0xE69F00, 0x009E73, 0xCC79A7};
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Deuteranopia/protanopia-safe luminance and hue");
    ImGui::BeginDisabled(!ScopedValues.customSymbolColors);
    EditSymbolColor("Axis X", &ScopedValues.symbolColors.AxisX);
    ImGui::SameLine();
    EditSymbolColor("Axis Y", &ScopedValues.symbolColors.AxisY);
    ImGui::SameLine();
    EditSymbolColor("Grid", &ScopedValues.symbolColors.Grid);
    EditSymbolColor("Play triangle", &ScopedValues.symbolColors.Play);
    ImGui::SameLine();
    EditSymbolColor("Pause bars", &ScopedValues.symbolColors.Pause);
    ImGui::SameLine();
    EditSymbolColor("Step", &ScopedValues.symbolColors.Step);
    ImGui::SameLine();
    EditSymbolColor("Stop square", &ScopedValues.symbolColors.Stop);
    EditSymbolColor("Rotate", &ScopedValues.symbolColors.Rotate);
    ImGui::SameLine();
    EditSymbolColor("Multiplayer", &ScopedValues.symbolColors.Multiplayer);
    const char* ScopedPlotLabels[] = {"Plot 1", "Plot 2", "Plot 3", "Plot 4"};
    for (std::size_t ScopedI = 0; ScopedI < ScopedValues.symbolColors.Plot.size(); ++ScopedI)
    {
        EditSymbolColor(ScopedPlotLabels[ScopedI], &ScopedValues.symbolColors.Plot[ScopedI]);
        if (ScopedI != ScopedValues.symbolColors.Plot.size() - 1)
        {
            ImGui::SameLine();
        }
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Theme changes the semantic colours directly; no full-editor colour matrix is applied.");

    // IDE accessibility. These switches apply the engine's accessibility
    ImGui::SeparatorText("Accessibility (editor window)");
    AccessibilityEditorSettings& ScopedIde = MStore->Project().accessibility.editor;
    if (ImGui::Checkbox("Blind mode: keyboard navigation, always-visible focus, LogBoy narration sink",
                        &ScopedIde.blindMode) &&
        ScopedIde.blindMode)
    {
        // Turning Blind mode on turns the editor narrator on with it: the
        // switch exists to make the editor usable without sight.
        MStore->Editor().narration.Enabled = true;
    }
    ImGui::Checkbox("Deaf mode: visual status cues only (no editor audio alerts)", &ScopedIde.deafMode);
    ImGui::Checkbox("High contrast (WCAG 2.2, 4.5:1 minimum)", &ScopedIde.highContrast);
    ImGui::Checkbox("Reduce motion (no blinking caret)", &ScopedIde.reduceMotion);
    ImGui::Checkbox("FlashGuard on editor background", &ScopedIde.flashGuard);
    ImGui::BeginDisabled(!ScopedIde.flashGuard);
    ImGui::SliderFloat("Max luminance change / frame", &ScopedIde.flashThreshold, 0.02f, 0.45f, "%.3f");
    ImGui::EndDisabled();

    ImGui::TextDisabled("Colour-vision support is selected above as a theme; it changes semantic colours, not the whole editor.");
    if (ScopedIde.blindMode)
    {
        ImGui::TextWrapped("LogBoy records installed by this mode are fixed-size PODs; a screen reader can drain "
                           "them from the LogBoy panel's ring without blocking the frame.");
    }

    // Editor narrator. It reads the editor's own settings, never the project's
    // game narration policy, and it is what makes the window usable blind.
    NarratorSettings& ScopedEditorNarration = MStore->Editor().narration;
    ImGui::SeparatorText("Narrator (editor window)");
    ImGui::Checkbox("Speak panels, focus, errors and PIE state", &ScopedEditorNarration.Enabled);
    ImGui::SameLine();
    ImGui::TextDisabled("(off by default; Blind mode turns it on)");
    ImGui::BeginDisabled(!ScopedEditorNarration.Enabled);
    ImGui::Checkbox("Announce focus", &ScopedEditorNarration.AnnounceFocus);
    ImGui::SameLine();
    ImGui::Checkbox("Announce role", &ScopedEditorNarration.AnnounceRole);
    ImGui::SameLine();
    ImGui::Checkbox("Announce state", &ScopedEditorNarration.AnnounceState);
    ImGui::Checkbox("Focus interrupts speech", &ScopedEditorNarration.InterruptOnFocus);
    const char* ScopedEditorVerbosityNames[] = {"Silent", "Name only", "Name and role", "Name, role and state"};
    int ScopedEditorVerbosity = static_cast<int>(std::min<std::uint8_t>(ScopedEditorNarration.Verbosity, 3));
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("Verbosity", &ScopedEditorVerbosity, ScopedEditorVerbosityNames, 4))
    {
        ScopedEditorNarration.Verbosity = static_cast<std::uint8_t>(std::clamp(ScopedEditorVerbosity, 0, 3));
    }
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Speech rate", &ScopedEditorNarration.Rate, 0.5f, 2.0f, "%.2f");
    char ScopedEditorLanguage[16]{};
    std::snprintf(ScopedEditorLanguage, sizeof(ScopedEditorLanguage), "%s", ScopedEditorNarration.Language);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputText("Voice language (BCP-47)", ScopedEditorLanguage, sizeof(ScopedEditorLanguage)))
    {
        std::snprintf(ScopedEditorNarration.Language, sizeof(ScopedEditorNarration.Language), "%s",
                      ScopedEditorLanguage);
    }
    DrawVoiceCombo("Voice", ScopedEditorNarration.VoiceId, sizeof(ScopedEditorNarration.VoiceId));
    ImGui::TextDisabled("The editor interface is English today; the voice is selected by the tag above and by "
                        "the installed voices.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Screen reader bridge (editor window)");
    AccessibilityBridgeSettings& ScopedEditorBridge = MStore->Editor().bridge;
    ImGui::Checkbox("Publish editor panels to the OS accessibility tree", &ScopedEditorBridge.Enabled);
    ImGui::BeginDisabled(!ScopedEditorBridge.Enabled);
    ImGui::Checkbox("Focus events", &ScopedEditorBridge.RaiseFocusEvents);
    ImGui::SameLine();
    ImGui::Checkbox("Structure events", &ScopedEditorBridge.RaiseStructureEvents);
    ImGui::SameLine();
    ImGui::Checkbox("State events", &ScopedEditorBridge.RaiseStateEvents);
    ImGui::TextDisabled("Windows: UI Automation. The game keeps its own bridge policy in Project Settings.");
    ImGui::EndDisabled();

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
    if (InContext.window != nullptr)
    {
        if (ScopedValues.themedCursor)
        {
            if (!InContext.window->SetCursorImage(ScopedValues.cursorImage, 2, 2))
            {
                LogWarn("settings", "cursor image '{}' could not be loaded; keeping the current cursor",
                        ScopedValues.cursorImage);
            }
        }
        else
        {
            InContext.window->SetSystemCursor();
        }
    }
    // The font set owns the global font scale: EditorSystem reloads the atlas
    // from the new EditorSettings on the next frame, so writing FontScaleMain
    // here would race that path and break the legacy density fallback.
    LogRetention ScopedRetention;
    ScopedRetention.MaxFiles = ScopedValues.logMaxFiles;
    ScopedRetention.MaxLinesPerFile = ScopedValues.logMaxLinesPerFile;
    SetLogRetention(ScopedRetention);
    if (MThumbnails != nullptr)
    {
        MThumbnails->SetEnabled(ScopedValues.thumbnailCacheEnabled);
        MThumbnails->SetLimit(ScopedValues.thumbnailCacheLimit);
    }
    if (MCommands != nullptr &&
        (!MAppliedValid || MApplied.undoMemoryMB != ScopedValues.undoMemoryMB ||
         MApplied.undoCommandLimit != ScopedValues.undoCommandLimit))
    {
        if (!MCommands->SetLimits(static_cast<std::size_t>(ScopedValues.undoMemoryMB) * (1u << 20),
                                  ScopedValues.undoCommandLimit))
        {
            LogWarn("settings", "could not apply the undo memory budget; keeping the previous buffer");
        }
    }
    ApplyIdeAccessibility(MStore->Project().accessibility.editor);
    const bool ScopedThemeChanged =
        !MAppliedValid || std::strcmp(MApplied.theme, ScopedValues.theme) != 0 ||
        MApplied.textColor != ScopedValues.textColor || MApplied.textOpacity != ScopedValues.textOpacity ||
        MApplied.textContrast != ScopedValues.textContrast || MApplied.textOutline != ScopedValues.textOutline ||
        MApplied.textOutlineColor != ScopedValues.textOutlineColor ||
        MApplied.textOutlineSize != ScopedValues.textOutlineSize ||
        MApplied.panelGlow != ScopedValues.panelGlow ||
        MApplied.customSymbolColors != ScopedValues.customSymbolColors ||
        MApplied.symbolColors != ScopedValues.symbolColors ||
        MApplied.stockingTheme != ScopedValues.stockingTheme ||
        MApplied.stockingColorA != ScopedValues.stockingColorA ||
        MApplied.stockingColorB != ScopedValues.stockingColorB;
    if (ScopedThemeChanged)
    {
        ApplyEditorTheme(ScopedValues);
        Palette().captured = false;
    }
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
