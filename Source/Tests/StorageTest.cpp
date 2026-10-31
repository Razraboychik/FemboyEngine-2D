// FemboyEngine2D - runtime storage layout, log file sink and crash reports.
#include "Engine/Core/CrashHandler.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/StoragePaths.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

[[nodiscard]] bool FileContains(const std::filesystem::path& InPath, std::string_view InNeedle)
{
    std::ifstream ScopedFile(InPath, std::ios::binary);
    if (!ScopedFile)
    {
        return false;
    }
    const std::string ScopedText((std::istreambuf_iterator<char>(ScopedFile)), std::istreambuf_iterator<char>());
    return ScopedText.find(InNeedle) != std::string::npos;
}

void SetStorageEnvironment(const std::string& InPath)
{
#if defined(_WIN32)
    (void)_putenv_s("FBY_STORAGE_DIR", InPath.c_str());
#else
    (void)setenv("FBY_STORAGE_DIR", InPath.c_str(), 1);
#endif
}

[[nodiscard]] std::size_t CountPrefix(const std::filesystem::path& InDirectory, std::string_view InPrefix)
{
    std::size_t ScopedCount = 0;
    std::error_code ScopedError;
    for (const std::filesystem::directory_entry& ScopedEntry :
         std::filesystem::directory_iterator(InDirectory, ScopedError))
    {
        if (ScopedError)
        {
            break;
        }
        if (ScopedEntry.is_regular_file(ScopedError) &&
            ScopedEntry.path().filename().string().rfind(InPrefix, 0) == 0)
        {
            ++ScopedCount;
        }
    }
    return ScopedCount;
}
}

int main()
{
    namespace FileSystem = std::filesystem;
    // The fixture lives inside the build tree: a test must not depend on the
    // platform temp directory being writable under a sandbox.
    const FileSystem::path ScopedRoot =
        FileSystem::current_path() / "build" /
        ("_storage_test_" + std::to_string(static_cast<unsigned long long>(std::rand())));
    FileSystem::remove_all(ScopedRoot);
    SetStorageEnvironment(ScopedRoot.string());
    // The test installs the handler it is testing: a later crash leaves a report
    // in the fixture directory instead of a silent non-zero exit.
    Fby::Core::InstallCrashHandler("storage_test");

    // Optional crash self-check, used by hand to prove the handler writes a
    // report: the process ends with the handler's exit code, so ctest never
    // runs it without this argument.
    if (std::getenv("FBY_CRASH_SELF_CHECK") != nullptr)
    {
        Fby::SetLogCategory("crashcheck");
        Fby::LogInfo("storage", "about to fault on purpose");
        volatile int* ScopedNull = nullptr;
        *ScopedNull = 1;
    }

    Check(Fby::Core::StorageRoot() == ScopedRoot, "the storage root follows FBY_STORAGE_DIR");
    Check(Fby::Core::LogDirectory("engine") == ScopedRoot / "Logs" / "engine", "the log directory is category scoped");
    Check(Fby::Core::CrashRoot() == ScopedRoot / "Crashes", "crash reports live under Saved/Crashes");
    Check(Fby::Core::ConfigDirectory() == ScopedRoot / "Config", "editor settings live under Saved/Config");
    Check(Fby::Core::CacheDirectory() == ScopedRoot / "Cache", "caches live under Saved/Cache");
    {
        std::error_code ScopedDirectoryError;
        FileSystem::create_directories(Fby::Core::LogDirectory("engine"), ScopedDirectoryError);
        if (ScopedDirectoryError)
        {
            std::fprintf(stderr, "NOTE: cannot create %s: %s\n",
                         Fby::Core::LogDirectory("engine").string().c_str(), ScopedDirectoryError.message().c_str());
        }
    }
    Check(FileSystem::is_directory(Fby::Core::LogDirectory("engine")), "the log directory is created on demand");

    const std::string ScopedStamp = Fby::Core::TimestampForPath();
    Check(ScopedStamp.size() == 15 && ScopedStamp[8] == '-', "the file stamp is YYYYMMDD-HHMMSS");

    // The log sink writes every accepted line into the category file.
    Fby::SetLogCategory("selftest");
    Check(Fby::LogCategory() == "selftest", "the category is stored");
    Fby::LogInfo("storage", "file sink line {}", 42);
    const std::string ScopedLogPath = Fby::CurrentLogFilePath();
    Check(!ScopedLogPath.empty(), "the sink reports its file");
    const FileSystem::path ScopedLogFile(ScopedLogPath);
    Check(FileSystem::exists(ScopedLogFile), "the log file exists");
    Check(ScopedLogFile.parent_path() == Fby::Core::LogDirectory("selftest"), "the log file sits in its category");
    Check(FileContains(ScopedLogFile, "file sink line 42"), "the log file carries the formatted line");
    Check(FileContains(ScopedLogFile, "[INFO ]"), "the log file carries the level tag");

    // Writing a crash report creates the session directory and the text report.
    Check(Fby::Core::WriteCrashReport("storage_test", "unit test reason", "unit test details"),
          "a crash report is written on demand");
    const std::string ScopedCrashDirectory(Fby::Core::LastCrashDirectory());
    Check(!ScopedCrashDirectory.empty(), "the session directory is remembered");
    Check(FileSystem::exists(FileSystem::path(ScopedCrashDirectory) / "report.txt"), "the report file exists");
    Check(FileContains(FileSystem::path(ScopedCrashDirectory) / "report.txt", "unit test reason"),
          "the report carries the reason");
    Check(FileContains(FileSystem::path(ScopedCrashDirectory) / "report.txt", "storage_test"),
          "the report carries the application name");

    // Rotation keeps the newest files of the category and drops the rest.
    Fby::SetLogCategory("rotate");
    const FileSystem::path ScopedRotateDirectory = Fby::Core::LogDirectory("rotate");
    Check(Fby::Core::EnsureDirectory(ScopedRotateDirectory), "the rotation directory exists");
    for (int ScopedIndex = 0; ScopedIndex < 25; ++ScopedIndex)
    {
        std::ofstream ScopedOld(ScopedRotateDirectory / ("rotate-old-" + std::to_string(ScopedIndex) + ".log"));
        ScopedOld << "old\n";
    }
    Check(CountPrefix(ScopedRotateDirectory, "rotate-") == 25, "the rotation fixture is in place");
    Fby::LogInfo("storage", "rotation trigger");
    Check(CountPrefix(ScopedRotateDirectory, "rotate-") == 20, "the sink keeps twenty files per category");

    // A one-off cleanup test directory is not part of the deliverable. The sink
    // file must be closed first: Windows refuses to delete an open file.
    Fby::CloseLogFile();
    std::error_code ScopedCleanupError;
    FileSystem::remove_all(ScopedRoot, ScopedCleanupError);
    Check(!ScopedCleanupError, "the fixture directory is removed");

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d storage test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("storage tests: ok\n");
    return 0;
}
