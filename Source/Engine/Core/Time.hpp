// FemboyEngine2D - clocks, timers and frame timing.
//
// Every value in this header is a plain double in seconds, so a timer pool is a
// flat array of doubles with no allocation, no virtual calls and no std::chrono
// types in the engine's data layout. The platform reading is converted to
// seconds at the boundary and never stored.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>

namespace Fby::Core
{

inline constexpr double kSecondsPerMinute = 60.0;
inline constexpr double kSecondsPerHour = 3600.0;
inline constexpr double kSecondsPerDay = 86400.0;
inline constexpr double kSecondsPerWeek = 604800.0;
inline constexpr double kNanosecondsPerSecond = 1.0e9;

// A monotonic frame clock. The engine never reads the wall clock for gameplay
// time: the clock owns a start reading, a scaled delta, a pause flag and a frame
// counter, and the caller only has to call Tick() once per iteration.
class Clock
{
public:
    Clock() noexcept
        : Clock(NowTicks())
    {
    }

    explicit Clock(std::int64_t InStartTicks) noexcept
    {
        Reset(InStartTicks);
    }

    // Platform readings, converted once. Steady ticks are monotonic and only
    // meaningful relative to another reading from the same process.
    [[nodiscard]] static std::int64_t NowTicks() noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    [[nodiscard]] static double NowSeconds() noexcept
    {
        return static_cast<double>(NowTicks()) / kNanosecondsPerSecond;
    }
    // Wall clock in whole seconds since the Unix epoch. Dates are the only
    // consumer: gameplay and timers stay on the steady clock.
    [[nodiscard]] static std::int64_t UnixSeconds() noexcept
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }
    [[nodiscard]] static double UnixSecondsDouble() noexcept
    {
        return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    [[nodiscard]] static std::uint64_t UnixMilliseconds() noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
    }
    // Monotonic milliseconds and nanoseconds, for subsystems that stamp events
    // or poll a timeout. Same epoch as NowTicks, so the values are comparable
    // only within one process.
    [[nodiscard]] static std::uint64_t NowMilliseconds() noexcept
    {
        return static_cast<std::uint64_t>(NowTicks() / 1000000);
    }
    [[nodiscard]] static std::uint64_t NowNanoseconds() noexcept
    {
        return static_cast<std::uint64_t>(NowTicks());
    }

    // Advances to an absolute reading. A reading at or before the last one
    // contributes zero delta, so a broken platform clock cannot rewind gameplay.
    void Tick(std::int64_t InTicks) noexcept
    {
        const std::int64_t ScopedTicks = InTicks > MLastTicks ? InTicks : MLastTicks;
        const double ScopedRaw = static_cast<double>(ScopedTicks - MLastTicks) / kNanosecondsPerSecond;
        MLastTicks = ScopedTicks;
        ++MFrame;
        if (MPaused)
        {
            MDelta = 0.0;
            return;
        }
        double ScopedDelta = ScopedRaw * MTimeScale;
        if (ScopedDelta > MMaxDelta)
        {
            ScopedDelta = MMaxDelta;
        }
        MDelta = ScopedDelta;
        MElapsed += ScopedDelta;
    }

    void Tick() noexcept
    {
        Tick(NowTicks());
    }

    // Re-bases the clock without touching the accumulated time, the frame count
    // or the scale. A pause survives a reset.
    void Reset(std::int64_t InStartTicks) noexcept
    {
        MStartTicks = InStartTicks;
        MLastTicks = InStartTicks;
        MElapsed = 0.0;
        MDelta = 0.0;
        MFrame = 0;
    }

    void Reset() noexcept
    {
        Reset(NowTicks());
    }

