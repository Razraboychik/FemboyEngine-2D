// FemboyEngine2D - libFuzzer target for persisted settings.
#include "Editor/Infrastructure/ProjectConfig.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* InData, std::size_t InSize)
{
    if (InData == nullptr || InSize == 0)
    {
        return 0;
    }
    const std::string_view ScopedText(reinterpret_cast<const char*>(InData), InSize);
    Fby::Editor::ProjectConfig ScopedConfig;
    std::string ScopedError;
    if (Fby::Editor::DecodeProjectToml(ScopedText, &ScopedConfig, &ScopedError))
    {
        const std::string ScopedRoundTrip = Fby::Editor::EncodeProjectToml(ScopedConfig);
        Fby::Editor::ProjectConfig ScopedReloaded;
        (void)Fby::Editor::DecodeProjectToml(ScopedRoundTrip, &ScopedReloaded, &ScopedError);
    }
    return 0;
}