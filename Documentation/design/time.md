# Time, timers and dates

Before this layer every subsystem reached for `std::chrono` itself: the sandbox
carried its own steady clock, sound cues took a `nowMs` from the caller, the
accessibility captions kept a float clock and the animation timeline worked in
float seconds. There was no shared clock, no timer pool and no date type.

`Source/Engine/Core/Time.hpp` and `Source/Engine/Core/Calendar.hpp` are that
layer. Both are header-only and allocation-free.

## Unit

Every engine value is a `double` in seconds. Nanoseconds exist only inside
`Clock::NowTicks()` and never enter a data layout, so a timer array is a flat
array of doubles.

## Clock

One monotonic frame clock: `Tick()` once per iteration, then read `Delta()`
(already scaled and clamped), `Elapsed()` and `Frame()`. It:

- clamps a long frame (`SetMaxDelta`, default 0.25 s);
- scales time (`SetTimeScale`, for hit stop or slow motion);
- pauses without losing the accumulated time;
- ignores a platform reading that goes backwards;
- accepts an explicit reading through `Tick(ticks)`, which is what makes the
  clock testable.

`Clock::UnixSeconds()` is the only wall-clock entry point and exists for dates
only.

## Stopwatch and timers

`Stopwatch` starts, stops, accumulates and reads, and is reusable across frames.

`TimerPool<Capacity>` is a structure of arrays: remaining time, interval, fire
count, generation, repeating flag, plus a dense active list and a free list.
`Advance(dt, outFired)` touches only live timers, swap-removes the ones that end,
and reports a timer that crossed several intervals in one frame with a
`FireCount` instead of dropping fires. Handles carry a generation, so a recycled
slot never revives an old handle. A full pool returns an invalid handle instead
of allocating.

`FrameTimer<Capacity>` keeps rolling frame samples with average, min, max and
fps; `Push` is O(1) and the statistics walk a small ring on demand.

## Dates

Time stays in seconds. `DateTimeFromUnixSeconds` turns a timestamp into civil
fields with Howard Hinnant's constant-time algorithms (no tables, no allocation,
dates before the epoch included). `UnixSecondsFromCivil` goes back, and the
derived facts a HUD needs are part of the result: ISO weekday (1 = Monday), day
of the year and the timezone offset the caller applied.

There is no DST database: the caller owns the offset in minutes, for example
+180 for Moscow.

## Formatting

`TextWriter` writes into a caller-provided `std::span<char>` and returns the
length, so formatting never allocates and a too-small buffer truncates.
`FormatDateIso` (2026-10-30), `FormatClock24` (17:04:09), `FormatClock12`
(5:04:09 PM, with midnight as 12 AM and noon as 12 PM), `FormatDateTime`,
`FormatTimezoneOffset` (UTC+03:00), `FormatUnixSeconds` and `FormatDuration`
with three styles: `Clock` (01:02:05), `Compact` (1h 02m 05s) and `Precise`
(00:00:01.500).

Every formatter emits Western Arabic 0-9 digits only and is locale-free.
Translated month and weekday names belong to the localization tables, not to the
calendar.

## Verification

The `time` test covers the clock conversion, pause, scale, clamp, backwards
readings and reset; one-shot, repeating, reset, cancel, generation and capacity
behaviour of the timer pool; the rolling frame statistics; leap years, month
lengths, weekday and day of year; a civil-date round trip across the epoch; known
Unix timestamps in UTC and with an offset; and every formatter, including the
12-hour edge cases and a truncated buffer.
