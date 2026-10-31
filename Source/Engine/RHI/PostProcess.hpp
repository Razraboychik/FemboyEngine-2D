// FemboyEngine2D - post-process policy shared by the camera stack and the RHI.
#pragma once

#include <cstdint>
#include <type_traits>

namespace Fby::Rhi
{

enum PostFxFlags : std::uint32_t
{
    PostFxNone = 0,
    PostFxCrt = 1u << 0,
    PostFxVignette = 1u << 1,
    PostFxBloom = 1u << 2,
    PostFxChromaticAberration = 1u << 3,
    PostFxFilmGrain = 1u << 4,
    PostFxFlashGuard = 1u << 5,
    // Screen-space AA. Mutually exclusive in practice; SMAA wins when both are set.
    PostFxFxaa = 1u << 6,
    PostFxSmaa = 1u << 7,
    // Accessibility colour-vision correction. The matrix travels in the postfx
    // constants, so the kernel and the shader share one source of truth.
    PostFxColorVision = 1u << 8,
};

struct PostFxSettings
{
    std::uint32_t flags = PostFxCrt | PostFxVignette | PostFxBloom | PostFxFxaa;
    // 0 = flat, 0.12 = a subtle tube
    float crtCurvature = 0.06f;
    float scanlineStrength = 0.12f;
    float vignetteStrength = 0.28f;
    float bloomThreshold = 0.75f;
    float bloomIntensity = 0.55f;
    float exposure = 1.0f;
    float chromaticAberration = 0.35f;
    float filmGrain = 0.035f;
    float flashGuardGain = 0.85f;
    // 0 none, 1 protanopia, 2 deuteranopia, 3 tritanopia
    std::uint32_t colorVision = 0;
    float colorVisionStrength = 1.0f;
};

static_assert(std::is_trivially_copyable_v<PostFxSettings>);
static_assert(std::is_standard_layout_v<PostFxSettings>);
static_assert(sizeof(PostFxSettings) == 48, "PostFxSettings is a network/camera POD");

[[nodiscard]] constexpr PostFxSettings PostFxDisabled() noexcept
{
    PostFxSettings ScopedSettings{};
    ScopedSettings.flags = PostFxNone;
    return ScopedSettings;
}
}
