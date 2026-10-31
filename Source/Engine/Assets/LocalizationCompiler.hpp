// FemboyEngine2D - localization asset compiler.
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Assets
{

// TOML layout:
[[nodiscard]] std::vector<std::byte> CompileLocalization(std::string_view InTomlContent,
                                                         std::string* InError = nullptr);

[[nodiscard]] std::string DecompileLocalization(std::span<const std::byte> InBinary, std::string* InError = nullptr);
}
