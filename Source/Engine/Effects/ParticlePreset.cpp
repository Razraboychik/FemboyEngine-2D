// FemboyEngine2D - .fbyparticle preset data-pack loader.
#include "Engine/Effects/ParticlePreset.hpp"

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
[[nodiscard]] std::string StringField(const toml::table& InTable, std::string_view InKey,
                                      std::string_view InFallback = {})
{
    const auto ScopedNode = InTable[InKey];
    const std::optional<std::string> ScopedValue = ScopedNode.value<std::string>();
    return ScopedValue.has_value() ? *ScopedValue : std::string(InFallback);
}

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

[[nodiscard]] std::pair<float, float> PairField(const toml::table& InTable, std::string_view InKey, float InFallbackA,
                                                float InFallbackB) noexcept
{
    const toml::array* ScopedArray = InTable[InKey].as_array();
    if (ScopedArray == nullptr || ScopedArray->size() != 2)
    {
        return {InFallbackA, InFallbackB};
    }
    const auto ScopedA = (*ScopedArray)[0].value<double>();
    const auto ScopedB = (*ScopedArray)[1].value<double>();
    if (!ScopedA.has_value() || !ScopedB.has_value())
    {
        return {InFallbackA, InFallbackB};
    }
    return {static_cast<float>(*ScopedA), static_cast<float>(*ScopedB)};
}

[[nodiscard]] bool Vec2Field(const toml::table& InTable, std::string_view InKey, Vector2D& OutValue) noexcept
{
    const toml::array* ScopedArray = InTable[InKey].as_array();
    if (ScopedArray == nullptr || ScopedArray->size() != 2)
    {
        return false;
    }
    const auto ScopedX = (*ScopedArray)[0].value<double>();
    const auto ScopedY = (*ScopedArray)[1].value<double>();
    if (!ScopedX.has_value() || !ScopedY.has_value())
    {
        return false;
    }
    OutValue = Vector2D{static_cast<float>(*ScopedX), static_cast<float>(*ScopedY)};
    return true;
}

