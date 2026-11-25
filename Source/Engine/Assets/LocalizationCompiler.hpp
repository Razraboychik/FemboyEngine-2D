// FemboyEngine2D - localization asset compiler.
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Assets
{

// TOML layout: `default = "<locale>"` plus one table per locale. Failure is
// reported by the optional error pointer; the checked forms below make it part
// of the return value.
struct LocalizationCompileResult
{
    bool Ok = false;
    std::vector<std::byte> Bytes;
    std::string Error;
};

struct LocalizationDecompileResult
{
    bool Ok = false;
    std::string Text;
    std::string Error;
};

[[nodiscard]] LocalizationCompileResult CompileLocalizationChecked(std::string_view InTomlContent);
[[nodiscard]] LocalizationDecompileResult DecompileLocalizationChecked(std::span<const std::byte> InBinary);

[[nodiscard]] std::vector<std::byte> CompileLocalization(std::string_view InTomlContent,
                                                         std::string* InError = nullptr);

[[nodiscard]] std::string DecompileLocalization(std::span<const std::byte> InBinary, std::string* InError = nullptr);
}
