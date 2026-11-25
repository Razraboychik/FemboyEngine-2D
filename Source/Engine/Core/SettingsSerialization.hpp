// FemboyEngine2D - TOML persistence for developer-authored settings.
#pragma once

#include "Engine/Core/SettingsRegistry.hpp"

#include <filesystem>
#include <string>

namespace Fby::Settings
{

// Writes only values that are present in the selected scope. The schema itself
// is never written: it belongs to the game, while the file belongs to the player
// or to the project.
[[nodiscard]] bool SaveSettingsToml(const Registry& InRegistry, Scope InScope, const std::filesystem::path& InPath,
                                    std::string* InError = nullptr);

// Loads known keys and ignores unknown ones for forward compatibility. Dangerous
// user values are rejected here; safe mode is the only path that restores them.
[[nodiscard]] bool LoadSettingsToml(Registry& InRegistry, Scope InScope, const std::filesystem::path& InPath,
                                    std::size_t* OutLoaded = nullptr, std::string* InError = nullptr);

}