[[nodiscard]] std::optional<ParticleShape> ShapeFromName(std::string_view InName) noexcept
{
    static constexpr std::array<std::string_view, kParticleShapeCount> kNames{
        "Square", "Circle", "Ring",   "Glow", "Star",      "Diamond",
        "Spark",  "Smoke",  "Bubble", "Leaf", "Snowflake", "Heart"};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kParticleShapeCount; ++ScopedIndex)
    {
        if (InName == kNames[ScopedIndex])
        {
            return static_cast<ParticleShape>(ScopedIndex);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ParticleEmissionShape> EmissionShapeFromName(std::string_view InName) noexcept
{
    static constexpr std::array<std::string_view, 4> kNames{"Point", "Circle", "Box", "Line"};
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kNames.size(); ++ScopedIndex)
    {
        if (InName == kNames[ScopedIndex])
        {
            return static_cast<ParticleEmissionShape>(ScopedIndex);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> ColorFromText(std::string_view InText) noexcept
{
    if (!InText.empty() && InText.front() == '#')
    {
        InText.remove_prefix(1);
    }
    if (InText.size() != 6 && InText.size() != 8)
    {
        return std::nullopt;
    }
    std::uint32_t ScopedValue = 0;
    for (const char ScopedCharacter : InText)
    {
        std::uint32_t ScopedDigit = 0;
        if (ScopedCharacter >= '0' && ScopedCharacter <= '9')
        {
            ScopedDigit = static_cast<std::uint32_t>(ScopedCharacter - '0');
        }
        else if (ScopedCharacter >= 'a' && ScopedCharacter <= 'f')
        {
            ScopedDigit = static_cast<std::uint32_t>(ScopedCharacter - 'a' + 10);
        }
        else if (ScopedCharacter >= 'A' && ScopedCharacter <= 'F')
        {
            ScopedDigit = static_cast<std::uint32_t>(ScopedCharacter - 'A' + 10);
        }
        else
        {
            return std::nullopt;
        }
        ScopedValue = (ScopedValue << 4u) | ScopedDigit;
    }
    const std::uint32_t ScopedAlpha = InText.size() == 8 ? ScopedValue & 0xFFu : 0xFFu;
    const std::uint32_t ScopedRgb = InText.size() == 8 ? ScopedValue >> 8u : ScopedValue;
    const std::uint32_t ScopedRed = (ScopedRgb >> 16u) & 0xFFu;
    const std::uint32_t ScopedGreen = (ScopedRgb >> 8u) & 0xFFu;
    const std::uint32_t ScopedBlue = ScopedRgb & 0xFFu;
    return Color32::FromBytes(static_cast<std::uint8_t>(ScopedRed), static_cast<std::uint8_t>(ScopedGreen),
                              static_cast<std::uint8_t>(ScopedBlue), static_cast<std::uint8_t>(ScopedAlpha))
        .rgba;
}

[[nodiscard]] bool ParsePreset(const toml::table& InTable, ParticlePresetDesc& OutPreset, std::string* OutError)
{
    const std::string ScopedId = StringField(InTable, "id");
    if (ScopedId.empty())
    {
        if (OutError != nullptr)
        {
            *OutError = "particle preset is missing id";
        }
        return false;
    }
    std::snprintf(OutPreset.Id, sizeof(OutPreset.Id), "%s", ScopedId.c_str());
    const std::string ScopedName = StringField(InTable, "name", ScopedId);
    std::snprintf(OutPreset.DisplayName, sizeof(OutPreset.DisplayName), "%s", ScopedName.c_str());
    const std::string ScopedCategory = StringField(InTable, "category", "Pack");
    std::snprintf(OutPreset.Category, sizeof(OutPreset.Category), "%s", ScopedCategory.c_str());

    const std::optional<ParticleShape> ScopedShape = ShapeFromName(StringField(InTable, "shape", "Circle"));
    if (!ScopedShape.has_value())
    {
        if (OutError != nullptr)
        {
            *OutError = "particle preset " + ScopedId + " has an unknown shape";
        }
        return false;
    }
    const std::optional<ParticleEmissionShape> ScopedEmissionShape =
        EmissionShapeFromName(StringField(InTable, "emission_shape", "Point"));
    if (!ScopedEmissionShape.has_value())
    {
        if (OutError != nullptr)
        {
            *OutError = "particle preset " + ScopedId + " has an unknown emission shape";
        }
        return false;
    }

    ParticleEffectSettings& ScopedSettings = OutPreset.Settings;
    ScopedSettings.Shape = *ScopedShape;
    ScopedSettings.EmissionShape = *ScopedEmissionShape;
    ScopedSettings.MaxParticles = static_cast<std::uint32_t>(NumberField(InTable, "max_particles", 256.0));
    ScopedSettings.Seed = static_cast<std::uint32_t>(NumberField(InTable, "seed", 0x9E3779B9u));
    (void)Vec2Field(InTable, "direction", ScopedSettings.Direction);
    (void)Vec2Field(InTable, "gravity", ScopedSettings.Gravity);
    (void)Vec2Field(InTable, "wind", ScopedSettings.Wind);
    (void)Vec2Field(InTable, "emission_extents", ScopedSettings.EmissionExtents);
    ScopedSettings.SpreadRadians = static_cast<float>(NumberField(InTable, "spread", 0.35));
    ScopedSettings.Drag = static_cast<float>(NumberField(InTable, "drag", 0.0));
    ScopedSettings.Turbulence = static_cast<float>(NumberField(InTable, "turbulence", 0.0));
    ScopedSettings.Swirl = static_cast<float>(NumberField(InTable, "swirl", 0.0));
    ScopedSettings.EmissionRate = static_cast<float>(NumberField(InTable, "rate", 60.0));
    ScopedSettings.BurstCount = static_cast<std::uint32_t>(NumberField(InTable, "burst_count", 0.0));
    ScopedSettings.BurstInterval = static_cast<float>(NumberField(InTable, "burst_interval", 0.0));
    ScopedSettings.Duration = static_cast<float>(NumberField(InTable, "duration", 0.0));
    ScopedSettings.Loop = BoolField(InTable, "loop", true);
    ScopedSettings.Additive = BoolField(InTable, "additive", false);

    const auto ScopedSpeed = PairField(InTable, "speed", 40.0f, 140.0f);
    ScopedSettings.SpeedMin = ScopedSpeed.first;
    ScopedSettings.SpeedMax = ScopedSpeed.second;
    const auto ScopedLifetime = PairField(InTable, "lifetime", 0.4f, 1.2f);
    ScopedSettings.LifetimeMin = ScopedLifetime.first;
    ScopedSettings.LifetimeMax = ScopedLifetime.second;
    const auto ScopedSize = PairField(InTable, "size", 8.0f, 2.0f);
    ScopedSettings.StartSize = ScopedSize.first;
    ScopedSettings.EndSize = ScopedSize.second;
    const auto ScopedRotation = PairField(InTable, "rotation", 0.0f, 1.0f);
    ScopedSettings.StartRotation = ScopedRotation.first;
    ScopedSettings.RotationSpeed = ScopedRotation.second;

    const toml::array* ScopedColors = InTable["colors"].as_array();
    if (ScopedColors != nullptr && ScopedColors->size() == 2)
    {
        const auto ScopedStart = (*ScopedColors)[0].value<std::string>();
        const auto ScopedEnd = (*ScopedColors)[1].value<std::string>();
        if (!ScopedStart.has_value() || !ScopedEnd.has_value())
        {
            if (OutError != nullptr)
            {
                *OutError = "particle preset " + ScopedId + " colors must be two strings";
            }
            return false;
        }
        const auto ScopedStartColor = ColorFromText(*ScopedStart);
        const auto ScopedEndColor = ColorFromText(*ScopedEnd);
        if (!ScopedStartColor.has_value() || !ScopedEndColor.has_value())
        {
            if (OutError != nullptr)
            {
                *OutError = "particle preset " + ScopedId + " has an invalid colour";
            }
            return false;
        }
        ScopedSettings.StartColor = Color32{*ScopedStartColor};
        ScopedSettings.EndColor = Color32{*ScopedEndColor};
    }

    ScopedSettings.Clamp();
    return true;
}
}

bool ParticlePresetRegistry::Add(const ParticlePresetDesc& InPreset, const void* InOwner, std::string* OutError)
{
    if (InPreset.Id[0] == '\0' || InPreset.DisplayName[0] == '\0')
    {
        if (OutError != nullptr)
        {
            *OutError = "particle preset needs id and display name";
        }
        return false;
    }
    if (Find(InPreset.Id) != nullptr)
    {
        if (OutError != nullptr)
        {
            *OutError = "duplicate particle preset id: " + std::string(InPreset.Id);
        }
        return false;
    }
    if (MCount >= kParticlePresetMaxCount)
    {
        if (OutError != nullptr)
        {
            *OutError = "particle preset registry is full";
        }
        return false;
    }
    MItems[MCount] = InPreset;
    MItems[MCount].Settings.Clamp();
    MOwners[MCount] = InOwner;
    ++MCount;
    return true;
}

void ParticlePresetRegistry::RemoveOwner(const void* InOwner) noexcept
{
    if (InOwner == nullptr)
    {
        return;
    }
    std::uint32_t ScopedIndex = 0;
    while (ScopedIndex < MCount)
    {
        if (MOwners[ScopedIndex] == InOwner)
        {
            MItems[ScopedIndex] = MItems[MCount - 1];
            MOwners[ScopedIndex] = MOwners[MCount - 1];
            --MCount;
        }
        else
        {
            ++ScopedIndex;
        }
    }
}

void ParticlePresetRegistry::Clear() noexcept
{
    MCount = 0;
}

void ParticlePresetRegistry::RegisterBuiltIns()
{
    for (const ParticleEffectPreset& ScopedPreset : ParticleEffectPresets())
    {
        ParticlePresetDesc ScopedDesc{};
        std::snprintf(ScopedDesc.Id, sizeof(ScopedDesc.Id), "%s", ScopedPreset.Id);
        const char* ScopedShortName = ScopedPreset.Id;
        for (const char* ScopedCursor = ScopedPreset.Id; *ScopedCursor != '\0'; ++ScopedCursor)
        {
            if (*ScopedCursor == '.')
            {
                ScopedShortName = ScopedCursor + 1;
            }
        }
        std::snprintf(ScopedDesc.DisplayName, sizeof(ScopedDesc.DisplayName), "%s", ScopedShortName);
        std::snprintf(ScopedDesc.Category, sizeof(ScopedDesc.Category), "%s", "BuiltIn");
        ScopedDesc.Settings = ScopedPreset.Settings;
        ScopedDesc.Flags = ParticlePresetBuiltIn;
        (void)Add(ScopedDesc);
    }
}

const ParticlePresetDesc* ParticlePresetRegistry::Find(std::string_view InId) const noexcept
{
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
    {
        if (InId == MItems[ScopedIndex].Id)
        {
            return &MItems[ScopedIndex];
        }
    }
    return nullptr;
}

const ParticlePresetDesc* ParticlePresetRegistry::At(std::uint32_t InIndex) const noexcept
{
    return InIndex < MCount ? &MItems[InIndex] : nullptr;
}

bool LoadParticlePresetToml(std::string_view InText, ParticlePresetRegistry& InRegistry, const void* InOwner,
                            std::string* OutError)
{
    toml::table ScopedRoot;
    try
    {
        ScopedRoot = toml::parse(InText);
    }
    catch (const toml::parse_error& ScopedParseError)
    {
        if (OutError != nullptr)
        {
            *OutError = "particle preset TOML error: " + std::string(ScopedParseError.description());
        }
        return false;
    }

    const toml::array* ScopedPresets = ScopedRoot["presets"].as_array();
    if (ScopedPresets == nullptr || ScopedPresets->empty())
    {
        if (OutError != nullptr)
        {
            *OutError = "particle preset pack needs at least one [[presets]] table";
        }
        return false;
    }

    for (const toml::node& ScopedNode : *ScopedPresets)
    {
        const toml::table* ScopedTable = ScopedNode.as_table();
        if (ScopedTable == nullptr)
        {
            if (OutError != nullptr)
            {
                *OutError = "particle preset entry is not a table";
            }
            return false;
        }
        ParticlePresetDesc ScopedDesc{};
        if (!ParsePreset(*ScopedTable, ScopedDesc, OutError))
        {
            return false;
        }
        if (!InRegistry.Add(ScopedDesc, InOwner, OutError))
        {
            return false;
        }
    }
    return true;
}

bool LoadParticlePresetFile(std::string_view InPath, ParticlePresetRegistry& InRegistry, const void* InOwner,
                            std::string* OutError)
{
    std::ifstream ScopedStream(std::string(InPath), std::ios::binary);
    if (!ScopedStream)
    {
        if (OutError != nullptr)
        {
            *OutError = "cannot open particle preset pack: " + std::string(InPath);
        }
        return false;
    }
    std::ostringstream ScopedContents;
    ScopedContents << ScopedStream.rdbuf();
    return LoadParticlePresetToml(ScopedContents.str(), InRegistry, InOwner, OutError);
}
}
