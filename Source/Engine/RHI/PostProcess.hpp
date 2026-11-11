// FemboyEngine2D - post-process policy shared by the camera stack and the RHI.
#pragma once

#include <array>
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
    // Plugin accessibility effect (NVG/thermal/alcohol) is active.
    PostFxAccessibilityEffectEnabled = 1u << 10,
    // Runtime camera distortion is active. The composite pass must run even
    // when no authored CRT/bloom/AA policy is enabled.
    PostFxCameraDistortion = 1u << 9,
};

struct PostFxAccessibilityEffect
{
    // 0 none, 1 NVG, 2 thermal, 3 alcohol
    std::uint32_t kind = 0;
    std::uint32_t tint = 0xFFFFFFFFu;
    std::uint32_t pattern = 0;
    float intensity = 0.0f;
    float patternScale = 1.0f;
    float blur = 0.0f;
    float sharpness = 1.0f;
    float doubleVision = 0.0f;
    float wobble = 0.0f;
    float wobbleFrequency = 0.0f;
    float chromatic = 0.0f;
    float vignette = 0.0f;
    float desaturation = 0.0f;
    float afterimage = 0.0f;
    float pulse = 0.0f;
    float sway = 0.0f;
    float scaleBreathing = 0.0f;
    std::array<std::uint32_t, 5> palette{};
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
    PostFxAccessibilityEffect accessibility{};
};

static_assert(std::is_trivially_copyable_v<PostFxSettings>);
static_assert(std::is_standard_layout_v<PostFxSettings>);
static_assert(sizeof(PostFxSettings) <= 256, "PostFxSettings remains a bounded camera POD");

[[nodiscard]] constexpr PostFxSettings PostFxDisabled() noexcept
{
    PostFxSettings ScopedSettings{};
    ScopedSettings.flags = PostFxNone;
    return ScopedSettings;
}
}
