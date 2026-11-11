// FemboyEngine2D - fixed-capacity frame profiler.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace Fby::Core
{

inline constexpr std::uint32_t kProfileMaxCategories = 8;
inline constexpr std::uint32_t kProfileDefaultSamples = 4096;

struct ProfileStats
{
    std::uint64_t Count = 0;
    double TotalMs = 0.0;
    double MinMs = 0.0;
    double MaxMs = 0.0;
    double AverageMs = 0.0;
    double P95Ms = 0.0;
};

// One retained timing, timestamped against the profiler epoch so a trace export
// can place it on a timeline.
struct ProfileSample
{
    std::uint64_t StartNanoseconds = 0;
    std::uint64_t DurationNanoseconds = 0;
    std::uint32_t Category = 0;

    [[nodiscard]] double Milliseconds() const noexcept
    {
        return static_cast<double>(DurationNanoseconds) / 1000000.0;
    }
};

// A ring of timing samples per category. The profiler is single-threaded by
// design: the sim is deterministic and one thread records, so there is no atomic
// in the hot path. Overflow drops the oldest sample.
class Profiler
{
public:
    explicit Profiler(std::uint32_t InCapacity = kProfileDefaultSamples)
        : MCapacity(std::max(InCapacity, 1u)), MBaseNanoseconds(NowNanoseconds())
    {
        MSamples.resize(MCapacity);
        MScratch.reserve(MCapacity);
        std::fill(MHeader.begin(), MHeader.end(), 0u);
    }

    void Reset() noexcept
    {
        MCount = 0;
        MWrite = 0;
        std::fill(MHeader.begin(), MHeader.end(), 0u);
    }

    void Record(std::uint32_t InCategory, double InMilliseconds) noexcept
    {
        constexpr double kMaxMilliseconds = static_cast<double>(std::numeric_limits<std::uint64_t>::max()) / 1000000.0;
        const double ScopedMilliseconds =
            std::isfinite(InMilliseconds) && InMilliseconds > 0.0 ? InMilliseconds : 0.0;
        const std::uint64_t ScopedDuration =
            ScopedMilliseconds >= kMaxMilliseconds
                ? std::numeric_limits<std::uint64_t>::max()
                : static_cast<std::uint64_t>(ScopedMilliseconds * 1000000.0);
        const std::uint64_t ScopedNow = NowNanoseconds() - MBaseNanoseconds;
        RecordAt(InCategory, ScopedNow >= ScopedDuration ? ScopedNow - ScopedDuration : 0, ScopedDuration);
    }

    void RecordAt(std::uint32_t InCategory, std::uint64_t InStartNanoseconds,
                  std::uint64_t InDurationNanoseconds) noexcept
    {
        if (InCategory >= kProfileMaxCategories)
        {
            return;
        }
        ProfileSample& ScopedSlot = MSamples[MWrite];
        ScopedSlot.StartNanoseconds = InStartNanoseconds;
        ScopedSlot.DurationNanoseconds = InDurationNanoseconds;
        ScopedSlot.Category = InCategory;
        MWrite = (MWrite + 1) % MCapacity;
        if (MCount < MCapacity)
        {
            ++MCount;
        }
        ++MHeader[InCategory];
    }

    [[nodiscard]] std::uint32_t SampleCount() const noexcept
    {
        return MCount;
    }

    // Samples in chronological order: index 0 is the oldest still retained.
    [[nodiscard]] ProfileSample SampleAt(std::uint32_t InIndex) const noexcept
    {
        const std::uint32_t ScopedStart = MCount == MCapacity ? MWrite : 0;
        return MSamples[(ScopedStart + InIndex) % MCapacity];
    }

    // Aggregates over the samples still in the ring. The percentile uses the
    // nearest-rank definition, so it is stable for a deterministic trace. The
    // scratch vector keeps its capacity across calls, so a panel that refreshes
    // every frame does not allocate eight times per frame.
    [[nodiscard]] ProfileStats Stats(std::uint32_t InCategory) const noexcept
    {
        ProfileStats ScopedStats;
        if (InCategory >= kProfileMaxCategories)
        {
            return ScopedStats;
        }
        MScratch.clear();
        MScratch.reserve(MCount);
        for (std::uint32_t ScopedIndex = 0; ScopedIndex < MCount; ++ScopedIndex)
        {
            const ProfileSample ScopedSample = SampleAt(ScopedIndex);
            if (ScopedSample.Category != InCategory)
            {
                continue;
            }
            MScratch.push_back(ScopedSample.Milliseconds());
        }
        if (MScratch.empty())
        {
            return ScopedStats;
        }
        std::sort(MScratch.begin(), MScratch.end());
        double ScopedTotal = 0.0;
        for (const double ScopedValue : MScratch)
        {
            ScopedTotal += ScopedValue;
        }
        ScopedStats.Count = MScratch.size();
        ScopedStats.TotalMs = ScopedTotal;
        ScopedStats.MinMs = MScratch.front();
        ScopedStats.MaxMs = MScratch.back();
        ScopedStats.AverageMs = ScopedTotal / static_cast<double>(MScratch.size());
        const std::size_t ScopedRank =
            static_cast<std::size_t>(0.95 * static_cast<double>(MScratch.size()) + 0.999999);
        ScopedStats.P95Ms = MScratch[std::min(ScopedRank, MScratch.size()) - 1];
        return ScopedStats;
    }

    // RAII scope: records into the given category on destruction.
    class Scope
    {
    public:
        Scope(Profiler& InProfiler, std::uint32_t InCategory) noexcept
            : MProfiler(&InProfiler), MCategory(InCategory),
              MStartNanoseconds(Profiler::NowNanoseconds() - InProfiler.MBaseNanoseconds)
        {
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&& InOther) noexcept
            : MProfiler(InOther.MProfiler), MCategory(InOther.MCategory),
              MStartNanoseconds(InOther.MStartNanoseconds)
        {
            InOther.MProfiler = nullptr;
        }
        ~Scope()
        {
            if (MProfiler == nullptr)
            {
                return;
            }
            const std::uint64_t ScopedNow = Profiler::NowNanoseconds() - MProfiler->MBaseNanoseconds;
            const std::uint64_t ScopedDuration = ScopedNow >= MStartNanoseconds ? ScopedNow - MStartNanoseconds : 0;
            MProfiler->RecordAt(MCategory, MStartNanoseconds, ScopedDuration);
        }

    private:
        Profiler* MProfiler = nullptr;
        std::uint32_t MCategory = 0;
        std::uint64_t MStartNanoseconds = 0;
    };

    [[nodiscard]] Scope BeginRound(std::uint32_t InCategory) noexcept
    {
        return Scope(*this, InCategory);
    }

private:
    [[nodiscard]] static std::uint64_t NowNanoseconds() noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    std::uint32_t MCapacity = kProfileDefaultSamples;
    std::uint64_t MBaseNanoseconds = 0;
    std::vector<ProfileSample> MSamples;
    // Reused by Stats; mutable because Stats is a const query.
    mutable std::vector<double> MScratch;
    std::uint32_t MCount = 0;
    std::uint32_t MWrite = 0;
    // Per-category lifetime counters, kept even after a sample leaves the ring.
    std::array<std::uint64_t, kProfileMaxCategories> MHeader{};
};

}
