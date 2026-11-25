// FemboyEngine2D - settings TOML persistence.
#include "Engine/Core/SettingsSerialization.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#if defined(TOML_ENABLE_FORMATTERS)
#undef TOML_ENABLE_FORMATTERS
#endif
#define TOML_ENABLE_FORMATTERS 1
#include <toml.hpp>

namespace Fby::Settings
{
namespace
{
void SetError(std::string* InError, std::string_view InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::string(InMessage);
    }
}

std::string ColorToHex(const Color& InColor)
{
    const auto ScopedChannel = [](float InValue)
    { return static_cast<unsigned>(std::lround(std::clamp(InValue, 0.0f, 1.0f) * 255.0f)); };
    std::ostringstream Scoped;
    Scoped << '#' << std::hex << std::setfill('0') << std::setw(2) << ScopedChannel(InColor.R) << std::setw(2)
           << ScopedChannel(InColor.G) << std::setw(2) << ScopedChannel(InColor.B) << std::setw(2)
           << ScopedChannel(InColor.A);
    return Scoped.str();
}

bool ColorFromHex(std::string_view InText, Color* OutColor)
{
    if (OutColor == nullptr || InText.size() != 9u || InText.front() != '#')
    {
        return false;
    }
    std::array<unsigned, 4> ScopedChannels{};
    for (std::size_t ScopedIndex = 0; ScopedIndex < 4; ++ScopedIndex)
    {
        const std::string ScopedByte(InText.substr(1u + ScopedIndex * 2u, 2u));
        char* ScopedEnd = nullptr;
        const unsigned long ScopedValue = std::strtoul(ScopedByte.c_str(), &ScopedEnd, 16);
        if (ScopedEnd == nullptr || *ScopedEnd != '\0')
        {
            return false;
        }
        ScopedChannels[ScopedIndex] = static_cast<unsigned>(ScopedValue);
    }
    *OutColor = Color{static_cast<float>(ScopedChannels[0]) / 255.0f, static_cast<float>(ScopedChannels[1]) / 255.0f,
                      static_cast<float>(ScopedChannels[2]) / 255.0f,
                      static_cast<float>(ScopedChannels[3]) / 255.0f};
    return true;
}
}

bool SaveSettingsToml(const Registry& InRegistry, Scope InScope, const std::filesystem::path& InPath,
                      std::string* InError)
{
    if (!InRegistry.IsFrozen())
    {
        SetError(InError, "settings schema is not frozen");
        return false;
    }
    try
    {
        toml::table ScopedSettings;
        InRegistry.ForEachOverride(
            InScope,
            [&ScopedSettings](const Descriptor& InDescriptor, const Value& InValue)
            {
                switch (InDescriptor.Kind)
                {
                    case Type::Bool:
                        ScopedSettings.insert(InDescriptor.Id, InValue.BoolValue);
                        break;
                    case Type::Int:
                        ScopedSettings.insert(InDescriptor.Id, InValue.IntValue);
                        break;
                    case Type::Float:
                        ScopedSettings.insert(InDescriptor.Id, InValue.FloatValue);
                        break;
                    case Type::Color:
                        ScopedSettings.insert(InDescriptor.Id, ColorToHex(InValue.ColorValue));
                        break;
                    case Type::String:
                        ScopedSettings.insert(InDescriptor.Id, InValue.StringValue);
                        break;
                    case Type::Enum:
                        ScopedSettings.insert(InDescriptor.Id, InValue.EnumValue);
                        break;
                }
            });

        toml::table ScopedRoot;
        ScopedRoot.insert("settings", std::move(ScopedSettings));
        const std::filesystem::path ScopedTemporary = InPath.string() + ".tmp";
        if (InPath.has_parent_path())
        {
            std::error_code ScopedCode;
            std::filesystem::create_directories(InPath.parent_path(), ScopedCode);
            if (ScopedCode)
            {
                SetError(InError, "cannot create settings directory: " + ScopedCode.message());
                return false;
            }
        }
        {
            std::ofstream ScopedStream(ScopedTemporary, std::ios::binary | std::ios::trunc);
            if (!ScopedStream)
            {
                SetError(InError, "cannot open settings temporary file");
                return false;
            }
            ScopedStream << ScopedRoot << '\n';
            if (!ScopedStream)
            {
                SetError(InError, "cannot write settings temporary file");
                return false;
            }
        }
        std::error_code ScopedCode;
        std::filesystem::rename(ScopedTemporary, InPath, ScopedCode);
        if (ScopedCode)
        {
            std::filesystem::remove(ScopedTemporary, ScopedCode);
            SetError(InError, "cannot replace settings file");
            return false;
        }
        return true;
    }
    catch (const std::exception& ScopedException)
    {
        SetError(InError, ScopedException.what());
        return false;
    }
}

bool LoadSettingsToml(Registry& InRegistry, Scope InScope, const std::filesystem::path& InPath,
                      std::size_t* OutLoaded, std::string* InError)
{
    if (!InRegistry.IsFrozen())
    {
        SetError(InError, "settings schema is not frozen");
        return false;
    }
    if (OutLoaded != nullptr)
    {
        *OutLoaded = 0;
    }
    try
    {
        const toml::table ScopedRoot = toml::parse_file(InPath.string());
        const toml::table* ScopedSettings = ScopedRoot["settings"].as_table();
        if (ScopedSettings == nullptr)
        {
            SetError(InError, "settings file has no [settings] table");
            return false;
        }
        std::size_t ScopedLoaded = 0;
        for (const auto& [ScopedKey, ScopedNode] : *ScopedSettings)
        {
            const std::string ScopedId(ScopedKey.str());
            const Descriptor* ScopedDescriptor = InRegistry.Find(ScopedId);
            if (ScopedDescriptor == nullptr)
            {
                continue;
            }
            Value ScopedValue;
            switch (ScopedDescriptor->Kind)
            {
                case Type::Bool:
                    ScopedValue = Value::MakeBool(ScopedNode.value_or(false));
                    break;
                case Type::Int:
                    ScopedValue = Value::MakeInt(ScopedNode.value_or(std::int64_t{0}));
                    break;
                case Type::Float:
                    ScopedValue = Value::MakeFloat(ScopedNode.value_or(0.0));
                    break;
                case Type::Color:
                {
                    Color ScopedColor;
                    if (!ColorFromHex(ScopedNode.value_or(std::string{}), &ScopedColor))
                    {
                        continue;
                    }
                    ScopedValue = Value::MakeColor(ScopedColor);
                    break;
                }
                case Type::String:
                    ScopedValue = Value::MakeString(ScopedNode.value_or(std::string{}));
                    break;
                case Type::Enum:
                    ScopedValue = Value::MakeEnum(ScopedNode.value_or(std::string{}));
                    break;
            }
            bool ScopedAccepted = false;
            if (InScope == Scope::Project)
            {
                ScopedAccepted = InRegistry.SetProject(ScopedId, std::move(ScopedValue));
            }
            else if (InScope == Scope::User)
            {
                ScopedAccepted = InRegistry.SetUser(ScopedId, std::move(ScopedValue));
            }
            else
            {
                ScopedAccepted = InRegistry.SetSession(ScopedId, std::move(ScopedValue));
            }
            if (ScopedAccepted)
            {
                ++ScopedLoaded;
            }
        }
        if (OutLoaded != nullptr)
        {
            *OutLoaded = ScopedLoaded;
        }
        return true;
    }
    catch (const toml::parse_error& ScopedError)
    {
        SetError(InError, std::string("settings parse error: ") + std::string(ScopedError.description()));
        return false;
    }
}
}