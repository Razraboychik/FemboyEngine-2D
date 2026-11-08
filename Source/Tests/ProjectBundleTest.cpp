// FemboyEngine2D - portable project bundle round-trip.
#include "Engine/Assets/ProjectBundle.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
void Write(const std::filesystem::path& InPath, const char* InText)
{
    std::filesystem::create_directories(InPath.parent_path());
    std::ofstream ScopedFile(InPath, std::ios::binary | std::ios::trunc);
    ScopedFile << InText;
}
}

int main()
{
    const std::filesystem::path ScopedRoot = std::filesystem::current_path() / "project_bundle_test";
    const std::filesystem::path ScopedArchive = std::filesystem::current_path() / "project_bundle_test.fbyproj";
    const std::filesystem::path ScopedImported = std::filesystem::current_path() / "project_bundle_imported";
    std::error_code ScopedError;
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::remove_all(ScopedImported, ScopedError);
    std::filesystem::remove(ScopedArchive, ScopedError);
    Write(ScopedRoot / "project.femproj", "format_version = 4\n");
    Write(ScopedRoot / "assets/hello.txt", "portable");
    Write(ScopedRoot / "build/ignored.txt", "ignored");
    Write(ScopedRoot / "Saved/ignored.txt", "ignored");
    Write(ScopedRoot / "cooked.fempak", "ignored");

    std::string ScopedErrorText;
    if (!Fby::Assets::ExportProjectBundle(ScopedRoot, ScopedArchive, &ScopedErrorText))
    {
        std::fprintf(stderr, "project_bundle: export failed: %s\n", ScopedErrorText.c_str());
        return 1;
    }
    const std::filesystem::path ScopedSecondArchive = std::filesystem::current_path() / "project_bundle_test2.fbyproj";
    std::filesystem::remove(ScopedSecondArchive, ScopedError);
    if (!Fby::Assets::ExportProjectBundle(ScopedRoot, ScopedSecondArchive, &ScopedErrorText))
    {
        std::fprintf(stderr, "project_bundle: second export failed: %s\n", ScopedErrorText.c_str());
        return 1;
    }
    const auto ScopedReadBytes = [](const std::filesystem::path& InPath, std::vector<char>* Out)
    {
        std::ifstream ScopedFile(InPath, std::ios::binary);
        Out->assign(std::istreambuf_iterator<char>(ScopedFile), std::istreambuf_iterator<char>());
    };
    std::vector<char> ScopedFirstBytes;
    std::vector<char> ScopedSecondBytes;
    ScopedReadBytes(ScopedArchive, &ScopedFirstBytes);
    ScopedReadBytes(ScopedSecondArchive, &ScopedSecondBytes);
    if (ScopedFirstBytes != ScopedSecondBytes)
    {
        std::fprintf(stderr, "project_bundle: export is not deterministic\n");
        return 1;
    }
    if (!ScopedSecondBytes.empty())
    {
        ScopedSecondBytes.back() = static_cast<char>(ScopedSecondBytes.back() ^ 0x5A);
        std::ofstream ScopedCorrupt(ScopedSecondArchive, std::ios::binary | std::ios::trunc);
        ScopedCorrupt.write(ScopedSecondBytes.data(), static_cast<std::streamsize>(ScopedSecondBytes.size()));
        ScopedCorrupt.close();
        const std::filesystem::path ScopedCorruptRoot = std::filesystem::current_path() / "project_bundle_corrupt";
        std::filesystem::remove_all(ScopedCorruptRoot, ScopedError);
        if (Fby::Assets::ImportProjectBundle(ScopedSecondArchive, ScopedCorruptRoot, false, &ScopedErrorText))
        {
            std::fprintf(stderr, "project_bundle: corrupted archive accepted\n");
            return 1;
        }
        std::filesystem::remove_all(ScopedCorruptRoot, ScopedError);
    }
    if (!Fby::Assets::ImportProjectBundle(ScopedArchive, ScopedImported, false, &ScopedErrorText))
    {
        std::fprintf(stderr, "project_bundle: import failed: %s\n", ScopedErrorText.c_str());
        return 1;
    }
    if (!std::filesystem::is_regular_file(ScopedImported / "project.femproj") ||
        !std::filesystem::is_regular_file(ScopedImported / "assets/hello.txt") ||
        std::filesystem::exists(ScopedImported / "build/ignored.txt") ||
        std::filesystem::exists(ScopedImported / "cooked.fempak"))
    {
        std::fprintf(stderr, "project_bundle: round-trip content failed\n");
        return 1;
    }
    if (Fby::Assets::ImportProjectBundle(ScopedArchive, ScopedImported, false, &ScopedErrorText))
    {
        std::fprintf(stderr, "project_bundle: non-empty destination accepted\n");
        return 1;
    }
    std::filesystem::remove_all(ScopedRoot, ScopedError);
    std::filesystem::remove_all(ScopedImported, ScopedError);
    std::filesystem::remove(ScopedArchive, ScopedError);
    std::filesystem::remove(ScopedSecondArchive, ScopedError);
    std::printf("project_bundle: OK (export, import, exclusions, non-empty guard)\n");
    return 0;
}
