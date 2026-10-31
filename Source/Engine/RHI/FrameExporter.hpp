// FemboyEngine2D - offscreen sequence exporter (.fbyseq).
#pragma once

#include "Engine/RHI/RHI.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Fby
{
class VirtualFileSystem;
}

namespace Fby::Render
{

inline constexpr char kSequenceMagic[8] = {'F', 'B', 'Y', 'S', 'E', 'Q', '0', '1'};
inline constexpr std::uint32_t kSequenceVersion = 1;
// 'FBMC' little-endian: the last word a complete file carries.
inline constexpr std::uint32_t kSequenceCompletionMarker = 0x434D4246u;
inline constexpr std::size_t kSequenceHeaderBytes = 32;
inline constexpr std::size_t kSequenceFooterBytes = 16;
inline constexpr std::string_view kSequenceExtension = ".fbyseq";
// Hard stop against a typo turning into a 200 GB allocation.
inline constexpr std::uint32_t kMaxSequenceFrames = 1u << 16;

struct SequenceHeader
{
    char magic[8];
    std::uint32_t version;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t fps;
    std::uint32_t frameCount;
    std::uint32_t reserved;
};

struct SequenceFooter
{
    std::uint32_t marker;
    std::uint32_t frameCount;
    std::uint64_t totalBytes;
};

static_assert(sizeof(SequenceHeader) == kSequenceHeaderBytes, ".fbyseq header is fixed at 32 bytes");
static_assert(sizeof(SequenceFooter) == kSequenceFooterBytes, ".fbyseq footer is fixed at 16 bytes");

// What the exporter renders. endFrame is inclusive; step is the frame stride.
struct FrameExportDesc
{
    Rhi::Extent2D size{};
    std::uint32_t fps = 30;
    std::uint32_t startFrame = 0;
    std::uint32_t endFrame = 0;
    std::uint32_t step = 1;
    // Offscreen target format. Sprite captures are display-referred, so sRGB is
    bool srgb = false;
};

// Number of frames a descriptor captures: floor((end-start)/step) + 1.
[[nodiscard]] constexpr std::uint32_t SequenceFrameCount(const FrameExportDesc& InDesc) noexcept
{
    if (InDesc.step == 0 || InDesc.endFrame < InDesc.startFrame)
    {
        return 0;
    }
    return (InDesc.endFrame - InDesc.startFrame) / InDesc.step + 1;
}

[[nodiscard]] constexpr std::uint64_t SequenceFrameBytes(const FrameExportDesc& InDesc) noexcept
{
    return static_cast<std::uint64_t>(InDesc.size.width) * InDesc.size.height * 4;
}

[[nodiscard]] constexpr std::uint64_t SequenceFileBytes(const FrameExportDesc& InDesc) noexcept
{
    return kSequenceHeaderBytes + SequenceFrameBytes(InDesc) * SequenceFrameCount(InDesc) + kSequenceFooterBytes;
}

// Where the finished bytes go. `write` is called exactly once, from End().
struct FrameSink
{
    void* user = nullptr;
    bool (*write)(void* InUser, std::string_view InPath, std::span<const std::byte> InBytes,
                  std::string* InError) = nullptr;
};

// Default sink: VirtualFileSystem::WriteFile into the writable overlay (the
[[nodiscard]] bool WriteSequenceToVfs(void* InUser, std::string_view InPath, std::span<const std::byte> InBytes,
                                      std::string* InError);
[[nodiscard]] FrameSink VfsFrameSink(VirtualFileSystem& InVfs) noexcept;

// The game/editor presents one animation frame into `packet`. The callback owns
using FrameRenderCallback = bool (*)(void* InUser, std::uint32_t InFrameIndex, Rhi::FramePacket& InPacket);

enum class ExportStatus : std::uint8_t
{
    // One frame captured; call again.
    Captured = 0,
    // BeginFrame() said "skip this frame" (minimised window, out-of-date
    Retry,
    // Every frame requested by the descriptor was captured; call End().
    Complete,
    // Hard failure; `error` is filled.
    Error,
};

class FrameExporter
{
public:
    FrameExporter() = default;
    ~FrameExporter();

    FrameExporter(const FrameExporter&) = delete;
    FrameExporter& operator=(const FrameExporter&) = delete;

    // Editor default: writes through the VFS overlay.
    [[nodiscard]] bool Begin(Rhi::RHI& InGpu, VirtualFileSystem& InVfs, std::string_view InPath,
                             const FrameExportDesc& InDesc, FrameRenderCallback InRender, void* InUser,
                             std::string* InError = nullptr);
    // Custom destination.
    [[nodiscard]] bool Begin(Rhi::RHI& InGpu, std::string_view InPath, const FrameExportDesc& InDesc, FrameSink InSink,
                             FrameRenderCallback InRender, void* InUser, std::string* InError = nullptr);

    // Captures the next deterministic frame:
    [[nodiscard]] ExportStatus CaptureNextFrame(std::string* InError = nullptr);

    // Writes the completion footer and publishes the file through the sink.
    [[nodiscard]] bool End(std::string* InError = nullptr);

    // Releases the render target and drops a partial sequence. Safe to call
    void Abort() noexcept;

    [[nodiscard]] bool IsActive() const noexcept
    {
        return MActive;
    }
    [[nodiscard]] std::uint32_t CapturedFrames() const noexcept
    {
        return MCaptured;
    }
    [[nodiscard]] std::uint32_t PlannedFrames() const noexcept
    {
        return MPlanned;
    }
    // Deterministic counter: the index CaptureNextFrame() will capture next.
    [[nodiscard]] std::uint32_t NextFrameIndex() const noexcept;
    [[nodiscard]] const FrameExportDesc& Description() const noexcept
    {
        return MDesc;
    }
    [[nodiscard]] std::string_view Path() const noexcept
    {
        return MPath;
    }

private:
    bool BeginCommon(Rhi::RHI& InGpu, std::string_view InPath, const FrameExportDesc& InDesc, FrameSink InSink,
                     FrameRenderCallback InRender, void* InUser, std::string* InError);

    Rhi::RHI* MGpu = nullptr;
    Rhi::RenderTargetHandle MTarget{};
    FrameSink MSink{};
    FrameRenderCallback MRender = nullptr;
    void* MRenderUser = nullptr;
    FrameExportDesc MDesc{};
    SequenceHeader MHeader{};
    std::string MPath{};
    std::vector<std::byte> MImage{};
    Rhi::FramePacket MPacket{};
    std::uint32_t MPlanned = 0;
    std::uint32_t MCaptured = 0;
    bool MActive = false;
};

// Validates a finished .fbyseq image: magic, version, dimensions, frame count,
[[nodiscard]] bool ValidateSequence(std::span<const std::byte> InBytes, std::uint32_t* InFrameCount = nullptr,
                                    std::string* InError = nullptr);
}
