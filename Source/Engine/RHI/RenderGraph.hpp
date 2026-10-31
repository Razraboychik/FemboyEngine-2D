// FemboyEngine2D - allocation-free render graph.
#pragma once

#include "Engine/Core/Types.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Fby
{
class CameraStackSystem;
}

namespace Fby::Rhi
{

inline constexpr std::uint32_t kMaxRenderPasses = 32;
inline constexpr std::uint32_t kMaxPassReads = 8;
inline constexpr std::uint32_t kMaxPassWrites = 4;

enum class RenderPassKind : std::uint8_t
{
    Graphics = 0,
    Compute = 1,
    Copy = 2,
};

enum class RenderResourceKind : std::uint8_t
{
    Texture = 0,
    Buffer = 1,
    ShadowMap = 2,
};

[[nodiscard]] constexpr std::uint64_t RenderResourceHash(std::string_view InName) noexcept
{
    std::uint64_t ScopedHash = 14695981039346656037ull;
    for (const char ScopedCharacter : InName)
    {
        ScopedHash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(ScopedCharacter));
        ScopedHash *= 1099511628211ull;
    }
    return ScopedHash;
}

// One recorded unit of GPU work. The RHI backend turns this into a pipeline bind,
struct RenderCommandRecord
{
    const char* pipelineName = nullptr;
    const void* constants = nullptr;
    std::uint32_t constantsBytes = 0;
    std::uint32_t groupCountX = 0;
    std::uint32_t groupCountY = 0;
    std::uint32_t groupCountZ = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Fullscreen graphics records default to one instance; a backend that
    // executes them must honour this for instanced passes.
    std::uint32_t instanceCount = 1;
    RenderPassKind kind = RenderPassKind::Graphics;
    std::uint32_t flags = 0;
};

struct RenderGraphContext
{
    RHI* renderer = nullptr;
    const FramePacket* frame = nullptr;
    CameraStackSystem* cameras = nullptr;
    float timeSeconds = 0.0f;
    std::uint64_t frameIndex = 0;
    Extent2D extent{};
    void* user = nullptr;
    std::span<RenderCommandRecord> records{};
    std::uint32_t* recordCount = nullptr;

    [[nodiscard]] bool Push(const RenderCommandRecord& InRecord) noexcept
    {
        if (recordCount == nullptr || records.empty() || *recordCount >= records.size())
        {
            return false;
        }
        records[(*recordCount)++] = InRecord;
        return true;
    }
};

using RenderPassExecute = void (*)(RenderGraphContext&, void*);

struct RenderPassDesc
{
    const char* name = nullptr;
    RenderPassKind kind = RenderPassKind::Graphics;
    RenderPassExecute execute = nullptr;
    void* user = nullptr;
    std::array<std::uint64_t, kMaxPassReads> reads{};
    std::uint8_t readCount = 0;
    std::array<std::uint64_t, kMaxPassWrites> writes{};
    std::uint8_t writeCount = 0;
};

struct RenderGraphPass
{
    const char* name = nullptr;
    RenderPassKind kind = RenderPassKind::Graphics;
    RenderPassExecute execute = nullptr;
    void* user = nullptr;
    std::array<std::uint64_t, kMaxPassReads> reads{};
    std::uint8_t readCount = 0;
    std::array<std::uint64_t, kMaxPassWrites> writes{};
    std::uint8_t writeCount = 0;
    bool enabled = true;
};

class RenderGraph
{
public:
    RenderGraph() = default;

    [[nodiscard]] bool AddPass(const RenderPassDesc& InDesc) noexcept;
    [[nodiscard]] bool Compile() noexcept;
    void Execute(RenderGraphContext& InContext) const noexcept;
    void Clear() noexcept
    {
        MPassCount = 0;
        MOrderCount = 0;
        MCompiled = false;
    }
    void SetEnabled(std::string_view InName, bool InEnabled) noexcept;

    [[nodiscard]] bool Compiled() const noexcept
    {
        return MCompiled;
    }
    [[nodiscard]] std::uint32_t PassCount() const noexcept
    {
        return MPassCount;
    }
    [[nodiscard]] std::uint32_t OrderCount() const noexcept
    {
        return MOrderCount;
    }
    [[nodiscard]] const RenderGraphPass& Pass(std::uint32_t InIndex) const noexcept
    {
        return MPasses[InIndex];
    }
    [[nodiscard]] std::uint32_t OrderAt(std::uint32_t InIndex) const noexcept
    {
        return MOrder[InIndex];
    }

private:
    std::array<RenderGraphPass, kMaxRenderPasses> MPasses{};
    std::array<std::uint32_t, kMaxRenderPasses> MOrder{};
    std::uint32_t MPassCount = 0;
    std::uint32_t MOrderCount = 0;
    bool MCompiled = false;
};

// Common reset helpers shared by passes. Each pass writes its constants before
template <typename Constants>
[[nodiscard]] RenderCommandRecord MakeComputeRecord(const char* InPipeline, const Constants& InConstants,
                                                    std::uint32_t InGroupsX, std::uint32_t InGroupsY = 1,
                                                    std::uint32_t InGroupsZ = 1) noexcept
{
    static_assert(std::is_trivially_copyable_v<Constants>);
    RenderCommandRecord ScopedRecord{};
    ScopedRecord.pipelineName = InPipeline;
    ScopedRecord.constants = &InConstants;
    ScopedRecord.constantsBytes = static_cast<std::uint32_t>(sizeof(Constants));
    ScopedRecord.groupCountX = InGroupsX;
    ScopedRecord.groupCountY = InGroupsY;
    ScopedRecord.groupCountZ = InGroupsZ;
    ScopedRecord.kind = RenderPassKind::Compute;
    return ScopedRecord;
}

template <typename Constants>
[[nodiscard]] RenderCommandRecord MakeGraphicsRecord(const char* InPipeline, const Constants& InConstants,
                                                     std::uint32_t InWidth, std::uint32_t InHeight,
                                                     std::uint32_t InInstanceCount = 1) noexcept
{
    static_assert(std::is_trivially_copyable_v<Constants>);
    RenderCommandRecord ScopedRecord{};
    ScopedRecord.pipelineName = InPipeline;
    ScopedRecord.constants = &InConstants;
    ScopedRecord.constantsBytes = static_cast<std::uint32_t>(sizeof(Constants));
    ScopedRecord.width = InWidth;
    ScopedRecord.height = InHeight;
    ScopedRecord.instanceCount = InInstanceCount;
    ScopedRecord.kind = RenderPassKind::Graphics;
    return ScopedRecord;
}
}
