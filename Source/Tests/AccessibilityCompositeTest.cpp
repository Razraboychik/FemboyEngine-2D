// FemboyEngine2D - GPU accessibility composite test (offscreen render target).
#include "Engine/Core/Log.hpp"
#include "Engine/Core/WindowManager.hpp"
#include "Engine/RHI/RHI.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{

int GChecks = 0;
int GFailures = 0;

void Check(bool InCondition, const char* InExpression, int InLine)
{
    ++GChecks;
    if (!InCondition)
    {
        ++GFailures;
        std::fprintf(stderr, "FAIL line %d: %s\n", InLine, InExpression);
    }
}

#define FBY_CHECK(expression) Check((expression), #expression, __LINE__)

int ChannelAt(const std::vector<std::byte>& InPixels, std::uint32_t InIndex)
{
    return static_cast<int>(static_cast<unsigned char>(InPixels[static_cast<std::size_t>(InIndex) * 4u]));
}

// One frame: clear-only packet with the given accessibility policy, captured back.
bool RenderAndCapture(Fby::Rhi::RHI& InGpu, Fby::Rhi::RenderTargetHandle InTarget, const Fby::Rhi::FramePacket& InPacket,
                      Fby::Rhi::Extent2D InExtent, std::vector<std::byte>& OutPixels)
{
    if (!InGpu.BeginFrame())
    {
        return false;
    }
    if (!InGpu.RenderToTarget(InTarget, InPacket))
    {
        (void)InGpu.EndFrame();
        return false;
    }
    InGpu.EndFrame();
    InGpu.WaitIdle();
    OutPixels.assign(static_cast<std::size_t>(InExtent.width) * InExtent.height * 4u, std::byte{0});
    Fby::Rhi::Extent2D ScopedSize{};
    return InGpu.CaptureRenderTarget(InTarget, OutPixels, &ScopedSize);
}
}

