// FemboyEngine2D - audio cooking entry point (cook-time only).
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Fby::Cook
{

// Cooks one audio source into a complete .fpcm block.
[[nodiscard]] bool CookAudioFile(const std::filesystem::path& InSource, std::vector<std::byte>* Output,
                                 std::string* InError);
}
