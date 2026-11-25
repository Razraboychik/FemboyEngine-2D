// FemboyEngine2D - parallel iteration over an FCS view.
#pragma once

#include "Engine/Core/JobSystem.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace Fby::Ecs
{

template <typename View, typename Function>
void ParallelForEach(JobSystem& InJobs, View& InView, std::uint32_t InGrain, Function&& InFunction)
{
    const auto ScopedSize = static_cast<std::uint32_t>(InView.size());
    if (ScopedSize == 0)
    {
        return;
    }
    // A zero grain would divide by zero and never advance the boundary loop.
    const std::uint32_t ScopedGrain = InGrain != 0u ? InGrain : 1u;

    // Reentrant or tiny work stays on the calling thread: submitting one task
    if (ScopedSize <= ScopedGrain || JobSystem::IsWorkerThread())
    {
        for (auto ScopedIt = InView.begin(); ScopedIt != InView.end(); ++ScopedIt)
        {
            InFunction(static_cast<decltype(*ScopedIt)>(*ScopedIt), InView);
        }
        return;
    }

    std::vector<typename View::iterator> ScopedBoundaries;
    ScopedBoundaries.reserve(static_cast<std::size_t>(ScopedSize / ScopedGrain) + 2);

    auto ScopedIt = InView.begin();
    while (ScopedIt != InView.end())
    {
        ScopedBoundaries.push_back(ScopedIt);
        for (std::uint32_t ScopedStep = 0; ScopedStep < ScopedGrain && ScopedIt != InView.end(); ++ScopedStep)
        {
            ++ScopedIt;
        }
    }
    ScopedBoundaries.push_back(InView.end());

    const auto ScopedChunkCount = static_cast<std::uint32_t>(ScopedBoundaries.size() - 1);
    InJobs.ParallelFor(ScopedChunkCount, 1,
                       [&](std::uint32_t InFirst, std::uint32_t InLast)
                       {
                           for (std::uint32_t ScopedChunk = InFirst; ScopedChunk < InLast; ++ScopedChunk)
                           {
                               for (auto ScopedEntity = ScopedBoundaries[ScopedChunk];
                                    ScopedEntity != ScopedBoundaries[ScopedChunk + 1]; ++ScopedEntity)
                               {
                                   InFunction(static_cast<decltype(*ScopedEntity)>(*ScopedEntity), InView);
                               }
                           }
                       });
}
}
