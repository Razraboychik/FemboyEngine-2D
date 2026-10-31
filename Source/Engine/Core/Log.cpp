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

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <span>
#include <format>
#include <mutex>
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
constexpr std::uint32_t kKeepLogFiles = 20;
std::mutex GFileMutex;
std::FILE* GFile = nullptr;
std::string GCategory = "engine";
std::string GFilePath;

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
    const std::string ScopedPrefix = GCategory + "-";
    Core::PruneOldFiles(ScopedDirectory, ScopedPrefix, kKeepLogFiles - 1);
    GFilePath = (ScopedDirectory / (ScopedPrefix + Core::TimestampForPath() + ".log")).string();
    GFile = std::fopen(GFilePath.c_str(), "ab");
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