int main()
{
    Fby::SetLogLevel(Fby::LogLevel::Warn);

    Fby::WindowManager ScopedWindow;
    if (!ScopedWindow.Initialize())
    {
        std::fprintf(stderr, "accessibility composite: window init failed\n");
        return 1;
    }
    Fby::WindowDesc ScopedWindowDesc;
    ScopedWindowDesc.title = "FemboyEngine - accessibility composite";
    ScopedWindowDesc.width = 320;
    ScopedWindowDesc.height = 200;
    if (!ScopedWindow.Create(ScopedWindowDesc))
    {
        std::fprintf(stderr, "accessibility composite: window create failed\n");
        return 1;
    }

    Fby::Rhi::DeviceDesc ScopedDeviceDesc;
    ScopedDeviceDesc.backend = Fby::Rhi::Backend::Vulkan;
    ScopedDeviceDesc.nativeWindow = ScopedWindow.NativeWindow();
    ScopedDeviceDesc.size = {320, 200};
    ScopedDeviceDesc.vsync = false;
    std::unique_ptr<Fby::Rhi::RHI> ScopedGpu = Fby::Rhi::RHI::Create(ScopedDeviceDesc);
    if (!ScopedGpu)
    {
        // A machine without a Vulkan device is not a composite failure.
        std::fprintf(stderr, "no Vulkan device available; skipping\n");
        return 0;
    }

    constexpr std::uint32_t kSize = 8;
    // The layer policy must be clamped to the device's maxImageArrayLayers.
    FBY_CHECK(ScopedGpu->Info().maxTextureLayers > 0);
    FBY_CHECK(ScopedGpu->Info().maxTextureLayers <= ScopedDeviceDesc.maxTextureLayers);
    Fby::Rhi::RenderTargetDesc ScopedTargetDesc;
    ScopedTargetDesc.size = {kSize, kSize};
    ScopedTargetDesc.srgb = false;
    const Fby::Rhi::RenderTargetHandle ScopedTarget = ScopedGpu->CreateRenderTarget(ScopedTargetDesc);
    if (!ScopedTarget.IsValid())
    {
        std::fprintf(stderr, "accessibility composite: no render target\n");
        return 1;
    }

    Fby::Rhi::FramePacket ScopedPacket;
    ScopedPacket.clear = Fby::Color32::FromBytes(200, 60, 60);
    const Fby::Rhi::Extent2D ScopedExtent{kSize, kSize};

    std::vector<std::byte> ScopedBaseline;
    FBY_CHECK(RenderAndCapture(*ScopedGpu, ScopedTarget, ScopedPacket, ScopedExtent, ScopedBaseline));
    FBY_CHECK(ScopedBaseline.size() == static_cast<std::size_t>(kSize) * kSize * 4u);
    const int ScopedBaseRed = ChannelAt(ScopedBaseline, 0);
    FBY_CHECK(ScopedBaseRed >= 190 && ScopedBaseRed <= 210);

    // Post-process composite: the swapchain path renders the scene offscreen and
    // runs the postfx pipeline, exercising pipeline creation and the draw.
    {
        Fby::Rhi::FramePacket ScopedPostFxPacket;
        ScopedPostFxPacket.clear = ScopedPacket.clear;
        ScopedPostFxPacket.postFx = Fby::Rhi::PostFxSettings{};
        ScopedPostFxPacket.postFxEnabled = true;
        FBY_CHECK(ScopedGpu->BeginFrame());
        ScopedGpu->Submit(ScopedPostFxPacket);
        ScopedGpu->EndFrame();
    }

    // The render-graph path records the same composite through the generic
    // graphics-pass API; an unknown pipeline must be refused.
    {
        std::array<std::byte, 112> ScopedFxBlock{};
        FBY_CHECK(ScopedGpu->BeginFrame());
        FBY_CHECK(ScopedGpu->RecordGraphicsPass("postfx_composite.frag", ScopedFxBlock.data(),
                                                static_cast<std::uint32_t>(ScopedFxBlock.size()), {320, 200}, 1));
        FBY_CHECK(!ScopedGpu->RecordGraphicsPass("not_a_graphics_pass", ScopedFxBlock.data(),
                                                 static_cast<std::uint32_t>(ScopedFxBlock.size()), {320, 200}, 1));
        Fby::Rhi::FramePacket ScopedGraphPacket;
        ScopedGraphPacket.clear = ScopedPacket.clear;
        ScopedGpu->Submit(ScopedGraphPacket);
        ScopedGpu->EndFrame();
    }

    // Colour-vision correction: deuteranopia at full strength changes the frame.
    Fby::Rhi::FramePacket ScopedCvd = ScopedPacket;
    ScopedCvd.accessibility.flags = Fby::Rhi::AccessibilityColorVision;
    ScopedCvd.accessibility.colorVision = 2;
    ScopedCvd.accessibility.colorVisionStrength = 1.0f;
    std::vector<std::byte> ScopedCorrected;
    FBY_CHECK(RenderAndCapture(*ScopedGpu, ScopedTarget, ScopedCvd, ScopedExtent, ScopedCorrected));
    int ScopedDelta = 0;
    for (std::uint32_t ScopedIndex = 0; ScopedIndex < kSize * kSize; ++ScopedIndex)
    {
        for (std::uint32_t ScopedChannel = 0; ScopedChannel < 3; ++ScopedChannel)
        {
            const int ScopedValue = static_cast<int>(
                static_cast<unsigned char>(ScopedCorrected[(ScopedIndex * 4u) + ScopedChannel]));
            const int ScopedBase =
                static_cast<int>(static_cast<unsigned char>(ScopedBaseline[(ScopedIndex * 4u) + ScopedChannel]));
            ScopedDelta += ScopedValue > ScopedBase ? ScopedValue - ScopedBase : ScopedBase - ScopedValue;
        }
    }
    FBY_CHECK(ScopedDelta > 0);

    // The flag alone does nothing: correction 0 and gain 1 must reproduce the frame.
    Fby::Rhi::FramePacket ScopedNoop = ScopedPacket;
    ScopedNoop.accessibility.flags = Fby::Rhi::AccessibilityColorVision | Fby::Rhi::AccessibilityFlashGuard;
    ScopedNoop.accessibility.colorVision = 0;
    ScopedNoop.accessibility.colorVisionStrength = 0.0f;
    ScopedNoop.accessibility.flashGain = 1.0f;
    std::vector<std::byte> ScopedNoopPixels;
    FBY_CHECK(RenderAndCapture(*ScopedGpu, ScopedTarget, ScopedNoop, ScopedExtent, ScopedNoopPixels));
    FBY_CHECK(std::memcmp(ScopedNoopPixels.data(), ScopedBaseline.data(), ScopedBaseline.size()) == 0);

    // The composite owns colour vision only; a lone flash-guard flag must not
    // dim a frame, because the per-pixel pass applies the residual instead.
    Fby::Rhi::FramePacket ScopedGuardOnly = ScopedPacket;
    ScopedGuardOnly.accessibility.flags = Fby::Rhi::AccessibilityFlashGuard;
    ScopedGuardOnly.accessibility.flashGain = 0.5f;
    std::vector<std::byte> ScopedGuardPixels;
    FBY_CHECK(RenderAndCapture(*ScopedGpu, ScopedTarget, ScopedGuardOnly, ScopedExtent, ScopedGuardPixels));
    FBY_CHECK(std::memcmp(ScopedGuardPixels.data(), ScopedBaseline.data(), ScopedBaseline.size()) == 0);

    // Per-pixel flash guard: a dark frame stores its luminance, a sudden bright
    // frame is clamped to the residual gain.
    Fby::Rhi::RenderTargetDesc ScopedFlashDesc;
    ScopedFlashDesc.size = {kSize, kSize};
    ScopedFlashDesc.srgb = false;
    const Fby::Rhi::RenderTargetHandle ScopedFlashTarget = ScopedGpu->CreateRenderTarget(ScopedFlashDesc);
    FBY_CHECK(ScopedFlashTarget.IsValid());
    Fby::Rhi::FramePacket ScopedDark = ScopedPacket;
    ScopedDark.clear = Fby::Color32::FromBytes(20, 20, 20);
    ScopedDark.accessibility.flags = Fby::Rhi::AccessibilityFlashGuard;
    ScopedDark.accessibility.flashThreshold = 0.2f;
    ScopedDark.accessibility.flashGain = 0.35f;
    std::vector<std::byte> ScopedDarkPixels;
    FBY_CHECK(RenderAndCapture(*ScopedGpu, ScopedFlashTarget, ScopedDark, ScopedExtent, ScopedDarkPixels));
    Fby::Rhi::FramePacket ScopedBright = ScopedPacket;
    ScopedBright.clear = Fby::Color32::FromBytes(220, 220, 220);
    ScopedBright.accessibility = ScopedDark.accessibility;
    std::vector<std::byte> ScopedFlashPixels;
    FBY_CHECK(RenderAndCapture(*ScopedGpu, ScopedFlashTarget, ScopedBright, ScopedExtent, ScopedFlashPixels));
    FBY_CHECK(ChannelAt(ScopedFlashPixels, 0) < 150);

    // Without the guard the same two-frame sequence keeps the bright frame.
    Fby::Rhi::RenderTargetDesc ScopedControlDesc;
    ScopedControlDesc.size = {kSize, kSize};
    ScopedControlDesc.srgb = false;
    const Fby::Rhi::RenderTargetHandle ScopedControlTarget = ScopedGpu->CreateRenderTarget(ScopedControlDesc);
    FBY_CHECK(ScopedControlTarget.IsValid());
    Fby::Rhi::FramePacket ScopedControlDark = ScopedPacket;
    ScopedControlDark.clear = Fby::Color32::FromBytes(20, 20, 20);
    std::vector<std::byte> ScopedControlDarkPixels;
    FBY_CHECK(RenderAndCapture(*ScopedGpu, ScopedControlTarget, ScopedControlDark, ScopedExtent,
                               ScopedControlDarkPixels));
    Fby::Rhi::FramePacket ScopedControlBright = ScopedBright;
    ScopedControlBright.accessibility = {};
    std::vector<std::byte> ScopedControlBrightPixels;
    FBY_CHECK(RenderAndCapture(*ScopedGpu, ScopedControlTarget, ScopedControlBright, ScopedExtent,
                               ScopedControlBrightPixels));
    FBY_CHECK(ChannelAt(ScopedControlBrightPixels, 0) > 200);
    ScopedGpu->DestroyRenderTarget(ScopedFlashTarget);
    ScopedGpu->DestroyRenderTarget(ScopedControlTarget);

    // The swapchain path runs the same pass against MRenderPass. It cannot be
    // captured, so this only proves a flagged frame still submits end to end.
    Fby::Rhi::FramePacket ScopedSwap = ScopedCvd;
    ScopedSwap.items = {};
    ScopedSwap.keys = {};
    ScopedSwap.batches = {};
    for (int ScopedFrame = 0; ScopedFrame < 3; ++ScopedFrame)
    {
        if (ScopedGpu->BeginFrame())
        {
            Fby::Rhi::FramePacket ScopedSwapFrame = ScopedSwap;
            if (ScopedFrame >= 1)
            {
                // Guard frames exercise the swapchain history image + compute pass.
                ScopedSwapFrame.accessibility.flags = Fby::Rhi::AccessibilityFlashGuard;
                ScopedSwapFrame.accessibility.flashThreshold = 0.2f;
                ScopedSwapFrame.accessibility.flashGain = 0.35f;
            }
            ScopedGpu->Submit(ScopedSwapFrame);
            ScopedGpu->EndFrame();
        }
    }
    ScopedGpu->WaitIdle();
    FBY_CHECK(ScopedGpu->Stats().items == 0);

    ScopedGpu->DestroyRenderTarget(ScopedTarget);
    ScopedGpu->Shutdown();

    if (GFailures != 0)
    {
        std::fprintf(stderr, "accessibility_composite: %d of %d checks failed\n", GFailures, GChecks);
        return 1;
    }
    std::printf("accessibility_composite: %d checks passed\n", GChecks);
    return 0;
}
