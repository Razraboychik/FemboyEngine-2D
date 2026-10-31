// FemboyEngine2D - calendar dates and time formatting.
//
// Time is stored in seconds; a date is only a presentation of those seconds.
// The conversions are the constant-time civil algorithms (days from the Unix
// epoch, no tables and no allocation), so a date can be formatted on any thread
// and inside a fixed char buffer. Digits are the Western Arabic 0-9 only: every
// formatter is locale free, and a localized variant is the text layer's job.
#pragma once

#include "Engine/Core/Time.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Core
{

struct CivilDate
{
    std::int32_t Year = 1970;
    std::uint8_t Month = 1;
    std::uint8_t Day = 1;

    [[nodiscard]] friend constexpr bool operator==(const CivilDate&, const CivilDate&) noexcept = default;
};

// Wall clock fields of one day. Millisecond is the only sub-second field the
// formatters expose; the engine keeps sub-second time as a double elsewhere.
struct ClockTime
{
    std::uint8_t Hour = 0;
    std::uint8_t Minute = 0;
    std::uint8_t Second = 0;
    std::uint16_t Millisecond = 0;

    [[nodiscard]] friend constexpr bool operator==(const ClockTime&, const ClockTime&) noexcept = default;
};

// A civil date with its clock fields and the derived calendar facts a game UI
// usually shows: the ISO weekday (1 = Monday) and the day of the year (1 = Jan 1).
struct DateTime
{
    CivilDate Date{};
    ClockTime Time{};
    std::int32_t DayOfYear = 1;
    std::uint8_t Weekday = 1;
    // Minutes east of UTC that produced this value, so a caller can render the
    // offset it used.
    std::int32_t TimezoneOffsetMinutes = 0;
};

[[nodiscard]] constexpr bool IsLeapYear(std::int32_t InYear) noexcept
{
    return (InYear % 4 == 0 && InYear % 100 != 0) || InYear % 400 == 0;
}

[[nodiscard]] constexpr std::uint8_t DaysInMonth(std::int32_t InYear, std::uint8_t InMonth) noexcept
{
    constexpr std::array<std::uint8_t, 12> kDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (InMonth < 1 || InMonth > 12)
    {
        return 0;
    }
    if (InMonth == 2 && IsLeapYear(InYear))
    {
        return 29;
    }
    return kDays[InMonth - 1];
}

// Howard Hinnant's civil algorithms: constant time for any proleptic Gregorian
// date, including dates before the epoch, with no tables to page in.
[[nodiscard]] constexpr std::int32_t DaysFromCivil(std::int32_t InYear, std::uint32_t InMonth,
                                                   std::uint32_t InDay) noexcept
{
    const std::int32_t ScopedYear = InYear - (InMonth <= 2u ? 1 : 0);
    const std::int32_t ScopedEra = (ScopedYear >= 0 ? ScopedYear : ScopedYear - 399) / 400;
    const std::uint32_t ScopedYearOfEra = static_cast<std::uint32_t>(ScopedYear - ScopedEra * 400);
    const std::int32_t ScopedMonthShift =
        InMonth > 2u ? static_cast<std::int32_t>(InMonth) - 3 : static_cast<std::int32_t>(InMonth) + 9;
    const std::uint32_t ScopedDayOfYear =
        static_cast<std::uint32_t>((153 * ScopedMonthShift + 2) / 5) + InDay - 1u;
    const std::uint32_t ScopedDayOfEra = ScopedYearOfEra * 365u + ScopedYearOfEra / 4u - ScopedYearOfEra / 100u +
                                         ScopedDayOfYear;
    return ScopedEra * 146097 + static_cast<std::int32_t>(ScopedDayOfEra) - 719468;
}

[[nodiscard]] constexpr CivilDate CivilFromDays(std::int32_t InDays) noexcept
{
    const std::int32_t ScopedShifted = InDays + 719468;
    const std::int32_t ScopedEra = (ScopedShifted >= 0 ? ScopedShifted : ScopedShifted - 146096) / 146097;
    const std::uint32_t ScopedDayOfEra = static_cast<std::uint32_t>(ScopedShifted - ScopedEra * 146097);
    const std::uint32_t ScopedYearOfEra =
        (ScopedDayOfEra - ScopedDayOfEra / 1460u + ScopedDayOfEra / 36524u - ScopedDayOfEra / 146096u) / 365u;
    const std::uint32_t ScopedYear = ScopedYearOfEra + static_cast<std::uint32_t>(ScopedEra) * 400u;
    const std::uint32_t ScopedDayOfYear =
        ScopedDayOfEra - (365u * ScopedYearOfEra + ScopedYearOfEra / 4u - ScopedYearOfEra / 100u);
    const std::uint32_t ScopedMonthIndex = (5u * ScopedDayOfYear + 2u) / 153u;
    const std::uint32_t ScopedDay = ScopedDayOfYear - (153u * ScopedMonthIndex + 2u) / 5u + 1u;
    const std::int32_t ScopedMonth = static_cast<std::int32_t>(ScopedMonthIndex) + (ScopedMonthIndex < 10u ? 3 : -9);
    CivilDate ScopedDate;
    ScopedDate.Year = static_cast<std::int32_t>(ScopedYear) + (ScopedMonth <= 2 ? 1 : 0);
    ScopedDate.Month = static_cast<std::uint8_t>(ScopedMonth);
    ScopedDate.Day = static_cast<std::uint8_t>(ScopedDay);
    return ScopedDate;
}

// ISO weekday: 1 = Monday .. 7 = Sunday. The Unix epoch was a Thursday.
[[nodiscard]] constexpr std::uint8_t WeekdayFromDays(std::int32_t InDays) noexcept
{
    const std::int32_t ScopedWeekday = ((InDays % 7) + 7 + 3) % 7;
    return static_cast<std::uint8_t>(ScopedWeekday + 1);
}

[[nodiscard]] constexpr std::int32_t DayOfYearFromCivil(const CivilDate& InDate) noexcept
{
    std::int32_t ScopedDay = InDate.Day;
    for (std::uint8_t ScopedMonth = 1; ScopedMonth < InDate.Month; ++ScopedMonth)
    {
        ScopedDay += DaysInMonth(InDate.Year, ScopedMonth);
    }
    return ScopedDay;
}

// Seconds since the Unix epoch for a civil date and clock time interpreted in
// UTC. The caller applies the timezone offset before calling this.
[[nodiscard]] constexpr std::int64_t UnixSecondsFromCivil(const CivilDate& InDate, const ClockTime& InTime) noexcept
{
    const std::int32_t ScopedDays = DaysFromCivil(InDate.Year, InDate.Month, InDate.Day);
    const std::int64_t ScopedDaySeconds = static_cast<std::int64_t>(ScopedDays) * 86400;
    const std::int64_t ScopedClockSeconds = static_cast<std::int64_t>(InTime.Hour) * 3600 +
                                            static_cast<std::int64_t>(InTime.Minute) * 60 + InTime.Second;
    return ScopedDaySeconds + ScopedClockSeconds;
}

// Splits a Unix timestamp into civil fields. InTimezoneOffsetMinutes moves the
// instant into a local wall clock, for example +180 for Moscow; there is no DST
// database, the caller owns the offset.
[[nodiscard]] inline DateTime DateTimeFromUnixSeconds(std::int64_t InUnixSeconds,
                                                      std::int32_t InTimezoneOffsetMinutes = 0) noexcept
{
    const std::int64_t ScopedLocal = InUnixSeconds + static_cast<std::int64_t>(InTimezoneOffsetMinutes) * 60;
    std::int64_t ScopedDays = ScopedLocal / 86400;
    std::int64_t ScopedRemainder = ScopedLocal % 86400;
    if (ScopedRemainder < 0)
    {
        ScopedRemainder += 86400;
        --ScopedDays;
    }
    DateTime ScopedResult;
    ScopedResult.Date = CivilFromDays(static_cast<std::int32_t>(ScopedDays));
    ScopedResult.Time.Hour = static_cast<std::uint8_t>(ScopedRemainder / 3600);
    ScopedResult.Time.Minute = static_cast<std::uint8_t>((ScopedRemainder % 3600) / 60);
    ScopedResult.Time.Second = static_cast<std::uint8_t>(ScopedRemainder % 60);
    ScopedResult.Time.Millisecond = 0;
    ScopedResult.DayOfYear = DayOfYearFromCivil(ScopedResult.Date);
    ScopedResult.Weekday = WeekdayFromDays(static_cast<std::int32_t>(ScopedDays));
    ScopedResult.TimezoneOffsetMinutes = InTimezoneOffsetMinutes;
    return ScopedResult;
}

// Same split for a double timestamp: the fraction becomes milliseconds.
[[nodiscard]] inline DateTime DateTimeFromUnixSeconds(double InUnixSeconds,
                                                      std::int32_t InTimezoneOffsetMinutes = 0) noexcept
{
    const double ScopedWhole = InUnixSeconds >= 0.0 ? static_cast<double>(static_cast<std::int64_t>(InUnixSeconds))
                                                    : static_cast<double>(static_cast<std::int64_t>(InUnixSeconds) - 1);
    const std::int64_t ScopedSeconds = static_cast<std::int64_t>(ScopedWhole);
    DateTime ScopedResult = DateTimeFromUnixSeconds(ScopedSeconds, InTimezoneOffsetMinutes);
    const double ScopedFraction = InUnixSeconds - ScopedWhole;
    if (ScopedFraction > 0.0)
    {
        const auto ScopedMilliseconds = static_cast<std::int32_t>(ScopedFraction * 1000.0 + 0.5);
        if (ScopedMilliseconds >= 1000)
        {
            return DateTimeFromUnixSeconds(ScopedSeconds + 1, InTimezoneOffsetMinutes);
        }
        ScopedResult.Time.Millisecond = static_cast<std::uint16_t>(ScopedMilliseconds);
    }
    return ScopedResult;
}

// Fixed-buffer writer. Every formatter below writes into a caller-provided span
// and returns the number of characters written, so formatting never allocates
// and a too-small buffer simply truncates.
struct TextWriter
{
    std::span<char> Out{};
    std::uint32_t Written = 0;

    void Push(char InCharacter) noexcept
    {
        if (Written < Out.size())
        {
            Out[Written] = InCharacter;
            ++Written;
        }
    }
    // Zero-padded unsigned number of at least InWidth digits. Digits are 0-9.
    void Number(std::uint32_t InValue, std::uint32_t InWidth) noexcept
    {
        std::array<char, 10> ScopedDigits{};
        std::uint32_t ScopedCount = 0;
        std::uint32_t ScopedValue = InValue;
        do
        {
            ScopedDigits[ScopedCount] = static_cast<char>('0' + static_cast<char>(ScopedValue % 10u));
            ++ScopedCount;
            ScopedValue /= 10u;
        } while (ScopedValue != 0u && ScopedCount < ScopedDigits.size());
        while (ScopedCount < InWidth && ScopedCount < ScopedDigits.size())
        {
            ScopedDigits[ScopedCount] = '0';
            ++ScopedCount;
        }
        while (ScopedCount > 0)
        {
            --ScopedCount;
            Push(ScopedDigits[ScopedCount]);
        }
    }
    // Signed year: the sign stays explicit and the magnitude keeps four digits.
    void Year(std::int32_t InYear) noexcept
    {
        if (InYear < 0)
        {
            Push('-');
            Number(static_cast<std::uint32_t>(-(InYear + 1)) + 1u, 4);
            return;
        }
        Number(static_cast<std::uint32_t>(InYear), 4);
    }
    void Text(std::string_view InText) noexcept
    {
        for (const char ScopedCharacter : InText)
        {
            Push(ScopedCharacter);
        }
    }
    // Appends the terminator when the buffer has room and returns the length
    // without it, so a caller can pass the result to printf-style APIs.
    [[nodiscard]] std::uint32_t Finish() noexcept
    {
        if (Written < Out.size())
        {
            Out[Written] = '\0';
        }
        return Written;
    }
};

// 2026-10-30.
[[nodiscard]] inline std::uint32_t FormatDateIso(const CivilDate& InDate, std::span<char> OutBuffer) noexcept
{
    TextWriter ScopedWriter{OutBuffer};
    ScopedWriter.Year(InDate.Year);
    ScopedWriter.Push('-');
    ScopedWriter.Number(InDate.Month, 2);
    ScopedWriter.Push('-');
    ScopedWriter.Number(InDate.Day, 2);
    return ScopedWriter.Finish();
}

// 17:04:09, always 24 hours.
[[nodiscard]] inline std::uint32_t FormatClock24(const ClockTime& InTime, std::span<char> OutBuffer) noexcept
{
    TextWriter ScopedWriter{OutBuffer};
    ScopedWriter.Number(InTime.Hour, 2);
    ScopedWriter.Push(':');
    ScopedWriter.Number(InTime.Minute, 2);
    ScopedWriter.Push(':');
    ScopedWriter.Number(InTime.Second, 2);
    return ScopedWriter.Finish();
}

// 5:04:09 PM. The suffix is fixed ASCII, because a translated suffix belongs to
// the text tables and not to the calendar.
[[nodiscard]] inline std::uint32_t FormatClock12(const ClockTime& InTime, std::span<char> OutBuffer,
                                                 bool InWithSeconds = true, bool InWithSuffix = true) noexcept
{
    TextWriter ScopedWriter{OutBuffer};
    std::uint32_t ScopedHour = InTime.Hour % 12u;
    if (ScopedHour == 0u)
    {
        ScopedHour = 12u;
    }
    ScopedWriter.Number(ScopedHour, 1);
    ScopedWriter.Push(':');
    ScopedWriter.Number(InTime.Minute, 2);
    if (InWithSeconds)
    {
        ScopedWriter.Push(':');
        ScopedWriter.Number(InTime.Second, 2);
    }
    if (InWithSuffix)
    {
        ScopedWriter.Text(InTime.Hour < 12u ? " AM" : " PM");
    }
    return ScopedWriter.Finish();
}

// 2026-10-30 17:04:09, or the 12 hour clock when asked. Milliseconds are
// appended only for the precise form.
[[nodiscard]] inline std::uint32_t FormatDateTime(const DateTime& InDateTime, std::span<char> OutBuffer,
                                                  bool InTwelveHour = false, bool InWithMilliseconds = false) noexcept
{
    TextWriter ScopedWriter{OutBuffer};
    ScopedWriter.Year(InDateTime.Date.Year);
    ScopedWriter.Push('-');
    ScopedWriter.Number(InDateTime.Date.Month, 2);
    ScopedWriter.Push('-');
    ScopedWriter.Number(InDateTime.Date.Day, 2);
    ScopedWriter.Push(' ');
    if (InTwelveHour)
    {
        ScopedWriter.Number(InDateTime.Time.Hour % 12u == 0u ? 12u : InDateTime.Time.Hour % 12u, 1);
    }
    else
    {
        ScopedWriter.Number(InDateTime.Time.Hour, 2);
    }
    ScopedWriter.Push(':');
    ScopedWriter.Number(InDateTime.Time.Minute, 2);
    ScopedWriter.Push(':');
    ScopedWriter.Number(InDateTime.Time.Second, 2);
    if (InWithMilliseconds)
    {
        ScopedWriter.Push('.');
        ScopedWriter.Number(InDateTime.Time.Millisecond, 3);
    }
    if (InTwelveHour)
    {
        ScopedWriter.Text(InDateTime.Time.Hour < 12u ? " AM" : " PM");
    }
    return ScopedWriter.Finish();
}

// UTC+03:00, or UTC+00:00 for a zero offset.
[[nodiscard]] inline std::uint32_t FormatTimezoneOffset(std::int32_t InOffsetMinutes,
                                                        std::span<char> OutBuffer) noexcept
{
    TextWriter ScopedWriter{OutBuffer};
    ScopedWriter.Text("UTC");
    ScopedWriter.Push(InOffsetMinutes < 0 ? '-' : '+');
    const std::uint32_t ScopedMinutes =
        static_cast<std::uint32_t>(InOffsetMinutes < 0 ? -InOffsetMinutes : InOffsetMinutes);
    ScopedWriter.Number(ScopedMinutes / 60u, 2);
    ScopedWriter.Push(':');
    ScopedWriter.Number(ScopedMinutes % 60u, 2);
    return ScopedWriter.Finish();
}

// HH:MM:SS for DurationStyle::Clock, "1h 2m 5s" for Compact and
// "HH:MM:SS.mmm" for Precise. A negative duration keeps a leading sign.
[[nodiscard]] inline std::uint32_t FormatDuration(double InSeconds, DurationStyle InStyle,
                                                  std::span<char> OutBuffer) noexcept
{
    TextWriter ScopedWriter{OutBuffer};
    if (InSeconds < 0.0)
    {
        ScopedWriter.Push('-');
        InSeconds = -InSeconds;
    }
    // One integer millisecond total keeps the carry rules trivial and exact for
    // any span up to a day.
    const auto ScopedTotalMilliseconds =
        static_cast<std::uint64_t>(InSeconds * 1000.0 + 0.5);
    const std::uint32_t ScopedHours = static_cast<std::uint32_t>(ScopedTotalMilliseconds / 3600000ull);
    const std::uint32_t ScopedMinutes = static_cast<std::uint32_t>((ScopedTotalMilliseconds / 60000ull) % 60ull);
    const std::uint32_t ScopedSeconds = static_cast<std::uint32_t>((ScopedTotalMilliseconds / 1000ull) % 60ull);
    const std::uint32_t ScopedMilliseconds = static_cast<std::uint32_t>(ScopedTotalMilliseconds % 1000ull);
    if (InStyle == DurationStyle::Compact)
    {
        bool ScopedWrote = false;
        if (ScopedHours != 0)
        {
            ScopedWriter.Number(ScopedHours, 1);
            ScopedWriter.Push('h');
            ScopedWriter.Push(' ');
            ScopedWrote = true;
        }
        if (ScopedMinutes != 0 || ScopedHours != 0)
        {
            ScopedWriter.Number(ScopedMinutes, ScopedWrote ? 2 : 1);
            ScopedWriter.Push('m');
            ScopedWriter.Push(' ');
            ScopedWrote = true;
        }
        ScopedWriter.Number(ScopedSeconds, ScopedWrote ? 2 : 1);
        ScopedWriter.Push('s');
        return ScopedWriter.Finish();
    }
    ScopedWriter.Number(ScopedHours, 2);
    ScopedWriter.Push(':');
    ScopedWriter.Number(ScopedMinutes, 2);
    ScopedWriter.Push(':');
    ScopedWriter.Number(ScopedSeconds, 2);
    if (InStyle == DurationStyle::Precise)
    {
        ScopedWriter.Push('.');
        ScopedWriter.Number(ScopedMilliseconds, 3);
    }
    return ScopedWriter.Finish();
}

// Convenience: one Unix timestamp straight to text.
[[nodiscard]] inline std::uint32_t FormatUnixSeconds(std::int64_t InUnixSeconds, std::int32_t InTimezoneOffsetMinutes,
                                                     std::span<char> OutBuffer, bool InTwelveHour = false) noexcept
{
    return FormatDateTime(DateTimeFromUnixSeconds(InUnixSeconds, InTimezoneOffsetMinutes), OutBuffer, InTwelveHour);
}
}
