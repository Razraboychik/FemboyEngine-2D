#include "Engine/Core/Calendar.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/StoragePaths.hpp"
#include "Engine/Core/Time.hpp"

#include "Engine/Core/Assert.hpp"

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <span>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

namespace Fby
{
namespace
{
std::atomic<LogLevel> GLevel{LogLevel::Info};
std::atomic<LogObserver> GObserver{nullptr};
std::mutex GSinkMutex;

// File sink state. The mutex is separate from the console one so a slow disk
// cannot block a console write for another thread.
std::mutex GFileMutex;
std::FILE* GFile = nullptr;
std::string GCategory = "engine";
std::string GFilePath;
LogRetention GRetention{};
std::uint64_t GLineCount = 0;
bool GCompactionFailed = false;

constexpr std::string_view LevelTag(LogLevel InLevel) noexcept
{
    switch (InLevel)
    {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warn:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
    }
    return "?????";
}

// Counts newline characters in a file. The sink always terminates a line with
// a newline, so this is the line count it wrote.
[[nodiscard]] std::uint64_t CountLinesInFile(const std::filesystem::path& InPath) noexcept
{
    std::ifstream ScopedStream(InPath, std::ios::binary);
    if (!ScopedStream)
    {
        return 0;
    }
    std::uint64_t ScopedLines = 0;
    char ScopedBuffer[64 * 1024];
    while (ScopedStream)
    {
        ScopedStream.read(ScopedBuffer, sizeof(ScopedBuffer));
        const std::streamsize ScopedRead = ScopedStream.gcount();
        if (ScopedRead <= 0)
        {
            break;
        }
        for (std::streamsize ScopedIndex = 0; ScopedIndex < ScopedRead; ++ScopedIndex)
        {
            if (ScopedBuffer[ScopedIndex] == '\n')
            {
                ++ScopedLines;
            }
        }
    }
    return ScopedLines;
}

// Keeps the category directory at MaxFiles - 1 files, which leaves room for
// the file this run is about to create. The current file must be closed first:
// deleting an open file behaves differently on each platform.
void PruneCategoryLocked() noexcept
{
    const std::filesystem::path ScopedDirectory = Core::LogDirectory(GCategory);
    std::error_code ScopedError;
    if (!std::filesystem::is_directory(ScopedDirectory, ScopedError))
    {
        return;
    }
    const std::uint32_t ScopedKeep = GRetention.MaxFiles > 0 ? GRetention.MaxFiles - 1 : 0;
    Core::PruneOldFiles(ScopedDirectory, GCategory + "-", ScopedKeep);
}

// Drops the oldest lines of the open file until it is back under the limit.
// Ninety percent is kept so the following writes do not compact on every line.
[[nodiscard]] bool TrimLogFileLocked() noexcept
{
    if (GFile == nullptr || GFilePath.empty())
    {
        return true;
    }
    const std::uint64_t ScopedLimit = std::max<std::uint64_t>(GRetention.MaxLinesPerFile, 1);
    if (GLineCount <= ScopedLimit)
    {
        return true;
    }
    std::fflush(GFile);
    std::fclose(GFile);
    GFile = nullptr;

    const std::filesystem::path ScopedPath(GFilePath);
    const std::uint64_t ScopedKeep = std::max<std::uint64_t>(1, ScopedLimit - ScopedLimit / 10);
    const std::uint64_t ScopedSkip = GLineCount > ScopedKeep ? GLineCount - ScopedKeep : 0;
    std::filesystem::path ScopedTemp = ScopedPath;
    ScopedTemp += ".trim";

    std::uint64_t ScopedWritten = 0;
    bool ScopedRewritten = false;
    {
        std::ifstream ScopedInput(ScopedPath, std::ios::binary);
        std::ofstream ScopedOutput(ScopedTemp, std::ios::binary | std::ios::trunc);
        if (ScopedInput && ScopedOutput)
        {
            std::string ScopedLine;
            std::uint64_t ScopedIndex = 0;
            ScopedRewritten = true;
            while (std::getline(ScopedInput, ScopedLine))
            {
                if (ScopedIndex < ScopedSkip)
                {
                    ++ScopedIndex;
                    continue;
                }
                ScopedOutput.write(ScopedLine.data(), static_cast<std::streamsize>(ScopedLine.size()));
                ScopedOutput.put('\n');
                if (!ScopedOutput)
                {
                    ScopedRewritten = false;
                    break;
                }
                ++ScopedIndex;
                ++ScopedWritten;
            }
            if (ScopedInput.bad())
            {
                ScopedRewritten = false;
            }
        }
    }

    std::error_code ScopedError;
    if (ScopedRewritten)
    {
        std::filesystem::rename(ScopedTemp, ScopedPath, ScopedError);
        if (ScopedError)
        {
            // Windows does not replace an existing file with rename().
            ScopedError.clear();
            std::filesystem::remove(ScopedPath, ScopedError);
            ScopedError.clear();
            std::filesystem::rename(ScopedTemp, ScopedPath, ScopedError);
        }
        if (ScopedError)
        {
            // The original may already be gone; put the rewritten copy back so
            // the log is not lost.
            ScopedError.clear();
            std::filesystem::rename(ScopedTemp, ScopedPath, ScopedError);
            if (!ScopedError)
            {
                ScopedRewritten = true;
            }
        }
    }
    if (!ScopedRewritten)
    {
        ScopedError.clear();
        std::filesystem::remove(ScopedTemp, ScopedError);
    }

    GFile = std::fopen(GFilePath.c_str(), "ab");
    if (GFile == nullptr)
    {
        GLineCount = 0;
        return false;
    }
    if (ScopedRewritten)
    {
        GLineCount = ScopedWritten;
    }
    return ScopedRewritten;
}

// Closes the open file, prunes the category directory and reopens the same
// file. Used when the limits change at runtime.
void ApplyRetentionLocked() noexcept
{
    const bool ScopedHadPath = !GFilePath.empty();
    const std::string ScopedPath = GFilePath;
    if (GFile != nullptr)
    {
        std::fflush(GFile);
        std::fclose(GFile);
        GFile = nullptr;
    }
    GLineCount = 0;
    GCompactionFailed = false;
    PruneCategoryLocked();
    if (!ScopedHadPath)
    {
        return;
    }
    GFile = std::fopen(ScopedPath.c_str(), "ab");
    if (GFile == nullptr)
    {
        GFilePath.clear();
        return;
    }
    GLineCount = CountLinesInFile(ScopedPath);
    if (GLineCount > GRetention.MaxLinesPerFile && !TrimLogFileLocked())
    {
        GCompactionFailed = true;
    }
}

// Opens the category log file on first use and prunes the older ones.
void EnsureLogFileLocked() noexcept
{
    if (GFile != nullptr)
    {
        return;
    }
    // An explicit opt-out keeps the file sink away from a run that only wants
    // the console, for example a probe or a test harness.
    if (const char* ScopedEnabled = std::getenv("FBY_LOG_FILE");
        ScopedEnabled != nullptr && std::string_view(ScopedEnabled) == "0")
    {
        return;
    }
    const std::filesystem::path ScopedDirectory = Core::LogDirectory(GCategory);
    if (!Core::EnsureDirectory(ScopedDirectory))
    {
        return;
    }
    GLineCount = 0;
    GCompactionFailed = false;
    PruneCategoryLocked();
    const std::string ScopedPrefix = GCategory + "-";
    GFilePath = (ScopedDirectory / (ScopedPrefix + Core::TimestampForPath() + ".log")).string();
    GFile = std::fopen(GFilePath.c_str(), "ab");
    if (GFile == nullptr)
    {
        return;
    }
    GLineCount = CountLinesInFile(GFilePath);
    if (GLineCount > GRetention.MaxLinesPerFile && !TrimLogFileLocked())
    {
        GCompactionFailed = true;
    }
}
}

void SetLogLevel(LogLevel InLevel) noexcept
{
    GLevel.store(InLevel, std::memory_order_relaxed);
}

LogLevel GetLogLevel() noexcept
{
    return GLevel.load(std::memory_order_relaxed);
}

void SetLogRetention(const LogRetention& InRetention) noexcept
{
    const std::scoped_lock ScopedLock(GFileMutex);
    GRetention.MaxFiles = std::clamp(InRetention.MaxFiles, 1u, 512u);
    GRetention.MaxLinesPerFile = std::clamp(InRetention.MaxLinesPerFile, 1000u, 2000000u);
    ApplyRetentionLocked();
}

LogRetention GetLogRetention() noexcept
{
    const std::scoped_lock ScopedLock(GFileMutex);
    return GRetention;
}

void SetLogObserver(LogObserver InObserver) noexcept
{
    GObserver.store(InObserver, std::memory_order_release);
}

void SetLogCategory(std::string_view InCategory) noexcept
{
    const std::scoped_lock ScopedLock(GFileMutex);
    if (GFile != nullptr)
    {
        std::fclose(GFile);
        GFile = nullptr;
    }
    GFilePath.clear();
    GLineCount = 0;
    GCompactionFailed = false;
    GCategory.assign(InCategory);
}

void CloseLogFile() noexcept
{
    const std::scoped_lock ScopedLock(GFileMutex);
    if (GFile != nullptr)
    {
        std::fclose(GFile);
        GFile = nullptr;
    }
    GFilePath.clear();
    GLineCount = 0;
    GCompactionFailed = false;
}

std::string LogCategory()
{
    const std::scoped_lock ScopedLock(GFileMutex);
    return GCategory;
}

std::string CurrentLogFilePath()
{
    const std::scoped_lock ScopedLock(GFileMutex);
    return GFilePath;
}

namespace
{
// Windows consoles default to the OEM code page, which turns UTF-8 log text
void EnsureConsoleUtf8() noexcept
{
#if defined(_WIN32)
    static const bool ScopedInitialized = []()
    {
        ::SetConsoleOutputCP(CP_UTF8);
        return true;
    }();
    (void)ScopedInitialized;
#endif
}
}

void LogLine(LogLevel InLevel, std::string_view InChannel, std::string_view InMessage) noexcept
{
    if (InLevel < GetLogLevel())
    {
        return;
    }
    EnsureConsoleUtf8();

    // One timestamp per line: local wall clock with milliseconds. The calendar
    // module formats it into a fixed buffer, so no platform time structure and
    // no locale are involved.
    const Fby::Core::DateTime ScopedStamp =
        Fby::Core::DateTimeFromUnixSeconds(Fby::Core::Clock::UnixSecondsDouble(), Fby::Core::LocalOffsetMinutes());
    char ScopedClockText[32];
    char ScopedPrefix[48];
    (void)Fby::Core::FormatClock24(ScopedStamp.Time, std::span<char>(ScopedClockText));
    std::snprintf(ScopedPrefix, sizeof(ScopedPrefix), "[%.*s.%03u]", static_cast<int>(std::strlen(ScopedClockText)),
                  ScopedClockText, static_cast<unsigned>(ScopedStamp.Time.Millisecond));

    {
        const std::scoped_lock ScopedLock(GSinkMutex);
        std::fprintf(stderr, "%s [%s] %.*s: %.*s\n", ScopedPrefix, LevelTag(InLevel).data(),
                     static_cast<int>(InChannel.size()), InChannel.data(), static_cast<int>(InMessage.size()),
                     InMessage.data());
        std::fflush(stderr);
    }
    {
        const std::scoped_lock ScopedLock(GFileMutex);
        EnsureLogFileLocked();
        if (GFile != nullptr)
        {
            std::fprintf(GFile, "%s [%s] %.*s: %.*s\n", ScopedPrefix, LevelTag(InLevel).data(),
                         static_cast<int>(InChannel.size()), InChannel.data(), static_cast<int>(InMessage.size()),
                         InMessage.data());
            std::fflush(GFile);
            ++GLineCount;
            if (!GCompactionFailed && GLineCount > GRetention.MaxLinesPerFile)
            {
                if (!TrimLogFileLocked())
                {
                    GCompactionFailed = true;
                }
            }
        }
    }

    // The observer runs outside the sink lock so a console/LogBoy consumer can
    if (const LogObserver ScopedObserver = GObserver.load(std::memory_order_acquire); ScopedObserver != nullptr)
    {
        ScopedObserver(InLevel, InChannel, InMessage);
    }

    if (InLevel == LogLevel::Fatal)
    {
        std::abort();
    }
}

void FailAssert(const char* InExpression, const char* InFile, int InLine, const char* InMessage) noexcept
{
    // The assertion must also land in the log file: a shipped editor or game has
    // no console, so stderr alone would lose the only explanation of the crash.
    LogError("assert", "{}:{}: {} {}", InFile, InLine, InExpression != nullptr ? InExpression : "<none>",
             InMessage != nullptr ? InMessage : "");
    std::fprintf(stderr, "\n*** FemboyEngine assertion failed ***\n  %s:%d\n  expression: %s\n", InFile, InLine,
                 InExpression != nullptr ? InExpression : "<none>");
    if (InMessage != nullptr)
    {
        std::fprintf(stderr, "  message: %s\n", InMessage);
    }
    std::fflush(stderr);

#if defined(_MSC_VER)
    __debugbreak();
#endif
    std::abort();
}
}
