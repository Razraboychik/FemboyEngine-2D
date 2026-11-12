// FemboyEngine2D - project plugin binding helpers.
#pragma once

#include "Editor/Infrastructure/ProjectConfig.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Fby::Editor
{

// Copies a plugin file into <root>/plugins and records its relative path in the
// project settings. The caller persists the settings and loads the plugin. A
// duplicate entry is re-enabled and its file is overwritten instead of being
// added twice. Returns true and fills OutRelativePath on success.
[[nodiscard]] bool ConnectProjectPlugin(const std::filesystem::path& InProjectRoot,
                                        const std::filesystem::path& InPluginFile,
                                        ProjectSettings& InSettings,
                                        std::string* OutRelativePath = nullptr,
                                        std::string* OutError = nullptr);

// Removes the entry at InIndex and deletes its copied file when it lives under
// <root>/plugins. The caller unloads the plugin first and persists afterwards.
[[nodiscard]] bool RemoveProjectPlugin(const std::filesystem::path& InProjectRoot,
                                       ProjectSettings& InSettings,
                                       std::uint32_t InIndex,
                                       std::string* OutError = nullptr);
}
