// FemboyEngine2D - mmap-backed cooked text asset test.
#include "Engine/Text/TextAssetFile.hpp"
#include "Engine/Text/TextAssetFormat.hpp"

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool InCondition, const char* InMessage)
{
    if (!InCondition)
    {
        std::fprintf(stderr, "FAIL: %s\n", InMessage);
        ++GFailures;
    }
}
}

int main()
{
    using namespace Fby;
    const std::filesystem::path ScopedDirectory =
        std::filesystem::current_path() / "build" / "fby_text_asset_file_test";
    std::error_code ScopedErrorCode;
    std::filesystem::create_directories(ScopedDirectory, ScopedErrorCode);
    if (ScopedErrorCode)
    {
        std::fprintf(stderr, "FAIL: cannot create test directory: %s\n", ScopedErrorCode.message().c_str());
        return 1;
    }
    const std::filesystem::path ScopedFile = ScopedDirectory / "story.fbytext";

    const std::string ScopedSource = "Mapped text with multiple blocks and words.";
    const std::vector<std::byte> ScopedBytes = Text::BuildTextAsset(ScopedSource, 12, nullptr);
    {
        std::ofstream ScopedStream(ScopedFile, std::ios::binary | std::ios::trunc);
        Check(static_cast<bool>(ScopedStream), "temporary cooked asset opens");
        ScopedStream.write(reinterpret_cast<const char*>(ScopedBytes.data()),
                           static_cast<std::streamsize>(ScopedBytes.size()));
        Check(static_cast<bool>(ScopedStream), "temporary cooked asset writes");
    }

    {
        Fby::VirtualFileSystem ScopedVfs;
        std::string ScopedVfsError;
        Check(ScopedVfs.MountDirectory(ScopedDirectory.string(), 0, &ScopedVfsError), "temporary text directory mounts");
        Text::MappedTextAsset ScopedAsset;
        std::string ScopedError;
        Check(ScopedAsset.Open(ScopedVfs, "story.fbytext", &ScopedError), "mmap text asset opens");
        Check(ScopedAsset.IsOpen(), "mmap text asset is open");
        Check(ScopedAsset.View().Text == ScopedSource, "mmap text keeps source bytes");
        Check(ScopedAsset.View().Header.BlockCount > 1, "mmap text keeps block table");
        Check(ScopedAsset.Block(0).size() > 0, "mmap text block reads");
        ScopedAsset.Close();
        Check(!ScopedAsset.IsOpen(), "mmap text asset closes");
    }

    std::filesystem::remove_all(ScopedDirectory, ScopedErrorCode);

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d text asset file test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("text asset file tests: ok\n");
    return 0;
}