    // Seconds since the last Tick, already clamped and scaled. This is the only
    // value a system should integrate.
    [[nodiscard]] double Delta() const noexcept
    {
        return MDelta;
    }
    [[nodiscard]] double Elapsed() const noexcept
    {
        return MElapsed;
    }
    [[nodiscard]] std::uint64_t Frame() const noexcept
    {
        return MFrame;
    }
    [[nodiscard]] bool IsPaused() const noexcept
    {
        return MPaused;
    }
    [[nodiscard]] double TimeScale() const noexcept
    {
        return MTimeScale;
    }
    [[nodiscard]] double MaxDelta() const noexcept
    {
        return MMaxDelta;
    }
    // Raw platform reading of the last Tick, still in nanoseconds.
    [[nodiscard]] std::int64_t LastTicks() const noexcept
    {
        return MLastTicks;
    }
    // Seconds between the start reading and the last Tick, including pauses.
    [[nodiscard]] double RealElapsed() const noexcept
    {
        return static_cast<double>(MLastTicks - MStartTicks) / kNanosecondsPerSecond;
    }

    void Pause() noexcept
    {
        MPaused = true;
        MDelta = 0.0;
    }
    void Resume() noexcept
    {
        MPaused = false;
    }
    void TogglePause() noexcept
    {
        if (MPaused)
        {
            Resume();
        }
        else
        {
            Pause();
        }
    }
    // Negative scales run time backwards; the elapsed sum follows the scaled
    // delta, so a hit-stop can be expressed as a short low scale.
    void SetTimeScale(double InScale) noexcept
    {
        MTimeScale = InScale > 0.0 ? InScale : 0.0;
    }
    // Clamps a long frame, for example after a debugger break or a stalled
    // window, so a single step cannot teleport the simulation.
    void SetMaxDelta(double InSeconds) noexcept
    {
        MMaxDelta = InSeconds > 0.0 ? InSeconds : 0.0;
    }

private:
    std::int64_t MStartTicks = 0;
    std::int64_t MLastTicks = 0;
    double MElapsed = 0.0;
    double MDelta = 0.0;
    double MTimeScale = 1.0;
    double MMaxDelta = 0.25;
    std::uint64_t MFrame = 0;
    bool MPaused = false;
};

// Measures one interval. Start is explicit so a caller can measure work that
// already began, and Stop keeps the accumulated value for repeated sections.
struct Stopwatch
{
    double StartSeconds = 0.0;
    double AccumulatedSeconds = 0.0;
    bool Running = false;

    void Start() noexcept
    {
        StartSeconds = Clock::NowSeconds();
        Running = true;
    }
    void Stop() noexcept
    {
        if (Running)
        {
            AccumulatedSeconds += Clock::NowSeconds() - StartSeconds;
            Running = false;
        }
    }
    [[nodiscard]] double Elapsed() const noexcept
    {
        return Running ? AccumulatedSeconds + (Clock::NowSeconds() - StartSeconds) : AccumulatedSeconds;
    }
    void Reset() noexcept
    {
        AccumulatedSeconds = 0.0;
        Running = false;
    }
};

// Generation handle: a recycled slot never revives an old handle.
struct TimerHandle
{
    std::uint32_t Index = 0xFFFFFFFFu;
    std::uint32_t Generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return Index != 0xFFFFFFFFu && Generation != 0;
    }
    [[nodiscard]] friend constexpr bool operator==(const TimerHandle&, const TimerHandle&) noexcept = default;
};

// One entry per timer that fired during Advance. FireCount is the number of
// intervals crossed, so a timer that missed frames reports the whole count
// instead of silently losing fires.
struct TimerFired
{
    TimerHandle Handle{};
    std::uint32_t FireCount = 0;
    double OverrunSeconds = 0.0;
};

// Fixed-capacity pool of countdown and repeating timers. The storage is
// structure-of-arrays and iteration only touches the active slot list, so a pool
// with a few live timers costs a few iterations per frame.
template <std::uint32_t Capacity = 64>
class TimerPool
{
public:
    [[nodiscard]] TimerHandle Create(double InSeconds, bool InRepeating = false) noexcept
    {
        const std::uint32_t ScopedSlot = AcquireSlot();
        if (ScopedSlot == 0xFFFFFFFFu)
        {
            return {};
        }
        MInterval[ScopedSlot] = InSeconds > 0.0 ? InSeconds : 0.0;
        MRemaining[ScopedSlot] = MInterval[ScopedSlot];
        MFires[ScopedSlot] = 0;
        MRepeating[ScopedSlot] = InRepeating ? 1u : 0u;
        MGeneration[ScopedSlot] = MNextGeneration;
        ++MNextGeneration;
        if (MNextGeneration == 0)
        {
            MNextGeneration = 1;
        }
        PushActive(ScopedSlot);
        return TimerHandle{ScopedSlot, MGeneration[ScopedSlot]};
    }

