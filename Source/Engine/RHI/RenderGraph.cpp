// FemboyEngine2D - render graph dependency resolution.
#include "Engine/RHI/RenderGraph.hpp"

#include <algorithm>
#include <cstring>

namespace Fby::Rhi
{
namespace
{

[[nodiscard]] bool SameName(const char* InLeft, const char* InRight) noexcept
{
    return InLeft != nullptr && InRight != nullptr && std::strcmp(InLeft, InRight) == 0;
}

[[nodiscard]] bool Contains(const std::array<std::uint64_t, kMaxPassReads>& InValues, std::uint8_t InCount,
                            std::uint64_t InValue) noexcept
{
    for (std::uint8_t ScopedI = 0; ScopedI < InCount; ++ScopedI)
    {
        if (InValues[ScopedI] == InValue)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool ContainsWrite(const std::array<std::uint64_t, kMaxPassWrites>& InValues, std::uint8_t InCount,
                                 std::uint64_t InValue) noexcept
{
    for (std::uint8_t ScopedI = 0; ScopedI < InCount; ++ScopedI)
    {
        if (InValues[ScopedI] == InValue)
        {
            return true;
        }
    }
    return false;
}
}

bool RenderGraph::AddPass(const RenderPassDesc& InDesc) noexcept
{
    if (InDesc.name == nullptr || InDesc.execute == nullptr || MPassCount >= kMaxRenderPasses ||
        InDesc.readCount > kMaxPassReads || InDesc.writeCount > kMaxPassWrites)
    {
        return false;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MPassCount; ++ScopedI)
    {
        if (SameName(MPasses[ScopedI].name, InDesc.name))
        {
            return false;
        }
    }
    RenderGraphPass& ScopedPass = MPasses[MPassCount++];
    ScopedPass = RenderGraphPass{};
    ScopedPass.name = InDesc.name;
    ScopedPass.kind = InDesc.kind;
    ScopedPass.execute = InDesc.execute;
    ScopedPass.user = InDesc.user;
    ScopedPass.reads = InDesc.reads;
    ScopedPass.readCount = InDesc.readCount;
    ScopedPass.writes = InDesc.writes;
    ScopedPass.writeCount = InDesc.writeCount;
    ScopedPass.enabled = true;
    MCompiled = false;
    return true;
}

bool RenderGraph::Compile() noexcept
{
    MOrderCount = 0;
    MCompiled = false;
    std::array<std::uint64_t, kMaxRenderPasses> ScopedEdges{};
    std::array<std::uint32_t, kMaxRenderPasses> ScopedIndegree{};

    for (std::uint32_t ScopedConsumer = 0; ScopedConsumer < MPassCount; ++ScopedConsumer)
    {
        const RenderGraphPass& ScopedPass = MPasses[ScopedConsumer];
        for (std::uint8_t ScopedRead = 0; ScopedRead < ScopedPass.readCount; ++ScopedRead)
        {
            const std::uint64_t ScopedResource = ScopedPass.reads[ScopedRead];
            for (std::uint32_t ScopedProducer = ScopedConsumer; ScopedProducer > 0; --ScopedProducer)
            {
                const std::uint32_t ScopedIndex = ScopedProducer - 1;
                if (ContainsWrite(MPasses[ScopedIndex].writes, MPasses[ScopedIndex].writeCount, ScopedResource))
                {
                    const std::uint64_t ScopedBit = 1ull << ScopedConsumer;
                    if ((ScopedEdges[ScopedIndex] & ScopedBit) == 0)
                    {
                        ScopedEdges[ScopedIndex] |= ScopedBit;
                        ++ScopedIndegree[ScopedConsumer];
                    }
                    break;
                }
            }
        }
        for (std::uint8_t ScopedWrite = 0; ScopedWrite < ScopedPass.writeCount; ++ScopedWrite)
        {
            const std::uint64_t ScopedResource = ScopedPass.writes[ScopedWrite];
            for (std::uint32_t ScopedProducer = ScopedConsumer; ScopedProducer > 0; --ScopedProducer)
            {
                const std::uint32_t ScopedIndex = ScopedProducer - 1;
                if (ContainsWrite(MPasses[ScopedIndex].writes, MPasses[ScopedIndex].writeCount, ScopedResource))
                {
                    const std::uint64_t ScopedBit = 1ull << ScopedConsumer;
                    if ((ScopedEdges[ScopedIndex] & ScopedBit) == 0)
                    {
                        ScopedEdges[ScopedIndex] |= ScopedBit;
                        ++ScopedIndegree[ScopedConsumer];
                    }
                    break;
                }
            }
            // A write after a read is a WAR hazard and must not be reordered
            for (std::uint32_t ScopedReader = ScopedConsumer; ScopedReader > 0; --ScopedReader)
            {
                const std::uint32_t ScopedIndex = ScopedReader - 1;
                if (Contains(MPasses[ScopedIndex].reads, MPasses[ScopedIndex].readCount, ScopedResource) &&
                    (ScopedEdges[ScopedIndex] & (1ull << ScopedConsumer)) == 0)
                {
                    ScopedEdges[ScopedIndex] |= 1ull << ScopedConsumer;
                    ++ScopedIndegree[ScopedConsumer];
                }
            }
        }
    }

    // Kahn's algorithm with an explicit stack. Insertion order is the tie-break,
    std::array<bool, kMaxRenderPasses> ScopedDone{};
    for (std::uint32_t ScopedStep = 0; ScopedStep < MPassCount; ++ScopedStep)
    {
        std::uint32_t ScopedNext = kMaxRenderPasses;
        for (std::uint32_t ScopedI = 0; ScopedI < MPassCount; ++ScopedI)
        {
            if (!ScopedDone[ScopedI] && ScopedIndegree[ScopedI] == 0)
            {
                ScopedNext = ScopedI;
                break;
            }
        }
        if (ScopedNext == kMaxRenderPasses)
        {
            return false;
        }
        ScopedDone[ScopedNext] = true;
        MOrder[MOrderCount++] = ScopedNext;
        for (std::uint32_t ScopedConsumer = 0; ScopedConsumer < MPassCount; ++ScopedConsumer)
        {
            if ((ScopedEdges[ScopedNext] & (1ull << ScopedConsumer)) != 0)
            {
                if (ScopedIndegree[ScopedConsumer] > 0)
                {
                    --ScopedIndegree[ScopedConsumer];
                }
            }
        }
    }
    MCompiled = MOrderCount == MPassCount;
    return MCompiled;
}

void RenderGraph::Execute(RenderGraphContext& InContext) const noexcept
{
    if (!MCompiled)
    {
        return;
    }
    for (std::uint32_t ScopedI = 0; ScopedI < MOrderCount; ++ScopedI)
    {
        const RenderGraphPass& ScopedPass = MPasses[MOrder[ScopedI]];
        if (ScopedPass.enabled && ScopedPass.execute != nullptr)
        {
            ScopedPass.execute(InContext, ScopedPass.user);
        }
    }
}

void RenderGraph::SetEnabled(std::string_view InName, bool InEnabled) noexcept
{
    for (std::uint32_t ScopedI = 0; ScopedI < MPassCount; ++ScopedI)
    {
        if (InName == std::string_view(MPasses[ScopedI].name))
        {
            MPasses[ScopedI].enabled = InEnabled;
        }
    }
}
}
