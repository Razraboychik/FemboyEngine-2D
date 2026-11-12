// FemboyEngine2D - project plugin binding helpers.
#include "Editor/Infrastructure/ProjectPlugins.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <system_error>
#include <utility>

namespace Fby::Editor
{
namespace
{
[[nodiscard]] bool IsPluginExtension(const std::filesystem::path& InPath)
{
    std::string ScopedExtension = InPath.extension().string();
    std::transform(ScopedExtension.begin(), ScopedExtension.end(), ScopedExtension.begin(),
                   [](unsigned char InCharacter) { return static_cast<char>(std::tolower(InCharacter)); });
    return ScopedExtension == ".dll" || ScopedExtension == ".so" || ScopedExtension == ".dylib";
}

[[nodiscard]] bool IsProjectPluginPath(const std::string& InPath)
{
    return InPath.starts_with("plugins/") && InPath.find("..") == std::string::npos;
}

void SetError(std::string* OutError, std::string InMessage)
{
    if (OutError != nullptr)
    {
        *OutError = std::move(InMessage);
    }
}
}

bool ConnectProjectPlugin(const std::filesystem::path& InProjectRoot, const std::filesystem::path& InPluginFile,
                          ProjectSettings& InSettings, std::string* OutRelativePath, std::string* OutError)
{
    std::error_code ScopedCode;
    if (!std::filesystem::is_regular_file(InPluginFile, ScopedCode) || !IsPluginExtension(InPluginFile))
    {
        SetError(OutError, "not a plugin file: " + InPluginFile.string());
        return false;
    }
    const std::string ScopedRelative = "plugins/" + InPluginFile.filename().string();
    for (std::uint32_t ScopedI = 0; ScopedI < InSettings.pluginCount; ++ScopedI)
    {
        if (ScopedRelative == InSettings.plugins[ScopedI].Path)
        {
            InSettings.plugins[ScopedI].Enabled = true;
            if (OutRelativePath != nullptr)
            {
                *OutRelativePath = ScopedRelative;
            }
            return true;
        }
    }
    if (InSettings.pluginCount >= ProjectSettings::kMaxPlugins)
    {
        SetError(OutError, "project plugin list is full");
        return false;
    }
    const std::filesystem::path ScopedTarget = InProjectRoot / "plugins" / InPluginFile.filename();
    std::filesystem::create_directories(ScopedTarget.parent_path(), ScopedCode);
    if (ScopedCode)
    {
        SetError(OutError, "cannot create the project plugin directory: " + ScopedCode.message());
        return false;
    }
    std::filesystem::copy_file(InPluginFile, ScopedTarget, std::filesystem::copy_options::overwrite_existing,
                               ScopedCode);
    if (ScopedCode)
    {
        SetError(OutError, "cannot copy the plugin into the project: " + ScopedCode.message());
        return false;
    }
    ProjectPluginEntry& ScopedEntry = InSettings.plugins[InSettings.pluginCount++];
    std::snprintf(ScopedEntry.Path, sizeof(ScopedEntry.Path), "%s", ScopedRelative.c_str());
    ScopedEntry.Enabled = true;
    if (OutRelativePath != nullptr)
    {
        *OutRelativePath = ScopedRelative;
    }
    return true;
}

bool RemoveProjectPlugin(const std::filesystem::path& InProjectRoot, ProjectSettings& InSettings,
                         std::uint32_t InIndex, std::string* OutError)
{
    if (InIndex >= InSettings.pluginCount)
    {
        SetError(OutError, "plugin entry index is out of range");
        return false;
    }
    const std::string ScopedRelative = InSettings.plugins[InIndex].Path;
    if (IsProjectPluginPath(ScopedRelative))
    {
        std::error_code ScopedCode;
        std::filesystem::remove(InProjectRoot / ScopedRelative, ScopedCode);
        if (ScopedCode)
        {
            SetError(OutError, "cannot delete the copied plugin: " + ScopedCode.message());
            return false;
        }
    }
    for (std::uint32_t ScopedMove = InIndex + 1; ScopedMove < InSettings.pluginCount; ++ScopedMove)
    {
        InSettings.plugins[ScopedMove - 1] = InSettings.plugins[ScopedMove];
    }
    --InSettings.pluginCount;
    return true;
}
}