    [[nodiscard]] bool Cancel(TimerHandle InHandle) noexcept
    {
        if (MGeneration[InHandle.Index] != InHandle.Generation || InHandle.Generation == 0 ||
            InHandle.Index >= Capacity)
        {
            return false;
        }
        RemoveActive(InHandle.Index);
        MGeneration[InHandle.Index] = 0;
        MRemaining[InHandle.Index] = 0.0;
        MFree[MFreeCount] = InHandle.Index;
        ++MFreeCount;
        return true;
    }

    // Re-arms a live timer with a new interval and clears its fire count.
    [[nodiscard]] bool Reset(TimerHandle InHandle, double InSeconds) noexcept
    {
        if (!IsActive(InHandle))
        {
            return false;
        }
        MInterval[InHandle.Index] = InSeconds > 0.0 ? InSeconds : 0.0;
        MRemaining[InHandle.Index] = MInterval[InHandle.Index];
        MFires[InHandle.Index] = 0;
        return true;
    }

    [[nodiscard]] bool IsActive(TimerHandle InHandle) const noexcept
    {
        return InHandle.Index < Capacity && InHandle.Generation != 0 &&
               MGeneration[InHandle.Index] == InHandle.Generation;
    }
    [[nodiscard]] double Remaining(TimerHandle InHandle) const noexcept
    {
        return IsActive(InHandle) ? MRemaining[InHandle.Index] : 0.0;
    }
    [[nodiscard]] double Interval(TimerHandle InHandle) const noexcept
    {
        return IsActive(InHandle) ? MInterval[InHandle.Index] : 0.0;
    }
    [[nodiscard]] double Progress(TimerHandle InHandle) const noexcept
    {
        if (!IsActive(InHandle) || MInterval[InHandle.Index] <= 0.0)
        {
            return 1.0;
        }
        const double ScopedProgress = 1.0 - MRemaining[InHandle.Index] / MInterval[InHandle.Index];
        return ScopedProgress < 0.0 ? 0.0 : (ScopedProgress > 1.0 ? 1.0 : ScopedProgress);
    }
    [[nodiscard]] std::uint32_t FireCount(TimerHandle InHandle) const noexcept
    {
        return IsActive(InHandle) ? MFires[InHandle.Index] : 0u;
    }
    [[nodiscard]] bool IsRepeating(TimerHandle InHandle) const noexcept
    {
        return IsActive(InHandle) && MRepeating[InHandle.Index] != 0;
    }

