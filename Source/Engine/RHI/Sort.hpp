// FemboyEngine2D - key sorting.
#pragma once

#include "Engine/RHI/RenderItem.hpp"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace Fby::Rhi
{

class KeySorter
{
public:
    // `order` starts as the identity permutation and is left holding the sorted
    void Sort(std::span<const RenderKey> InKeys, std::span<std::uint32_t> InOrder)
    {
        const auto ScopedCount = static_cast<std::uint32_t>(InOrder.size());
        if (ScopedCount < 2)
        {
            return;
        }

        if (MCounts.size() < kBuckets)
        {
            MCounts.resize(kBuckets);
        }
        if (MScratch.size() < ScopedCount)
        {
            MScratch.resize(ScopedCount);
        }

        std::uint32_t* ScopedSource = InOrder.data();
        std::uint32_t* ScopedDestination = MScratch.data();

        // Four passes of 16 bits cover the whole key.
        for (int ScopedPass = 0; ScopedPass < 4; ++ScopedPass)
        {
            const auto ScopedShift = static_cast<unsigned>(ScopedPass * 16);
            std::fill(MCounts.begin(), MCounts.begin() + kBuckets, 0u);

            for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
            {
                ++MCounts[(InKeys[ScopedSource[ScopedI]] >> ScopedShift) & 0xFFFFu];
            }

            std::uint32_t ScopedRunning = 0;
            for (std::uint32_t ScopedBucket = 0; ScopedBucket < kBuckets; ++ScopedBucket)
            {
                const std::uint32_t ScopedSize = MCounts[ScopedBucket];
                MCounts[ScopedBucket] = ScopedRunning;
                ScopedRunning += ScopedSize;
            }

            for (std::uint32_t ScopedI = 0; ScopedI < ScopedCount; ++ScopedI)
            {
                const std::uint32_t ScopedIndex = ScopedSource[ScopedI];
                const std::uint32_t ScopedBucket =
                    static_cast<std::uint32_t>((InKeys[ScopedIndex] >> ScopedShift) & 0xFFFFu);
                ScopedDestination[MCounts[ScopedBucket]++] = ScopedIndex;
            }

            std::swap(ScopedSource, ScopedDestination);
        }

        // An even number of passes leaves the result in the caller's buffer; the
        if (ScopedSource != InOrder.data())
        {
            std::copy(ScopedSource, ScopedSource + ScopedCount, InOrder.data());
        }
    }

private:
    static constexpr std::uint32_t kBuckets = 65536;

    std::vector<std::uint32_t> MCounts;
    std::vector<std::uint32_t> MScratch;
};
}
