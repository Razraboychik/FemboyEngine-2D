// FemboyEngine2D - runtime storage layout.
//
// Everything the engine writes while it runs lives under one storage root, so a
// project directory stays free of stray logs, dumps and caches:
//
//   Saved/Logs/<category>/<category>-YYYYMMDD-HHMMSS.log
//   Saved/Crashes/<YYYYMMDD-HHMMSS>-<pid>/report.txt
//   Saved/Config/editor.ini
//   Saved/Cache/
//
// FBY_STORAGE_DIR moves the root; the default is <working directory>/Saved.
#pragma once

#include "Engine/Core/Calendar.hpp"
#include "Engine/Core/Time.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Core
{

[[nodiscard]] inline std::filesystem::path StorageRoot()
{
    if (const char* ScopedEnvironment = std::getenv("FBY_STORAGE_DIR");
        ScopedEnvironment != nullptr && ScopedEnvironment[0] != '\0')
    {
        return std::filesystem::path(ScopedEnvironment);
    }
    return std::filesystem::current_path() / "Saved";
}

[[nodiscard]] inline std::filesystem::path LogDirectory(std::string_view InCategory)
{
    return StorageRoot() / "Logs" / std::string(InCategory);
}

[[nodiscard]] inline std::filesystem::path CrashRoot()
{
    return StorageRoot() / "Crashes";
}

[[nodiscard]] inline std::filesystem::path ConfigDirectory()
{
    return StorageRoot() / "Config";
}

[[nodiscard]] inline std::filesystem::path CacheDirectory()
{
    return StorageRoot() / "Cache";
}

[[nodiscard]] inline bool EnsureDirectory(const std::filesystem::path& InPath)
{
    std::error_code ScopedError;
    std::filesystem::create_directories(InPath, ScopedError);
    return std::filesystem::is_directory(InPath, ScopedError);
}

// YYYYMMDD-HHMMSS in the local zone, which is also the name component of a log
// file and of a crash directory.
[[nodiscard]] inline std::string TimestampForPath()
{
    const DateTime ScopedNow = DateTimeFromUnixSeconds(Clock::UnixSeconds(), LocalOffsetMinutes());
    return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}", ScopedNow.Date.Year, ScopedNow.Date.Month,
                       ScopedNow.Date.Day, ScopedNow.Time.Hour, ScopedNow.Time.Minute, ScopedNow.Time.Second);
}

// Keeps the newest InKeep files whose name starts with InPrefix and deletes the
// rest. The comparison uses the last write time, so a clock jump in the name
// cannot preserve an outdated file.
inline void PruneOldFiles(const std::filesystem::path& InDirectory, std::string_view InPrefix,
                          std::uint32_t InKeep)
{
    std::error_code ScopedError;
    std::vector<std::filesystem::directory_entry> ScopedEntries;
    for (const std::filesystem::directory_entry& ScopedEntry :
         std::filesystem::directory_iterator(InDirectory, ScopedError))
    {
        if (ScopedError)
        {
            return;
        }
        if (!ScopedEntry.is_regular_file(ScopedError))
        {
            continue;
        }
        const std::string ScopedName = ScopedEntry.path().filename().string();
        if (ScopedName.rfind(InPrefix, 0) == 0)
        {
            ScopedEntries.push_back(ScopedEntry);
        }
    }
    if (ScopedEntries.size() <= InKeep)
    {
        return;
    }
    std::sort(ScopedEntries.begin(), ScopedEntries.end(),
              [](const std::filesystem::directory_entry& InLeft, const std::filesystem::directory_entry& InRight)
              {
                  std::error_code ScopedLeftError;
                  std::error_code ScopedRightError;
                  const auto ScopedLeft = InLeft.last_write_time(ScopedLeftError);
                  const auto ScopedRight = InRight.last_write_time(ScopedRightError);
                  if (ScopedLeftError || ScopedRightError)
                  {
                      return InLeft.path().filename().string() < InRight.path().filename().string();
                  }
                  return ScopedLeft < ScopedRight;
              });
    const std::size_t ScopedRemove = ScopedEntries.size() - InKeep;
    for (std::size_t ScopedIndex = 0; ScopedIndex < ScopedRemove; ++ScopedIndex)
    {
        std::filesystem::remove(ScopedEntries[ScopedIndex].path(), ScopedError);
    }
}
}