    // Advances every active timer and writes the ones that fired to OutFired.
    // Returns the number of entries written; a timer that fires several times in
    // one advance is one entry with FireCount set. A repeating timer stays
    // active; a one-shot timer becomes invalid, so its handle can be dropped.
    [[nodiscard]] std::uint32_t Advance(double InDeltaSeconds, std::span<TimerFired> OutFired) noexcept
    {
        if (InDeltaSeconds <= 0.0 || MActiveCount == 0)
        {
            return 0;
        }
        std::uint32_t ScopedWritten = 0;
        for (std::uint32_t ScopedCursor = MActiveCount; ScopedCursor > 0; --ScopedCursor)
        {
            const std::uint32_t ScopedIndex = ScopedCursor - 1u;
            const std::uint32_t ScopedSlot = MActive[ScopedIndex];
            const double ScopedRemaining = MRemaining[ScopedSlot] - InDeltaSeconds;
            if (ScopedRemaining > 0.0)
            {
                MRemaining[ScopedSlot] = ScopedRemaining;
                continue;
            }
            const double ScopedInterval = MInterval[ScopedSlot] > 0.0 ? MInterval[ScopedSlot] : 1.0e-6;
            const double ScopedOverrun = -ScopedRemaining;
            std::uint32_t ScopedFires = 1;
            if (ScopedOverrun >= ScopedInterval)
            {
                const double ScopedExtra = ScopedOverrun / ScopedInterval;
                ScopedFires += static_cast<std::uint32_t>(ScopedExtra);
            }
            MFires[ScopedSlot] += ScopedFires;
            if (ScopedWritten < OutFired.size())
            {
                OutFired[ScopedWritten++] = TimerFired{TimerHandle{ScopedSlot, MGeneration[ScopedSlot]}, ScopedFires,
                                                       ScopedOverrun};
            }
            if (MRepeating[ScopedSlot] != 0)
            {
                double ScopedNext = ScopedRemaining + ScopedInterval * static_cast<double>(ScopedFires);
                if (ScopedNext <= 0.0)
                {
                    ScopedNext = ScopedInterval;
                }
                MRemaining[ScopedSlot] = ScopedNext;
                continue;
            }
            MRemaining[ScopedSlot] = 0.0;
            RemoveActiveAt(ScopedIndex);
            MGeneration[ScopedSlot] = 0;
            MFree[MFreeCount] = ScopedSlot;
            ++MFreeCount;
        }
        return ScopedWritten;
    }

    // Same advance without collecting the fired list, for callers that only poll
    // Remaining or FireCount.
    void Advance(double InDeltaSeconds) noexcept
    {
        (void)Advance(InDeltaSeconds, std::span<TimerFired>());
    }

    [[nodiscard]] std::uint32_t ActiveCount() const noexcept
    {
        return MActiveCount;
    }
    [[nodiscard]] static constexpr std::uint32_t PoolCapacity() noexcept
    {
        return Capacity;
    }
    // Live slot indices, so a caller can run one branch per timer without
    // storing handles.
    [[nodiscard]] std::span<const std::uint32_t> ActiveSlots() const noexcept
    {
        return std::span<const std::uint32_t>(MActive.data(), MActiveCount);
    }
    [[nodiscard]] std::uint32_t GenerationAt(std::uint32_t InSlot) const noexcept
    {
        return InSlot < Capacity ? MGeneration[InSlot] : 0u;
    }

    void Clear() noexcept
    {
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MActiveCount; ++ScopedIndex)
        {
            MGeneration[MActive[ScopedIndex]] = 0;
        }
        MActiveCount = 0;
        MFreeCount = 0;
        MHighWater = 0;
        MNextGeneration = 1;
    }

private:
    [[nodiscard]] std::uint32_t AcquireSlot() noexcept
    {
        if (MFreeCount != 0)
        {
            --MFreeCount;
            return MFree[MFreeCount];
        }
        if (MHighWater >= Capacity)
        {
            return 0xFFFFFFFFu;
        }
        const std::uint32_t ScopedSlot = MHighWater;
        ++MHighWater;
        return ScopedSlot;
    }

    void PushActive(std::uint32_t InSlot) noexcept
    {
        MSlotInActive[InSlot] = MActiveCount;
        MActive[MActiveCount] = InSlot;
        ++MActiveCount;
    }

    // Swap-removes one active entry, so a cancelled timer costs one copy and the
    // active list stays dense.
    void RemoveActiveAt(std::uint32_t InActiveIndex) noexcept
    {
        const std::uint32_t ScopedLast = MActiveCount - 1u;
        if (InActiveIndex != ScopedLast)
        {
            const std::uint32_t ScopedMoved = MActive[ScopedLast];
            MActive[InActiveIndex] = ScopedMoved;
            MSlotInActive[ScopedMoved] = InActiveIndex;
        }
        --MActiveCount;
    }

    void RemoveActive(std::uint32_t InSlot) noexcept
    {
        RemoveActiveAt(MSlotInActive[InSlot]);
    }

    std::array<double, Capacity> MRemaining{};
    std::array<double, Capacity> MInterval{};
    std::array<std::uint32_t, Capacity> MGeneration{};
    std::array<std::uint32_t, Capacity> MFires{};
    std::array<std::uint8_t, Capacity> MRepeating{};
    std::array<std::uint32_t, Capacity> MActive{};
    std::array<std::uint32_t, Capacity> MSlotInActive{};
    std::array<std::uint32_t, Capacity> MFree{};
    std::uint32_t MActiveCount = 0;
    std::uint32_t MFreeCount = 0;
    std::uint32_t MHighWater = 0;
    std::uint32_t MNextGeneration = 1;
};

