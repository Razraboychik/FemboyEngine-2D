// FemboyEngine2D - profiler panel data model (rows, sorting, formatting).
#pragma once

#include "Engine/Core/Profiler.hpp"

#include <algorithm>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby::Core
{

enum class ProfileSortMode : std::uint8_t
{
    Total = 0,
    Average,
    P95,
    Max,
};

// One rendered row of the profiler panel. The name is copied because the panel
// outlives the string views the caller passes in.
struct ProfileRow
{
    std::string Name;
    std::uint64_t Count = 0;
    double TotalMs = 0.0;
    double AverageMs = 0.0;
    double P95Ms = 0.0;
    double MaxMs = 0.0;
};

// Turns a Profiler into the rows a panel draws: categories with samples only,
// sorted by the active metric, capped to a row limit. The cap is what keeps the
// panel readable; the hidden count is reported instead of silently dropped.
class ProfilerPanel
{
public:
    void SetMaxRows(std::uint32_t InMaxRows) noexcept
    {
        MMaxRows = std::max(InMaxRows, 1u);
    }
    void SetSortMode(ProfileSortMode InMode) noexcept
    {
        MSortMode = InMode;
    }
    [[nodiscard]] ProfileSortMode SortMode() const noexcept
    {
        return MSortMode;
    }

    void Update(const Profiler& InProfiler, std::span<const std::string_view> InNames)
    {
        MRows.clear();
        MHidden = 0;
        for (std::uint32_t ScopedCategory = 0; ScopedCategory < InNames.size(); ++ScopedCategory)
        {
            const ProfileStats ScopedStats = InProfiler.Stats(ScopedCategory);
            if (ScopedStats.Count == 0 || InNames[ScopedCategory].empty())
            {
                continue;
            }
            ProfileRow ScopedRow;
            ScopedRow.Name = std::string(InNames[ScopedCategory]);
            ScopedRow.Count = ScopedStats.Count;
            ScopedRow.TotalMs = ScopedStats.TotalMs;
            ScopedRow.AverageMs = ScopedStats.AverageMs;
            ScopedRow.P95Ms = ScopedStats.P95Ms;
            ScopedRow.MaxMs = ScopedStats.MaxMs;
            MRows.push_back(std::move(ScopedRow));
        }
        const auto ScopedLess = [this](const ProfileRow& InLeft, const ProfileRow& InRight)
        {
            double ScopedLeft = InLeft.TotalMs;
            double ScopedRight = InRight.TotalMs;
            switch (MSortMode)
            {
            case ProfileSortMode::Average:
                ScopedLeft = InLeft.AverageMs;
                ScopedRight = InRight.AverageMs;
                break;
            case ProfileSortMode::P95:
                ScopedLeft = InLeft.P95Ms;
                ScopedRight = InRight.P95Ms;
                break;
            case ProfileSortMode::Max:
                ScopedLeft = InLeft.MaxMs;
                ScopedRight = InRight.MaxMs;
                break;
            case ProfileSortMode::Total:
                break;
            }
            if (ScopedLeft != ScopedRight)
            {
                return ScopedLeft > ScopedRight;
            }
            // Stable tie-break on the name keeps the panel from flickering.
            return InLeft.Name < InRight.Name;
        };
        std::sort(MRows.begin(), MRows.end(), ScopedLess);
        if (MRows.size() > MMaxRows)
        {
            MHidden = MRows.size() - MMaxRows;
            MRows.resize(MMaxRows);
        }
    }

    [[nodiscard]] std::span<const ProfileRow> Rows() const noexcept
    {
        return MRows;
    }
    [[nodiscard]] std::uint32_t HiddenCount() const noexcept
    {
        return MHidden;
    }

private:
    std::vector<ProfileRow> MRows;
    std::uint32_t MMaxRows = 8;
    std::uint32_t MHidden = 0;
    ProfileSortMode MSortMode = ProfileSortMode::Total;
};

// "1.234 ms" style value for the panel column; the buffer keeps it allocation free.
[[nodiscard]] inline std::string_view FormatMilliseconds(double InMilliseconds, char* InBuffer,
                                                         std::size_t InBufferSize) noexcept
{
    if (InBuffer == nullptr || InBufferSize == 0)
    {
        return {};
    }
    const int ScopedWritten = std::snprintf(InBuffer, InBufferSize, "%.3f ms", InMilliseconds);
    if (ScopedWritten <= 0)
    {
        InBuffer[0] = '\0';
        return {};
    }
    return std::string_view(InBuffer, std::min(static_cast<std::size_t>(ScopedWritten), InBufferSize - 1));
}

}
