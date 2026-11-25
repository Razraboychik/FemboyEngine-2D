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
#if defined(FBY_FUZZ_STANDALONE)
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <vector>

int main(int InArgc, char** InArgv)
{
    if (InArgc < 2)
    {
        std::cerr << "usage: fby_settings_fuzz <corpus-directory>\n";
        return 2;
    }
    std::size_t ScopedInputs = 0;
    for (const std::filesystem::directory_entry& ScopedEntry :
         std::filesystem::recursive_directory_iterator(InArgv[1]))
    {
        if (!ScopedEntry.is_regular_file())
        {
            continue;
        }
        std::ifstream ScopedFile(ScopedEntry.path(), std::ios::binary);
        std::vector<std::uint8_t> ScopedBytes((std::istreambuf_iterator<char>(ScopedFile)),
                                              std::istreambuf_iterator<char>());
        if (!ScopedBytes.empty())
        {
            (void)LLVMFuzzerTestOneInput(ScopedBytes.data(), ScopedBytes.size());
            ++ScopedInputs;
        }
    }
    std::cout << "fuzz corpus: " << ScopedInputs << " input(s)\n";
    return ScopedInputs == 0 ? 1 : 0;
}
#endif
