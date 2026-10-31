// FemboyEngine2D - logging.
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Fby
{

enum class LogLevel : std::uint8_t
{
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal
};

// Set once at startup (--verbose, --quiet). Thread safe to read.
void SetLogLevel(LogLevel InLevel) noexcept;
[[nodiscard]] LogLevel GetLogLevel() noexcept;

// Writes one fully formatted line. Prefer the templated helpers below.
void LogLine(LogLevel InLevel, std::string_view InChannel, std::string_view InMessage) noexcept;

// Optional observer for the editor console, LogBoy and a crash-dump writer.
using LogObserver = void (*)(LogLevel InLevel, std::string_view InChannel, std::string_view InMessage);
void SetLogObserver(LogObserver InObserver) noexcept;

// File sink. Every accepted line is appended to
// Saved/Logs/<category>/<category>-YYYYMMDD-HHMMSS.log; the category is the
// application kind ("engine", "editor", "server", ...). Set it before the first
// line, because the directory decides where the file opens. FBY_LOG_FILE=0
// disables the sink for a run; FBY_STORAGE_DIR moves Saved/.
void SetLogCategory(std::string_view InCategory) noexcept;
// Closes the sink file. A test or a short-lived probe calls this before it
// removes its fixture directory, because Windows cannot delete an open file.
void CloseLogFile() noexcept;
[[nodiscard]] std::string LogCategory();
[[nodiscard]] std::string CurrentLogFilePath();

template <typename... Args>
void Log(LogLevel InLevel, std::string_view InChannel, std::format_string<Args...> InFmt, Args&&... InArgs)
{
    if (InLevel < GetLogLevel())
    {
        return;
    }
    // Build the line first: std::format allocates, so it must not happen while
    LogLine(InLevel, InChannel, std::format(InFmt, std::forward<Args>(InArgs)...));
}

template <typename... Args>
void LogInfo(std::string_view InChannel, std::format_string<Args...> InFmt, Args&&... InArgs)
{
    Log(LogLevel::Info, InChannel, InFmt, std::forward<Args>(InArgs)...);
}

template <typename... Args>
void LogWarn(std::string_view InChannel, std::format_string<Args...> InFmt, Args&&... InArgs)
{
    Log(LogLevel::Warn, InChannel, InFmt, std::forward<Args>(InArgs)...);
}

template <typename... Args>
void LogError(std::string_view InChannel, std::format_string<Args...> InFmt, Args&&... InArgs)
{
    Log(LogLevel::Error, InChannel, InFmt, std::forward<Args>(InArgs)...);
}
}
