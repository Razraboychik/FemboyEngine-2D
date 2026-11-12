// FemboyEngine2D - .fbyvoice preset data-pack loader.
#include "Engine/Audio/VoicePreset.hpp"

#include <toml.hpp>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace Fby
{
namespace
{
[[nodiscard]] double NumberField(const toml::table& InTable, std::string_view InKey, double InFallback) noexcept
{
    const auto ScopedNode = InTable[InKey];
    const std::optional<double> ScopedValue = ScopedNode.value<double>();
    return ScopedValue.has_value() ? *ScopedValue : InFallback;
}

[[nodiscard]] bool BoolField(const toml::table& InTable, std::string_view InKey, bool InFallback) noexcept
{
    const auto ScopedNode = InTable[InKey];
    const std::optional<bool> ScopedValue = ScopedNode.value<bool>();
    return ScopedValue.has_value() ? *ScopedValue : InFallback;
}

[[nodiscard]] std::string StringField(const toml::table& InTable, std::string_view InKey,
                                      std::string_view InFallback = {})
{
    const auto ScopedNode = InTable[InKey];
    const std::optional<std::string> ScopedValue = ScopedNode.value<std::string>();
    return ScopedValue.has_value() ? *ScopedValue : std::string(InFallback);
}

[[nodiscard]] VoiceEffectKind KindFromName(std::string_view InName) noexcept
{
    for (std::uint8_t ScopedIndex = 0; ScopedIndex < static_cast<std::uint8_t>(VoiceEffectKind::Count); ++ScopedIndex)
    {
        const auto ScopedKind = static_cast<VoiceEffectKind>(ScopedIndex);
        if (InName == VoiceEffectKindName(ScopedKind))
        {
            return ScopedKind;
        }
    }
    return VoiceEffectKind::None;
}

[[nodiscard]] std::uint32_t FlagsFromNames(const toml::array* InFlags, bool& OutValid) noexcept
{
    OutValid = true;
    std::uint32_t ScopedFlags = VoicePresetFlagNone;
    if (InFlags == nullptr)
    {
        return ScopedFlags;
    }
    for (const toml::node& ScopedNode : *InFlags)
    {
        const std::optional<std::string> ScopedName = ScopedNode.value<std::string>();
        if (!ScopedName.has_value())
        {
            OutValid = false;
            return VoicePresetFlagNone;
        }
        if (*ScopedName == "cosmetic")
        {
            ScopedFlags |= VoicePresetFlagCosmetic;
        }
        else if (*ScopedName == "moderation")
        {
            ScopedFlags |= VoicePresetFlagModeration;
        }
        else if (*ScopedName == "uplink")
        {
            ScopedFlags |= VoicePresetFlagAllowUplink;
        }
        else if (*ScopedName == "downlink")
        {
            ScopedFlags |= VoicePresetFlagAllowDownlink;
        }
        else
        {
            OutValid = false;
            return VoicePresetFlagNone;
        }
    }
    return ScopedFlags;
}

[[nodiscard]] bool ParseSlot(const toml::table& InTable, VoiceEffectSlot* OutSlot, std::string* OutError)
{
    const std::string ScopedKindName = StringField(InTable, "kind");
    const VoiceEffectKind ScopedKind = KindFromName(ScopedKindName);
    if (ScopedKind == VoiceEffectKind::None)
    {
        if (OutError != nullptr)
        {
            *OutError = "slot kind is missing or unknown: " + ScopedKindName;
        }
        return false;
    }
    VoiceEffectSlot ScopedSlot{};
    ScopedSlot.Kind = ScopedKind;
    ScopedSlot.Mix = static_cast<float>(NumberField(InTable, "mix", 1.0));
    switch (ScopedKind)
    {
        case VoiceEffectKind::Mute:
            ScopedSlot.Mute.FadeSeconds = static_cast<float>(NumberField(InTable, "fade_seconds", 0.08));
            break;
        case VoiceEffectKind::Muffle:
            ScopedSlot.Muffle.LowCutHz = static_cast<float>(NumberField(InTable, "low_cut_hz", 120.0));
            ScopedSlot.Muffle.HighCutHz = static_cast<float>(NumberField(InTable, "high_cut_hz", 620.0));
            ScopedSlot.Muffle.Resonance = static_cast<float>(NumberField(InTable, "resonance", 1.0));
            ScopedSlot.Muffle.DriveDb = static_cast<float>(NumberField(InTable, "drive_db", 9.0));
            ScopedSlot.Muffle.WobbleHz = static_cast<float>(NumberField(InTable, "wobble_hz", 3.2));
            ScopedSlot.Muffle.WobbleDepth = static_cast<float>(NumberField(InTable, "wobble_depth", 0.28));
            ScopedSlot.Muffle.BreathLevel = static_cast<float>(NumberField(InTable, "breath_level", 0.06));
            break;
        case VoiceEffectKind::Underwater:
            ScopedSlot.Underwater.LowCutHz = static_cast<float>(NumberField(InTable, "low_cut_hz", 140.0));
            ScopedSlot.Underwater.HighCutHz = static_cast<float>(NumberField(InTable, "high_cut_hz", 700.0));
            ScopedSlot.Underwater.Resonance = static_cast<float>(NumberField(InTable, "resonance", 1.35));
            ScopedSlot.Underwater.WowHz = static_cast<float>(NumberField(InTable, "wow_hz", 0.7));
            ScopedSlot.Underwater.WowDepth = static_cast<float>(NumberField(InTable, "wow_depth", 0.35));
            break;
        case VoiceEffectKind::Radio:
            ScopedSlot.Radio.LowCutHz = static_cast<float>(NumberField(InTable, "low_cut_hz", 300.0));
            ScopedSlot.Radio.HighCutHz = static_cast<float>(NumberField(InTable, "high_cut_hz", 3200.0));
            ScopedSlot.Radio.StaticLevel = static_cast<float>(NumberField(InTable, "static_level", 0.18));
            ScopedSlot.Radio.CrackleRate = static_cast<float>(NumberField(InTable, "crackle_rate", 7.0));
            ScopedSlot.Radio.Squelch = static_cast<float>(NumberField(InTable, "squelch", 0.12));
            ScopedSlot.Radio.DriveDb = static_cast<float>(NumberField(InTable, "drive_db", 6.0));
            ScopedSlot.Radio.MuteWhenSquelched = BoolField(InTable, "mute_when_squelched", false);
            break;
        case VoiceEffectKind::Pitch:
            ScopedSlot.Pitch.Semitones = static_cast<float>(NumberField(InTable, "semitones", 7.0));
            ScopedSlot.Pitch.WindowMs = static_cast<float>(NumberField(InTable, "window_ms", 20.0));
            break;
        case VoiceEffectKind::Formant:
            ScopedSlot.Formant.Semitones = static_cast<float>(NumberField(InTable, "semitones", 4.0));
            ScopedSlot.Formant.StrengthDb = static_cast<float>(NumberField(InTable, "strength_db", 6.0));
            break;
        case VoiceEffectKind::Distortion:
            ScopedSlot.Distortion.DriveDb = static_cast<float>(NumberField(InTable, "drive_db", 12.0));
            ScopedSlot.Distortion.LowCutHz = static_cast<float>(NumberField(InTable, "low_cut_hz", 180.0));
            ScopedSlot.Distortion.HighCutHz = static_cast<float>(NumberField(InTable, "high_cut_hz", 5000.0));
            ScopedSlot.Distortion.BitDepth = static_cast<float>(NumberField(InTable, "bit_depth", 10.0));
            break;
        case VoiceEffectKind::RingModulation:
            ScopedSlot.RingModulation.FrequencyHz = static_cast<float>(NumberField(InTable, "frequency_hz", 30.0));
            ScopedSlot.RingModulation.Depth = static_cast<float>(NumberField(InTable, "depth", 0.7));
            break;
        case VoiceEffectKind::Whisper:
            ScopedSlot.Whisper.NoiseLevel = static_cast<float>(NumberField(InTable, "noise_level", 0.35));
            ScopedSlot.Whisper.LowCutHz = static_cast<float>(NumberField(InTable, "low_cut_hz", 900.0));
            ScopedSlot.Whisper.HighCutHz = static_cast<float>(NumberField(InTable, "high_cut_hz", 6500.0));
            break;
        case VoiceEffectKind::None:
        case VoiceEffectKind::Count:
            break;
    }
    *OutSlot = ScopedSlot;
    return true;
}
}

bool LoadVoicePresetToml(std::string_view InText, VoicePresetRegistry& InRegistry, const void* InOwner,
                         std::string* OutError)
{
    const auto ScopedFail = [OutError](std::string InMessage)
    {
        if (OutError != nullptr)
        {
            *OutError = std::move(InMessage);
        }
        return false;
    };
    toml::table ScopedRoot;
    try
    {
        ScopedRoot = toml::parse(InText);
    }
    catch (const toml::parse_error& ScopedParseError)
    {
        return ScopedFail("voice preset TOML error: " + std::string(ScopedParseError.description()));
    }

    const std::int64_t ScopedVersion = ScopedRoot["version"].value<std::int64_t>().value_or(0);
    if (ScopedVersion != 1)
    {
        return ScopedFail("voice preset version must be 1");
    }

    VoicePresetDesc ScopedPreset{};
    const std::string ScopedId = StringField(ScopedRoot, "id");
    const std::string ScopedNameKey = StringField(ScopedRoot, "name_key");
    std::snprintf(ScopedPreset.Id, sizeof(ScopedPreset.Id), "%s", ScopedId.c_str());
    std::snprintf(ScopedPreset.NameKey, sizeof(ScopedPreset.NameKey), "%s", ScopedNameKey.c_str());
    ScopedPreset.Category = VoicePresetCategoryFromName(StringField(ScopedRoot, "category"));
    bool ScopedFlagsValid = true;
    ScopedPreset.Flags = FlagsFromNames(ScopedRoot["flags"].as_array(), ScopedFlagsValid);
    if (!ScopedFlagsValid)
    {
        return ScopedFail("voice preset flags contain an unknown name");
    }
    ScopedPreset.MinIntensity = static_cast<float>(NumberField(ScopedRoot, "min_intensity", 0.0));
    ScopedPreset.MaxIntensity = static_cast<float>(NumberField(ScopedRoot, "max_intensity", 1.0));
    ScopedPreset.Profile.EnableConditions = BoolField(ScopedRoot, "enable_conditions", true);

    const toml::array* ScopedSlots = ScopedRoot["slot"].as_array();
    if (ScopedSlots == nullptr || ScopedSlots->empty())
    {
        return ScopedFail("voice preset needs at least one [[slot]] table");
    }
    for (const toml::node& ScopedNode : *ScopedSlots)
    {
        const toml::table* ScopedTable = ScopedNode.as_table();
        if (ScopedTable == nullptr)
        {
            return ScopedFail("voice preset slot is not a table");
        }
        VoiceEffectSlot ScopedSlot{};
        std::string ScopedSlotError;
        if (!ParseSlot(*ScopedTable, &ScopedSlot, &ScopedSlotError))
        {
            return ScopedFail(ScopedSlotError);
        }
        if (!ScopedPreset.Profile.BaseChain.Add(ScopedSlot))
        {
            return ScopedFail("voice preset has more than eight slots");
        }
    }

    return InRegistry.Add(ScopedPreset, InOwner, OutError);
}

bool LoadVoicePresetFile(std::string_view InPath, VoicePresetRegistry& InRegistry, const void* InOwner,
                         std::string* OutError)
{
    std::ifstream ScopedStream(std::string(InPath), std::ios::binary);
    if (!ScopedStream)
    {
        if (OutError != nullptr)
        {
            *OutError = "cannot open voice preset file: " + std::string(InPath);
        }
        return false;
    }
    std::ostringstream ScopedContents;
    ScopedContents << ScopedStream.rdbuf();
    return LoadVoicePresetToml(ScopedContents.str(), InRegistry, InOwner, OutError);
}
}