// Rolling frame statistics over a fixed number of samples. Push is O(1); the
// min, max and average walk a small ring only when a caller asks for them.
template <std::uint32_t Capacity = 120>
struct FrameTimer
{
    std::array<double, Capacity> Samples{};
    std::uint32_t Count = 0;
    std::uint32_t Next = 0;
    double Total = 0.0;

    void Push(double InDeltaSeconds) noexcept
    {
        if (InDeltaSeconds < 0.0)
        {
            InDeltaSeconds = 0.0;
        }
        if (Count == Capacity)
        {
            Total -= Samples[Next];
        }
        else
        {
            ++Count;
        }
        Samples[Next] = InDeltaSeconds;
        Total += InDeltaSeconds;
        Next = (Next + 1u) % Capacity;
    }

    [[nodiscard]] double Average() const noexcept
    {
        return Count != 0 ? Total / static_cast<double>(Count) : 0.0;
    }
    [[nodiscard]] double Min() const noexcept
    {
        double ScopedMin = 0.0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < Count; ++ScopedIndex)
        {
            if (ScopedIndex == 0 || Samples[ScopedIndex] < ScopedMin)
            {
                ScopedMin = Samples[ScopedIndex];
            }
        }
        return ScopedMin;
    }
    [[nodiscard]] double Max() const noexcept
    {
        double ScopedMax = 0.0;
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < Count; ++ScopedIndex)
        {
            if (ScopedIndex == 0 || Samples[ScopedIndex] > ScopedMax)
            {
                ScopedMax = Samples[ScopedIndex];
            }
        }
        return ScopedMax;
    }
    // Frames per second of the rolling average; 0 before the first sample.
    [[nodiscard]] double Fps() const noexcept
    {
        const double ScopedAverage = Average();
        return ScopedAverage > 0.0 ? 1.0 / ScopedAverage : 0.0;
    }
    void Reset() noexcept
    {
        Count = 0;
        Next = 0;
        Total = 0.0;
    }
};

// How a duration is rendered. Clock writes HH:MM:SS, Compact writes the
// human-readable "1h 2m 5s" form and Precise keeps milliseconds behind a clock.
enum class DurationStyle : std::uint8_t
{
    Clock = 0,
    Compact,
    Precise,
};

// Blocks the calling thread. Sleeps are the only place the engine still lets
// time pass without a clock tick, and they stay on chrono steady time.
inline void SleepSeconds(double InSeconds) noexcept
{
    if (InSeconds <= 0.0)
    {
        return;
    }
    std::this_thread::sleep_for(std::chrono::duration<double>(InSeconds));
}

inline void SleepMilliseconds(double InMilliseconds) noexcept
{
    SleepSeconds(InMilliseconds / 1000.0);
}

inline void SleepMicroseconds(double InMicroseconds) noexcept
{
    SleepSeconds(InMicroseconds / 1000000.0);
}

// Best-effort local offset in minutes east of UTC for the current instant.
// C++20 time zone support is used when the standard library provides it; a
// platform without it reports UTC, and a caller that wants a specific zone
// passes the offset to the formatters instead.
[[nodiscard]] inline std::int32_t LocalOffsetMinutes() noexcept
{
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907
    if (const std::chrono::time_zone* ScopedZone = std::chrono::current_zone(); ScopedZone != nullptr)
    {
        const auto ScopedInfo = ScopedZone->get_info(std::chrono::system_clock::now());
        return static_cast<std::int32_t>(ScopedInfo.offset.count() / 60);
    }
#endif
    return 0;
}
}
