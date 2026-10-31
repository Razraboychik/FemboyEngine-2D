// FemboyEngine2D - .fbyseq sequence exporter.
#include "Engine/RHI/FrameExporter.hpp"

#include "Engine/Assets/VFS.hpp"
#include "Engine/Core/Log.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace Fby::Render
{
namespace
{

void SetError(std::string* InError, std::string InMessage)
{
    if (InError != nullptr)
    {
        *InError = std::move(InMessage);
    }
}
}

bool WriteSequenceToVfs(void* InUser, std::string_view InPath, std::span<const std::byte> InBytes, std::string* InError)
{
    auto* ScopedVfs = static_cast<VirtualFileSystem*>(InUser);
    if (ScopedVfs == nullptr)
    {
        SetError(InError, "frame sink has no VirtualFileSystem");
        return false;
    }
    if (!ScopedVfs->WriteFile(InPath, InBytes, InError))
    {
        if (InError != nullptr && InError->empty())
        {
            *InError = "VirtualFileSystem refused the sequence write (no writable directory mount)";
        }
        return false;
    }
    return true;
}

FrameSink VfsFrameSink(VirtualFileSystem& InVfs) noexcept
{
    return FrameSink{&InVfs, &WriteSequenceToVfs};
}

FrameExporter::~FrameExporter()
{
    Abort();
}

bool FrameExporter::Begin(Rhi::RHI& InGpu, VirtualFileSystem& InVfs, std::string_view InPath,
                          const FrameExportDesc& InDesc, FrameRenderCallback InRender, void* InUser,
                          std::string* InError)
{
    return Begin(InGpu, InPath, InDesc, VfsFrameSink(InVfs), InRender, InUser, InError);
}

bool FrameExporter::Begin(Rhi::RHI& InGpu, std::string_view InPath, const FrameExportDesc& InDesc, FrameSink InSink,
                          FrameRenderCallback InRender, void* InUser, std::string* InError)
{
    return BeginCommon(InGpu, InPath, InDesc, InSink, InRender, InUser, InError);
}

bool FrameExporter::BeginCommon(Rhi::RHI& InGpu, std::string_view InPath, const FrameExportDesc& InDesc,
                                FrameSink InSink, FrameRenderCallback InRender, void* InUser, std::string* InError)
{
    Abort();

    if (InRender == nullptr)
    {
        SetError(InError, "FrameExporter needs a render callback");
        return false;
    }
    if (InSink.write == nullptr)
    {
        SetError(InError, "FrameExporter needs a FrameSink writer");
        return false;
    }
    if (InDesc.size.width == 0 || InDesc.size.height == 0 || InDesc.size.width > 16384 || InDesc.size.height > 16384)
    {
        SetError(InError, "frame export size is out of range");
        return false;
    }
    if (InDesc.fps == 0 || InDesc.fps > 240)
    {
        SetError(InError, "frame export fps must be 1..240");
        return false;
    }
    if (InDesc.step == 0)
    {
        SetError(InError, "frame export step must be at least 1");
        return false;
    }
    if (InDesc.endFrame < InDesc.startFrame)
    {
        SetError(InError, "frame export range is empty (endFrame < startFrame)");
        return false;
    }
    const std::uint32_t ScopedPlanned = SequenceFrameCount(InDesc);
    if (ScopedPlanned == 0 || ScopedPlanned > kMaxSequenceFrames)
    {
        SetError(InError, "frame export range produces too many frames");
        return false;
    }
    const std::uint64_t ScopedTotalBytes64 = SequenceFileBytes(InDesc);
    if (ScopedTotalBytes64 > std::numeric_limits<std::size_t>::max())
    {
        SetError(InError, "frame export image is too large for this platform");
        return false;
    }

    const Rhi::RenderTargetDesc ScopedTargetDesc{InDesc.size, InDesc.srgb};
    const Rhi::RenderTargetHandle ScopedTarget = InGpu.CreateRenderTarget(ScopedTargetDesc);
    if (!ScopedTarget.IsValid())
    {
        SetError(InError, "RHI refused the offscreen render target (backend has no offscreen support)");
        return false;
    }

    // One allocation for the whole sequence: header, every frame, footer. The
    MImage.assign(static_cast<std::size_t>(ScopedTotalBytes64), std::byte{0});

    MDesc = InDesc;
    MPlanned = ScopedPlanned;
    MCaptured = 0;
    MGpu = &InGpu;
    MTarget = ScopedTarget;
    MSink = InSink;
    MRender = InRender;
    MRenderUser = InUser;
    MPath.assign(InPath);

    MHeader = SequenceHeader{};
    std::memcpy(MHeader.magic, kSequenceMagic, sizeof(kSequenceMagic));
    MHeader.version = kSequenceVersion;
    MHeader.width = InDesc.size.width;
    MHeader.height = InDesc.size.height;
    MHeader.fps = InDesc.fps;
    MHeader.frameCount = 0;
    MHeader.reserved = 0;
    std::memcpy(MImage.data(), &MHeader, sizeof(MHeader));

    // A deterministic packet base. The render callback fills the item spans,
    MPacket = Rhi::FramePacket{};
    MPacket.camera.viewport = {static_cast<float>(InDesc.size.width), static_cast<float>(InDesc.size.height)};
    MPacket.textureArray = 0xFFFFFFFFu;

    MActive = true;
    LogInfo("export", "frame export '{}' - {}x{} @ {} fps, frames {}..{} step {} ({} frames, {} bytes)", MPath,
            InDesc.size.width, InDesc.size.height, InDesc.fps, InDesc.startFrame, InDesc.endFrame, InDesc.step,
            MPlanned, MImage.size());
    return true;
}

ExportStatus FrameExporter::CaptureNextFrame(std::string* InError)
{
    if (!MActive || MGpu == nullptr)
    {
        SetError(InError, "FrameExporter is not active; call Begin() first");
        return ExportStatus::Error;
    }
    if (MCaptured >= MPlanned)
    {
        return ExportStatus::Complete;
    }

    const std::uint32_t ScopedFrameIndex = MDesc.startFrame + MCaptured * MDesc.step;
    if (!MGpu->BeginFrame())
    {
        // Minimised window / out-of-date swapchain. The deterministic counter is
        return ExportStatus::Retry;
    }

    bool ScopedOk = true;
    // Re-seed only the fields the exporter owns; the callback owns the rest.
    MPacket.clear = Color32::Black();
    MPacket.textureArray = 0xFFFFFFFFu;
    if (!MRender(MRenderUser, ScopedFrameIndex, MPacket))
    {
        SetError(InError, "frame render callback failed");
        ScopedOk = false;
    }

    if (ScopedOk && !MGpu->RenderToTarget(MTarget, MPacket))
    {
        SetError(InError, "RHI::RenderToTarget failed for the export target");
        ScopedOk = false;
    }

    // Submit the recorded frame before reading the target: the readback runs on
    // its own command buffer, so an unsubmitted frame would return the previous
    // image (or undefined memory on the first capture).
    MGpu->EndFrame();

    if (ScopedOk)
    {
        const std::uint64_t ScopedFrameBytes = SequenceFrameBytes(MDesc);
        const std::uint64_t ScopedOffset =
            kSequenceHeaderBytes + static_cast<std::uint64_t>(MCaptured) * ScopedFrameBytes;
        std::span<std::byte> ScopedDestination(MImage.data() + static_cast<std::size_t>(ScopedOffset),
                                               static_cast<std::size_t>(ScopedFrameBytes));
        Rhi::Extent2D ScopedCapturedSize{};
        if (!MGpu->CaptureRenderTarget(MTarget, ScopedDestination, &ScopedCapturedSize))
        {
            SetError(InError, "RHI::CaptureRenderTarget failed (backend has no readback support)");
            ScopedOk = false;
        }
        else if (ScopedCapturedSize != MDesc.size)
        {
            SetError(InError, "RHI readback returned a different size than the export target");
            ScopedOk = false;
        }
    }

    if (!ScopedOk)
    {
        return ExportStatus::Error;
    }

    ++MCaptured;
    MHeader.frameCount = MCaptured;
    std::memcpy(MImage.data(), &MHeader, sizeof(MHeader));
    return MCaptured >= MPlanned ? ExportStatus::Complete : ExportStatus::Captured;
}

bool FrameExporter::End(std::string* InError)
{
    if (!MActive || MGpu == nullptr)
    {
        SetError(InError, "FrameExporter is not active; call Begin() first");
        return false;
    }
    if (MCaptured != MPlanned)
    {
        SetError(InError, "frame export is incomplete: captured " + std::to_string(MCaptured) + " of " +
                              std::to_string(MPlanned) + " frames");
        return false;
    }

    SequenceFooter ScopedFooter{};
    ScopedFooter.marker = kSequenceCompletionMarker;
    ScopedFooter.frameCount = MCaptured;
    ScopedFooter.totalBytes = MImage.size();
    std::memcpy(MImage.data() + MImage.size() - sizeof(ScopedFooter), &ScopedFooter, sizeof(ScopedFooter));

    if (!MSink.write(MSink.user, MPath, MImage, InError))
    {
        if (InError != nullptr && InError->empty())
        {
            *InError = "FrameSink refused the completed sequence";
        }
        return false;
    }

    LogInfo("export", "frame export complete: '{}' - {} frames, {} bytes", MPath, MCaptured, MImage.size());
    MGpu->DestroyRenderTarget(MTarget);
    MTarget = {};
    MActive = false;
    MGpu = nullptr;
    MImage.clear();
    MImage.shrink_to_fit();
    return true;
}

void FrameExporter::Abort() noexcept
{
    if (MGpu != nullptr && MTarget.IsValid())
    {
        MGpu->DestroyRenderTarget(MTarget);
    }
    MTarget = {};
    MGpu = nullptr;
    MActive = false;
    MCaptured = 0;
    MPlanned = 0;
    MSink = {};
    MRender = nullptr;
    MRenderUser = nullptr;
    MPath.clear();
    MImage.clear();
    MImage.shrink_to_fit();
    MPacket = {};
    MHeader = {};
}

std::uint32_t FrameExporter::NextFrameIndex() const noexcept
{
    if (!MActive || MCaptured >= MPlanned)
    {
        return MDesc.endFrame;
    }
    return MDesc.startFrame + MCaptured * MDesc.step;
}

bool ValidateSequence(std::span<const std::byte> InBytes, std::uint32_t* InFrameCount, std::string* InError)
{
    if (InBytes.size() < kSequenceHeaderBytes + kSequenceFooterBytes)
    {
        SetError(InError, "sequence file is smaller than its header and footer");
        return false;
    }

    SequenceHeader ScopedHeader{};
    std::memcpy(&ScopedHeader, InBytes.data(), sizeof(ScopedHeader));
    if (std::memcmp(ScopedHeader.magic, kSequenceMagic, sizeof(kSequenceMagic)) != 0)
    {
        SetError(InError, "sequence file has a bad magic (not a .fbyseq)");
        return false;
    }
    if (ScopedHeader.version != kSequenceVersion)
    {
        SetError(InError, "sequence file uses an unsupported .fbyseq version");
        return false;
    }
    if (ScopedHeader.width == 0 || ScopedHeader.height == 0 || ScopedHeader.width > 16384 ||
        ScopedHeader.height > 16384 || ScopedHeader.fps == 0 || ScopedHeader.fps > 240)
    {
        SetError(InError, "sequence file header is out of range");
        return false;
    }

    SequenceFooter ScopedFooter{};
    std::memcpy(&ScopedFooter, InBytes.data() + InBytes.size() - sizeof(ScopedFooter), sizeof(ScopedFooter));
    if (ScopedFooter.marker != kSequenceCompletionMarker)
    {
        SetError(InError, "sequence file has no completion marker (the export was interrupted)");
        return false;
    }
    if (ScopedFooter.frameCount != ScopedHeader.frameCount)
    {
        SetError(InError, "sequence header and completion marker disagree about the frame count");
        return false;
    }
    if (ScopedFooter.totalBytes != InBytes.size())
    {
        SetError(InError, "sequence completion marker has the wrong byte count");
        return false;
    }

    const std::uint64_t ScopedFrameBytes = static_cast<std::uint64_t>(ScopedHeader.width) * ScopedHeader.height * 4;
    const std::uint64_t ScopedExpected = kSequenceHeaderBytes +
                                         ScopedFrameBytes * static_cast<std::uint64_t>(ScopedHeader.frameCount) +
                                         kSequenceFooterBytes;
    if (ScopedExpected != InBytes.size())
    {
        SetError(InError, "sequence payload size does not match its header");
        return false;
    }
    if (InFrameCount != nullptr)
    {
        *InFrameCount = ScopedHeader.frameCount;
    }
    return true;
}
}
