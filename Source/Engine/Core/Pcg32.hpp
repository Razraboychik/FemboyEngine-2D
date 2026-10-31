// FemboyEngine2D - PCG32: the deterministic simulation generator.
#pragma once

#include <cstdint>
#include <type_traits>

namespace Fby
{

// The complete generator state. Snapshot v2 stores these 16 bytes in its header;
struct RngState
{
    std::uint64_t state = 0;
    std::uint64_t inc = 1;

    [[nodiscard]] friend constexpr bool operator==(const RngState&, const RngState&) = default;
};

// PCG32 (XSH-RR 64/32). A single 64-bit LCG step feeds a 32-bit xorshift and a
class Pcg32
{
public:
    static constexpr std::uint64_t kMultiplier = 6364136223846793005ull;
    static constexpr std::uint64_t kDefaultStream = 0x9E3779B97F4A7C15ull;

    constexpr Pcg32() = default;

    // PCG's reference seeding procedure. `stream` becomes the odd LCG increment
    [[nodiscard]] static constexpr Pcg32 FromSeed(std::uint64_t InSeed,
                                                  std::uint64_t InStream = kDefaultStream) noexcept
    {
        Pcg32 ScopedRng;
        ScopedRng.Seed(InSeed, InStream);
        return ScopedRng;
    }

    constexpr void Seed(std::uint64_t InSeed, std::uint64_t InStream = kDefaultStream) noexcept
    {
        MState = 0;
        MInc = (InStream << 1u) | 1u;
        (void)NextU32();
        MState += InSeed;
        (void)NextU32();
    }

    // One PCG32 output word. This is the exact reference algorithm:
    [[nodiscard]] constexpr std::uint32_t NextU32() noexcept
    {
        const std::uint64_t ScopedOld = MState;
        MState = ScopedOld * kMultiplier + MInc;
        const std::uint32_t ScopedXorshifted = static_cast<std::uint32_t>(((ScopedOld >> 18u) ^ ScopedOld) >> 27u);
        const std::uint32_t ScopedRotation = static_cast<std::uint32_t>(ScopedOld >> 59u);
        return (ScopedXorshifted >> ScopedRotation) | (ScopedXorshifted << ((0u - ScopedRotation) & 31u));
    }

    // Two words, low first. Stable order matters for a serialized game stream.
    [[nodiscard]] constexpr std::uint64_t NextU64() noexcept
    {
        const std::uint64_t ScopedLow = NextU32();
        const std::uint64_t ScopedHigh = NextU32();
        return ScopedLow | (ScopedHigh << 32u);
    }

    // [0, 1) with 24 bits of mantissa. Pure integer arithmetic, therefore
    [[nodiscard]] constexpr float NextFloat() noexcept
    {
        return static_cast<float>(NextU32() >> 8u) * (1.0f / 16777216.0f);
    }

    // [0, 1) in double precision, 53 bits. Used by editors and tools where a
    [[nodiscard]] constexpr double NextDouble() noexcept
    {
        return static_cast<double>(NextU64() >> 11u) * (1.0 / 9007199254740992.0);
    }

    // Legacy bounded draw. Multiply-high is branch-free and one draw; the bias
    [[nodiscard]] constexpr std::uint32_t Range(std::uint32_t InBound) noexcept
    {
        if (InBound == 0)
        {
            return 0;
        }
        const std::uint64_t ScopedProduct = static_cast<std::uint64_t>(NextU32()) * static_cast<std::uint64_t>(InBound);
        return static_cast<std::uint32_t>(ScopedProduct >> 32u);
    }

    // Unbiased bounded draw (Lemire's method with rejection). Prefer this for
    [[nodiscard]] constexpr std::uint32_t Bounded(std::uint32_t InBound) noexcept
    {
        if (InBound == 0)
        {
            return 0;
        }
        std::uint64_t ScopedProduct = static_cast<std::uint64_t>(NextU32()) * static_cast<std::uint64_t>(InBound);
        auto ScopedLow = static_cast<std::uint32_t>(ScopedProduct);
        if (ScopedLow < InBound)
        {
            const std::uint32_t ScopedThreshold = static_cast<std::uint32_t>(0u - InBound) % InBound;
            while (ScopedLow < ScopedThreshold)
            {
                ScopedProduct = static_cast<std::uint64_t>(NextU32()) * static_cast<std::uint64_t>(InBound);
                ScopedLow = static_cast<std::uint32_t>(ScopedProduct);
            }
        }
        return static_cast<std::uint32_t>(ScopedProduct >> 32u);
    }

    [[nodiscard]] constexpr std::int32_t BoundedInt(std::int32_t InMinimum, std::int32_t InMaximum) noexcept
    {
        if (InMaximum <= InMinimum)
        {
            return InMinimum;
        }
        const auto ScopedSpan = static_cast<std::uint32_t>(static_cast<std::int64_t>(InMaximum) - InMinimum + 1);
        return InMinimum + static_cast<std::int32_t>(Bounded(ScopedSpan));
    }

    [[nodiscard]] constexpr float RangeFloat(float InMinimum, float InMaximum) noexcept
    {
        return InMinimum + (InMaximum - InMinimum) * NextFloat();
    }

    // Save/restore is what makes rollback exact. The increment is part of the
    [[nodiscard]] constexpr RngState Save() const noexcept
    {
        return RngState{MState, MInc};
    }

    constexpr void Restore(const RngState& InState) noexcept
    {
        MState = InState.state;
        MInc = InState.inc | 1u;
    }

    [[nodiscard]] constexpr std::uint64_t State() const noexcept
    {
        return MState;
    }
    [[nodiscard]] constexpr std::uint64_t Increment() const noexcept
    {
        return MInc;
    }

    // A child stream for a subsystem (particles, loot, noise). The parent is
    [[nodiscard]] constexpr Pcg32 Fork() noexcept
    {
        const std::uint64_t ScopedStream = (static_cast<std::uint64_t>(NextU32()) << 32u) | NextU32();
        Pcg32 ScopedChild;
        ScopedChild.MState = MState;
        ScopedChild.MInc = (ScopedStream << 1u) | 1u;
        return ScopedChild;
    }

private:
    std::uint64_t MState = 0;
    std::uint64_t MInc = 1;
};

static_assert(std::is_trivially_copyable_v<Pcg32>);
static_assert(std::is_trivially_copyable_v<RngState>);
static_assert(sizeof(Pcg32) == 16, "PCG32 has exactly 128 bits of state");
static_assert(sizeof(RngState) == 16);
}
