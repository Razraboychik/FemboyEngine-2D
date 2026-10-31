// FemboyEngine2D - crash diagnostics.
//
// InstallCrashHandler arms the platform handlers once per process. On Windows an
// unhandled exception filter writes a text report and a minidump; on Linux and
// macOS the fatal signals do the same with a backtrace. Both write under
// Saved/Crashes/<stamp>-<pid>/ and flush before the process dies, so a crash
// leaves an artefact a user can attach to a report.
#pragma once

#include <cstdint>
#include <string_view>

namespace Fby::Core
{

// Arms the handlers. InApplicationName lands in the report and the file header.
void InstallCrashHandler(std::string_view InApplicationName) noexcept;
void UninstallCrashHandler() noexcept;
[[nodiscard]] bool CrashHandlerInstalled() noexcept;

// Writes one report on demand and returns the directory it created. Useful for
// an "unrecoverable state" path that is not a signal.
[[nodiscard]] bool WriteCrashReport(std::string_view InApplicationName, std::string_view InReason,
                                    std::string_view InDetails = {}) noexcept;

// Directory of the last report written by this process, empty before the first.
[[nodiscard]] std::string_view LastCrashDirectory() noexcept;
}
