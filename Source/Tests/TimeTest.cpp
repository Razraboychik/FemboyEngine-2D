// FemboyEngine2D - clocks, timers, rolling frame timing and calendar dates.
#include "Engine/Core/Calendar.hpp"
#include "Engine/Core/Time.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

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

void CheckText(std::string_view InExpected, std::span<char> InBuffer, std::uint32_t InWritten,
               const char* InMessage)
{
    const std::string_view ScopedActual(InBuffer.data(), InWritten);
    if (ScopedActual != InExpected)
    {
        std::fprintf(stderr, "FAIL: %s: expected '%s', got '%s'\n", InMessage, std::string(InExpected).c_str(),
                     std::string(ScopedActual).c_str());
        ++GFailures;
    }
}
}

int main()
{
    using namespace Fby::Core;

    // Clock behavior: deterministic ticks, pause, scale and clamping.
    Clock ScopedClock(0);
    Check(ScopedClock.Delta() == 0.0 && ScopedClock.Elapsed() == 0.0 && ScopedClock.Frame() == 0,
          "a fresh clock starts idle");
    // The default clamp is a gameplay frame budget; a manual clock widens it so
    // the conversion itself can be checked.
    ScopedClock.SetMaxDelta(1.0);
    ScopedClock.Tick(500000000);
    Check(ScopedClock.Delta() == 0.5, "the clock converts nanoseconds to seconds");
    Check(ScopedClock.Frame() == 1, "tick advances the frame counter");
    ScopedClock.Tick(750000000);
    Check(ScopedClock.Delta() == 0.25 && ScopedClock.Elapsed() == 0.75, "deltas accumulate");
    ScopedClock.Tick(750000000);
    Check(ScopedClock.Delta() == 0.0, "a repeated reading is a zero delta");
    ScopedClock.Tick(700000000);
    Check(ScopedClock.Delta() == 0.0 && ScopedClock.Elapsed() == 0.75, "a rewound reading cannot rewind time");
    ScopedClock.SetMaxDelta(0.25);
    ScopedClock.Tick(6000000000);
    Check(ScopedClock.Delta() == 0.25, "a long frame is clamped to the max delta");
    ScopedClock.SetMaxDelta(0.5);
    ScopedClock.Tick(6500000000);
    Check(ScopedClock.Delta() == 0.5, "the clamp is configurable");
    ScopedClock.Pause();
    ScopedClock.Tick(7500000000);
    Check(ScopedClock.Delta() == 0.0 && ScopedClock.IsPaused(), "a paused clock reports no delta");
    ScopedClock.Resume();
    ScopedClock.SetTimeScale(0.5);
    ScopedClock.Tick(8500000000);
    Check(ScopedClock.Delta() == 0.5, "the time scale multiplies the delta");
    ScopedClock.SetTimeScale(0.0);
    ScopedClock.Tick(9500000000);
    Check(ScopedClock.Delta() == 0.0, "a zero scale freezes gameplay time");
    ScopedClock.SetTimeScale(1.0);
    ScopedClock.Reset(10000000000);
    Check(ScopedClock.Elapsed() == 0.0 && ScopedClock.Frame() == 0, "reset re-bases the clock");
    Check(ScopedClock.RealElapsed() == 0.0, "reset also re-bases the real elapsed time");
    const std::int64_t ScopedFirst = Clock::NowTicks();
    const std::int64_t ScopedSecond = Clock::NowTicks();
    Check(ScopedSecond >= ScopedFirst, "the platform clock is monotonic");
    Check(Clock::UnixSeconds() > 1600000000, "the wall clock is a plausible Unix timestamp");

    // Timer pool behavior: one-shot, repeating, reset, cancel and generations.
    TimerPool<8> ScopedPool;
    const TimerHandle ScopedOneShot = ScopedPool.Create(1.0);
    Check(ScopedOneShot.IsValid() && ScopedPool.ActiveCount() == 1, "a timer is created");
    Check(ScopedPool.Remaining(ScopedOneShot) == 1.0, "the timer starts at its interval");
    Check(ScopedPool.Progress(ScopedOneShot) == 0.0, "progress starts at zero");
    std::array<TimerFired, 8> ScopedFired{};
    ScopedPool.Advance(0.5);
    Check(ScopedPool.Remaining(ScopedOneShot) == 0.5 && ScopedPool.Progress(ScopedOneShot) == 0.5,
          "advance counts the remaining time");
    Check(ScopedPool.Advance(0.5, std::span<TimerFired>(ScopedFired)) == 1, "the timer fires once");
    Check(ScopedFired[0].Handle == ScopedOneShot && ScopedFired[0].FireCount == 1,
          "the fired entry carries the handle and the count");
    Check(!ScopedPool.IsActive(ScopedOneShot), "a one-shot timer deactivates after firing");
    Check(ScopedPool.ActiveCount() == 0, "the active list is empty again");

    const TimerHandle ScopedReused = ScopedPool.Create(1.0);
    Check(ScopedReused.Index == ScopedOneShot.Index && ScopedReused.Generation != ScopedOneShot.Generation,
          "the slot is recycled with a new generation");
    Check(!ScopedPool.IsActive(ScopedOneShot), "a stale handle stays invalid");
    Check(!ScopedPool.Cancel(ScopedOneShot) && ScopedPool.Cancel(ScopedReused),
          "cancel accepts only live handles");

    const TimerHandle ScopedRepeating = ScopedPool.Create(0.25, true);
    Check(ScopedPool.IsRepeating(ScopedRepeating), "a repeating timer is marked");
    const std::uint32_t ScopedRepeatFires = ScopedPool.Advance(1.0, std::span<TimerFired>(ScopedFired));
    Check(ScopedRepeatFires == 1 && ScopedFired[0].FireCount == 4, "a slow frame reports every missed interval");
    Check(ScopedPool.IsActive(ScopedRepeating) && ScopedPool.FireCount(ScopedRepeating) == 4,
          "a repeating timer keeps running and counts its fires");
    Check(ScopedPool.Remaining(ScopedRepeating) == 0.25, "the repeating timer re-arms on the interval");
    Check(ScopedPool.Reset(ScopedRepeating, 2.0) && ScopedPool.FireCount(ScopedRepeating) == 0,
          "reset re-arms with a new interval");
    ScopedPool.Advance(0.5);
    Check(ScopedPool.Progress(ScopedRepeating) == 0.25, "progress follows the new interval");
    ScopedPool.Clear();
    Check(ScopedPool.ActiveCount() == 0 && !ScopedPool.IsActive(ScopedRepeating), "clear drops every timer");

    std::array<TimerHandle, 8> ScopedFull{};
    for (TimerHandle& ScopedHandle : ScopedFull)
    {
        ScopedHandle = ScopedPool.Create(1.0);
    }
    Check(ScopedPool.ActiveCount() == ScopedPool.PoolCapacity(), "the pool fills to capacity");
    Check(!ScopedPool.Create(1.0).IsValid(), "a full pool rejects a new timer without allocating");

    // Frame timer behavior: rolling average, minimum, maximum and fps.
    FrameTimer<4> ScopedFrames;
    Check(ScopedFrames.Fps() == 0.0, "an empty frame timer reports zero fps");
    ScopedFrames.Push(0.010);
    ScopedFrames.Push(0.020);
    ScopedFrames.Push(0.030);
    Check(ScopedFrames.Average() == 0.020 && ScopedFrames.Min() == 0.010 && ScopedFrames.Max() == 0.030,
          "the frame timer reports min, max and average");
    Check(ScopedFrames.Fps() == 50.0, "fps follows the average frame time");
    ScopedFrames.Push(0.040);
    ScopedFrames.Push(0.010);
    Check(ScopedFrames.Count == 4 && ScopedFrames.Min() == 0.010 && ScopedFrames.Max() == 0.040,
          "the ring keeps the newest samples");
    Check(ScopedFrames.Average() > 0.019 && ScopedFrames.Average() < 0.026, "the average forgets the oldest sample");
    ScopedFrames.Reset();
    Check(ScopedFrames.Count == 0 && ScopedFrames.Average() == 0.0, "reset clears the ring");

    // Calendar and civil-date conversions: known instants and leap years.
    Check(IsLeapYear(2000) && IsLeapYear(2024) && !IsLeapYear(1900) && !IsLeapYear(2023),
          "leap year rules");
    Check(DaysInMonth(2024, 2) == 29 && DaysInMonth(2023, 2) == 28 && DaysInMonth(2024, 4) == 30,
          "month lengths");
    Check(DaysFromCivil(1970, 1, 1) == 0, "the Unix epoch is day zero");
    Check(DaysFromCivil(2000, 1, 1) == 10957, "2000-01-01 is day 10957");
    Check(WeekdayFromDays(0) == 4, "the Unix epoch was a Thursday");
    const CivilDate ScopedEpoch = CivilFromDays(0);
    Check(ScopedEpoch.Year == 1970 && ScopedEpoch.Month == 1 && ScopedEpoch.Day == 1, "day zero is 1970-01-01");
    // Round trip over a wide range, including dates before the epoch.
    bool ScopedRoundTrip = true;
    for (std::int32_t ScopedDays = -40000; ScopedDays <= 40000; ScopedDays += 97)
    {
        const CivilDate ScopedDate = CivilFromDays(ScopedDays);
        ScopedRoundTrip = ScopedRoundTrip && DaysFromCivil(ScopedDate.Year, ScopedDate.Month, ScopedDate.Day) ==
                                                ScopedDays;
    }
    Check(ScopedRoundTrip, "civil and day numbers round trip");

    const DateTime ScopedKnown = DateTimeFromUnixSeconds(static_cast<std::int64_t>(1700000000));
    Check(ScopedKnown.Date.Year == 2023 && ScopedKnown.Date.Month == 11 && ScopedKnown.Date.Day == 14,
          "a known timestamp splits into the right date");
    Check(ScopedKnown.Time.Hour == 22 && ScopedKnown.Time.Minute == 13 && ScopedKnown.Time.Second == 20,
          "a known timestamp splits into the right time");
    Check(ScopedKnown.Weekday == 2, "2023-11-14 is a Tuesday");
    Check(ScopedKnown.DayOfYear == 318, "2023-11-14 is day 318");
    const DateTime ScopedMoscow = DateTimeFromUnixSeconds(static_cast<std::int64_t>(1700000000), 180);
    Check(ScopedMoscow.Date.Day == 15 && ScopedMoscow.Time.Hour == 1,
          "a timezone offset moves the wall clock");
    Check(UnixSecondsFromCivil(ScopedKnown.Date, ScopedKnown.Time) == 1700000000, "civil time converts back");
    const DateTime ScopedLeap = DateTimeFromUnixSeconds(static_cast<std::int64_t>(951782400));
    Check(ScopedLeap.Date.Year == 2000 && ScopedLeap.Date.Month == 2 && ScopedLeap.Date.Day == 29,
          "the leap day splits correctly");
    const DateTime ScopedFraction = DateTimeFromUnixSeconds(1700000000.25);
    Check(ScopedFraction.Time.Millisecond == 250, "a fractional timestamp becomes milliseconds");
    const DateTime ScopedBeforeEpoch = DateTimeFromUnixSeconds(static_cast<std::int64_t>(-86400));
    Check(ScopedBeforeEpoch.Date.Year == 1969 && ScopedBeforeEpoch.Date.Month == 12 &&
              ScopedBeforeEpoch.Date.Day == 31,
          "a negative timestamp keeps the previous day");

    // Formatting behavior: 24-hour and 12-hour clocks, dates and durations.
    std::array<char, 64> ScopedBuffer{};
    std::uint32_t ScopedWritten = FormatDateIso(ScopedKnown.Date, std::span<char>(ScopedBuffer));
    CheckText("2023-11-14", std::span<char>(ScopedBuffer), ScopedWritten, "ISO date");
    ScopedWritten = FormatClock24(ScopedKnown.Time, std::span<char>(ScopedBuffer));
    CheckText("22:13:20", std::span<char>(ScopedBuffer), ScopedWritten, "24 hour clock");
    ScopedWritten = FormatClock12(ScopedKnown.Time, std::span<char>(ScopedBuffer));
    CheckText("10:13:20 PM", std::span<char>(ScopedBuffer), ScopedWritten, "12 hour clock PM");
    ClockTime ScopedMidnight;
    ScopedMidnight.Hour = 0;
    ScopedMidnight.Minute = 5;
    ScopedWritten = FormatClock12(ScopedMidnight, std::span<char>(ScopedBuffer));
    CheckText("12:05:00 AM", std::span<char>(ScopedBuffer), ScopedWritten, "midnight is 12 AM");
    ClockTime ScopedNoon;
    ScopedNoon.Hour = 12;
    ScopedWritten = FormatClock12(ScopedNoon, std::span<char>(ScopedBuffer), false, false);
    CheckText("12:00", std::span<char>(ScopedBuffer), ScopedWritten, "noon is 12 without seconds or suffix");
    ScopedWritten = FormatDateTime(ScopedKnown, std::span<char>(ScopedBuffer));
    CheckText("2023-11-14 22:13:20", std::span<char>(ScopedBuffer), ScopedWritten, "date and time");
    ScopedWritten = FormatDateTime(ScopedKnown, std::span<char>(ScopedBuffer), true, true);
    CheckText("2023-11-14 10:13:20.000 PM", std::span<char>(ScopedBuffer), ScopedWritten, "precise 12 hour time");
    ScopedWritten = FormatTimezoneOffset(180, std::span<char>(ScopedBuffer));
    CheckText("UTC+03:00", std::span<char>(ScopedBuffer), ScopedWritten, "timezone offset");
    ScopedWritten = FormatTimezoneOffset(-330, std::span<char>(ScopedBuffer));
    CheckText("UTC-05:30", std::span<char>(ScopedBuffer), ScopedWritten, "negative half hour offset");
    ScopedWritten = FormatUnixSeconds(static_cast<std::int64_t>(1700000000), 0, std::span<char>(ScopedBuffer));
    CheckText("2023-11-14 22:13:20", std::span<char>(ScopedBuffer), ScopedWritten, "timestamp convenience");

    ScopedWritten = FormatDuration(3725.0, DurationStyle::Clock, std::span<char>(ScopedBuffer));
    CheckText("01:02:05", std::span<char>(ScopedBuffer), ScopedWritten, "clock duration");
    ScopedWritten = FormatDuration(3725.0, DurationStyle::Compact, std::span<char>(ScopedBuffer));
    CheckText("1h 02m 05s", std::span<char>(ScopedBuffer), ScopedWritten, "compact duration");
    ScopedWritten = FormatDuration(1.5, DurationStyle::Precise, std::span<char>(ScopedBuffer));
    CheckText("00:00:01.500", std::span<char>(ScopedBuffer), ScopedWritten, "precise duration");
    ScopedWritten = FormatDuration(-2.25, DurationStyle::Clock, std::span<char>(ScopedBuffer));
    CheckText("-00:00:02", std::span<char>(ScopedBuffer), ScopedWritten, "negative duration keeps the sign");
    ScopedWritten = FormatDuration(0.0, DurationStyle::Compact, std::span<char>(ScopedBuffer));
    CheckText("0s", std::span<char>(ScopedBuffer), ScopedWritten, "zero duration");
    ScopedWritten = FormatDuration(59.999, DurationStyle::Clock, std::span<char>(ScopedBuffer));
    CheckText("00:00:59", std::span<char>(ScopedBuffer), ScopedWritten, "the clock style truncates milliseconds");
    ScopedWritten = FormatDuration(59.9999, DurationStyle::Clock, std::span<char>(ScopedBuffer));
    CheckText("00:01:00", std::span<char>(ScopedBuffer), ScopedWritten, "rounding carries into the minute");
    ScopedWritten = FormatDuration(59.999, DurationStyle::Clock, std::span<char>(ScopedBuffer).first(4));
    CheckText("00:0", std::span<char>(ScopedBuffer), ScopedWritten, "a small buffer truncates instead of writing out");

    if (GFailures != 0)
    {
        std::fprintf(stderr, "%d time test(s) failed\n", GFailures);
        return 1;
    }
    std::printf("time tests: ok\n");
    return 0;
}
