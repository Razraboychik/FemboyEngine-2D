// FemboyEngine2D - built-in render-graph passes.
#pragma once

#include "Engine/RHI/PostProcess.hpp"
#include "Engine/RHI/RenderGraph.hpp"

#include <cstdint>
#include <string_view>

namespace Fby
{
class CameraStackSystem;
}

namespace Fby::Rhi
{

struct SdfShadowDesc
{
    Vector2D lightDirection{0.55f, -0.83f};
    // 0 = hard shadow, 1 = one cell of penumbra
    float softness = 1.25f;
    std::uint32_t steps = 64;
    float maxDistance = 640.0f;
    Extent2D resolution{960, 540};
    // Opaque RHI texture id for the terrain SDF. The graph never dereferences it.
    std::uint32_t sdfTextureId = 0;
    float lightRadius = 24.0f;
    float ambient = 0.18f;
    // Terrain field transform, mirrored into the compute constants
    Vector2D fieldOrigin{};
    Vector2D fieldCellSize{2.0f, 2.0f};
    std::uint32_t fieldWidth = 0;
    std::uint32_t fieldHeight = 0;
};

struct SdfShadowConstants
{
    // Matches the SdfShadowBlock UBO in sdf_shadow.comp (std430, 608 bytes).
    // centre.xy, zoom, padding
    Vector4D camera{};
    // width, height, 0, 0
    Vector4D viewport{};
    // origin.xy, extent.xy
    Vector4D fieldBounds{};
    // direction towards the light.xy, softness, ambient
    Vector4D sun{};
    // steps, occluderCount, occlusionRadius, occlusionStrength
    Vector4D params{};
    // encodeScale, enabled, 0, 0
    Vector4D encode{};
    // Pairs: shape (x, y, radius, 0) and extents (hx, hy, box, 0).
    Vector4D occluders[32]{};
};

static_assert(sizeof(SdfShadowConstants) == 608);

class SdfShadowPass
{
public:
    SdfShadowPass() = default;

    void Initialize(const SdfShadowDesc& InDesc) noexcept
    {
        MDesc = InDesc;
    }
    [[nodiscard]] bool Build(RenderGraph& InGraph, std::string_view InSdfResource = "sdf.terrain",
                             std::string_view InShadowResource = "sdf.shadow") noexcept;
    [[nodiscard]] const SdfShadowDesc& Desc() const noexcept
    {
        return MDesc;
    }
    [[nodiscard]] const SdfShadowConstants& Constants() const noexcept
    {
        return MConstants;
    }

private:
    static void ExecuteShadow(RenderGraphContext& InContext, void* InUser);
    void RefreshConstants(const RenderGraphContext& InContext) noexcept;

    SdfShadowDesc MDesc{};
    SdfShadowConstants MConstants{};
    std::uint64_t MSdfResource = 0;
    std::uint64_t MShadowResource = 0;
    std::uint64_t MColorResource = 0;
};

struct PostFxConstants
{
    Vector4D flagsAndCrt{};
    Vector4D vignetteAndBloom{};
    Vector4D exposureAndChromatic{};
    Vector4D grainAndFlash{};
    // Colour-vision correction matrix, one row per vector. The composite applies
    // it when PostFxColorVision is set; exposureAndChromatic.w carries strength.
    Vector4D colorRow0{};
    Vector4D colorRow1{};
    Vector4D colorRow2{};
};

static_assert(sizeof(PostFxConstants) == 112, "the composite UBO is layout-checked against the shader");

struct ColorGradingSettings
{
    // Opaque LUT texture id: a 32x1024 strip or a 3D palette.
    std::uint64_t lutTexture = 0;
    std::uint32_t lutSize = 32;
    float intensity = 1.0f;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float brightness = 0.0f;
    std::uint32_t flags = 0;
};

struct ColorGradingConstants
{
    // lut size, intensity, saturation, contrast
    Vector4D params0{};
    // brightness, lut texture id low/high, flags
    Vector4D params1{};
};

class ColorGradingPass
{
public:
    ColorGradingPass() = default;

    [[nodiscard]] bool Build(RenderGraph& InGraph, const ColorGradingSettings& InSettings,
                             std::string_view InColorResource = "scene.color") noexcept;
    [[nodiscard]] const ColorGradingConstants& Constants() const noexcept
    {
        return MConstants;
    }

private:
    static void Execute(RenderGraphContext& InContext, void* InUser);
    void Refresh() noexcept;

    ColorGradingSettings MSettings{};
    ColorGradingConstants MConstants{};
    std::uint64_t MColorResource = 0;
};

class PostFxPass
{
public:
    PostFxPass() = default;

    // `InCameras` must outlive the graph. The composite reads ActivePostFx() at
    [[nodiscard]] bool Build(RenderGraph& InGraph, Fby::CameraStackSystem& InCameras,
                             std::string_view InColorResource = "scene.color") noexcept;
    [[nodiscard]] const PostFxConstants& Constants() const noexcept
    {
        return MConstants;
    }

private:
    static void ExecutePrefilter(RenderGraphContext& InContext, void* InUser);
    static void ExecuteBlur(RenderGraphContext& InContext, void* InUser);
    static void ExecuteComposite(RenderGraphContext& InContext, void* InUser);
    void RefreshConstants(const RenderGraphContext& InContext) noexcept;

    Fby::CameraStackSystem* MCameras = nullptr;
    PostFxConstants MConstants{};
    std::uint64_t MColorResource = 0;
    std::uint64_t MBloomResource = 0;
};
}
