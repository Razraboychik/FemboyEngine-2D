// FemboyEngine2D - pak-backed VFS entries cannot be invalidated.
#include "Engine/Assets/FempakWriter.hpp"
#include "Engine/Assets/VFS.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
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

    const std::filesystem::path ScopedPakPath = "_fby_vfs_pak_test.fempak";
    std::error_code ScopedRemoveError;
    std::filesystem::remove(ScopedPakPath, ScopedRemoveError);

    Assets::FempakWriter ScopedWriter;
    const std::array<std::byte, 11> ScopedBytes{std::byte{'p'}, std::byte{'a'}, std::byte{'k'}, std::byte{' '},
                                                std::byte{'c'}, std::byte{'o'}, std::byte{'n'}, std::byte{'t'},
                                                std::byte{'e'}, std::byte{'n'}, std::byte{'t'}};
    ScopedWriter.Add("textures/a.txt", std::span<const std::byte>(ScopedBytes.data(), ScopedBytes.size()));
    const std::vector<std::byte> ScopedArchive = ScopedWriter.Build();
    Check(!ScopedArchive.empty(), "the pak builder produces bytes");
    std::string ScopedError;
    Check(Assets::WriteFileAtomically(ScopedPakPath.string(), ScopedArchive, &ScopedError),
          "the pak is written to disk");

    VirtualFileSystem ScopedVfs;
    Check(ScopedVfs.MountPak(ScopedPakPath.string(), 0, &ScopedError), "the pak mounts");
    const auto ScopedRead = ScopedVfs.Read("textures/a.txt");
    Check(ScopedRead.has_value() && ScopedRead->size() == ScopedBytes.size(), "a pak entry is readable");
    Check(!ScopedVfs.InvalidateFile("textures/a.txt"),
          "invalidating a pak-backed entry is refused");
    const auto ScopedAfter = ScopedVfs.Read("textures/a.txt");
    Check(ScopedAfter.has_value() && ScopedAfter->size() == ScopedBytes.size(),
          "the pak entry survives the refused invalidation");

    // A directory mount still participates in invalidation: the refusal above is
    // specific to immutable pak entries, not a general no-op.
    const std::filesystem::path ScopedDirectory = "_fby_vfs_dir_test";
    std::filesystem::create_directories(ScopedDirectory, ScopedRemoveError);
    {
        std::FILE* ScopedFile = std::fopen((ScopedDirectory / "b.txt").string().c_str(), "wb");
        if (ScopedFile != nullptr)
        {
            (void)std::fwrite("loose", 1, 5, ScopedFile);
            std::fclose(ScopedFile);
        }
    }
    Check(ScopedVfs.MountDirectory(ScopedDirectory.string(), 1, &ScopedError), "the directory mounts");
    Check(ScopedVfs.Read("b.txt").has_value(), "a loose file is readable");
    Check(ScopedVfs.InvalidateFile("b.txt"), "a directory-backed entry can be invalidated");
    Check(!ScopedVfs.Read("b.txt").has_value(), "the invalidated loose file disappears from the index");

    std::filesystem::remove_all(ScopedDirectory, ScopedRemoveError);
    std::filesystem::remove(ScopedPakPath, ScopedRemoveError);

    if (GFailures == 0)
    {
        std::printf("vfs_pak: OK\n");
        return 0;
    }
    std::fprintf(stderr, "vfs_pak: %d failure(s)\n", GFailures);
    return 1;
}
