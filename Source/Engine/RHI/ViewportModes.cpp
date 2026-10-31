// FemboyEngine2D - viewport mode metadata, help text and heat ramp.
#include "Engine/RHI/ViewportModes.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace Fby::Rhi
{
namespace
{
// Legend palettes are static storage: the descriptor points into them, so the
// tooltip can be rebuilt every frame without allocation.
constexpr std::array<ViewportModeLegend, 4> kStandardLegend = {
    ViewportModeLegend{"Authored sprite colour", Color32::FromBytes(180, 190, 210)},
    ViewportModeLegend{"Ambient fill", Color32::FromBytes(40, 44, 60)},
    ViewportModeLegend{"Light contribution", Color32::FromBytes(255, 214, 120)},
    ViewportModeLegend{"SDF shadow", Color32::FromBytes(90, 70, 120)},
};

constexpr std::array<ViewportModeLegend, 3> kUnlitLegend = {
    ViewportModeLegend{"Unlit sprite", Color32::FromBytes(120, 220, 255)},
    ViewportModeLegend{"Sprite bounds", Color32::FromBytes(80, 160, 255)},
    ViewportModeLegend{"Mesh or cloth bounds", Color32::FromBytes(60, 220, 180)},
};

constexpr std::array<ViewportModeLegend, 4> kCollisionLegend = {
    ViewportModeLegend{"Simple collider (Box2D)", Color32::FromBytes(80, 220, 120)},
    ViewportModeLegend{"Complex SDF collider", Color32::FromBytes(250, 210, 70)},
    ViewportModeLegend{"Trigger or query only", Color32::FromBytes(230, 90, 220)},
    ViewportModeLegend{"No collision", Color32::FromBytes(140, 140, 150)},
};

constexpr std::array<ViewportModeLegend, 4> kComplexityLegend = {
    ViewportModeLegend{"Low cost", Color32::FromBytes(80, 220, 120)},
    ViewportModeLegend{"Moderate cost", Color32::FromBytes(230, 230, 90)},
    ViewportModeLegend{"High cost", Color32::FromBytes(250, 150, 60)},
    ViewportModeLegend{"Overdraw hotspot", Color32::FromBytes(230, 60, 60)},
};

constexpr std::array<ViewportModeLegend, 4> kLightLegend = {
    ViewportModeLegend{"Ambient only", Color32::FromBytes(90, 140, 255)},
    ViewportModeLegend{"One light", Color32::FromBytes(80, 220, 120)},
    ViewportModeLegend{"Two or three lights", Color32::FromBytes(230, 230, 90)},
    ViewportModeLegend{"Four or more lights", Color32::FromBytes(230, 60, 60)},
};

constexpr std::array<ViewportModeLegend, 4> kSdfLegend = {
    ViewportModeLegend{"Solid (inside the field)", Color32::FromBytes(230, 60, 60)},
    ViewportModeLegend{"Surface band", Color32::FromBytes(250, 210, 70)},
    ViewportModeLegend{"Open space", Color32::FromBytes(80, 210, 220)},
    ViewportModeLegend{"Baked occluder", Color32::FromBytes(160, 120, 255)},
};

// The descriptor table is the single source of the dropdown, of the hover help
// and of the editor settings parser. Text is deliberately diagnostic: every
// paragraph ends with the action a user can take, not with a description of the
// internals.
constexpr std::array<ViewportModeDescriptor, kViewportModeCount> kViewportModes = {
    ViewportModeDescriptor{
        ViewportMode::Standard,
        "Standard (Lit)",
        "Standard",
        "Gameplay colour: authored sprites resolved through ambient, global, omni, spot and arc lights.",
        "Standard is the reference image: sprites keep their authored colour and every light is applied "
        "exactly as the shipped game applies it, including SDF shadows from the baked field and from "
        "dynamic occluders. If the image looks wrong here, it will look wrong in the build.",
        "A sprite that is unexpectedly dark usually sits behind an occluder, outside every light radius, "
        "or under a low ambient term. Raise the ambient fill, move the lamp, or check the occluder list "
        "before editing the sprite colour.",
        "Use Standard as the baseline. Every other mode is a diagnostic overlay on the same geometry, so "
        "switching back here proves that a fix changed gameplay colour rather than only the overlay.",
        kStandardLegend.data(),
        static_cast<std::uint32_t>(kStandardLegend.size()),
        kViewportModeFlagNeedsLights,
    },
    ViewportModeDescriptor{
        ViewportMode::UnlitWireframe,
        "Unlit / Wireframe",
        "Unlit",
        "Authored albedo without lighting, with sprite and mesh bounds drawn as an outline.",
        "Unlit drops the lighting solve and shows the authored sprite colour, so a texture that is too "
        "dark or too bright is visible without ambient or shadows hiding it. Bounds are then drawn on top: "
        "blue is a sprite quad, teal is a skinned or cloth mesh.",
        "If a sprite is missing here but present in Standard, the RenderMask excludes the sprite pass, "
        "the colour alpha is zero, or the entity has no DrawOrder. If the bounds are offset from the art, "
        "the pivot or the sprite size is wrong.",
        "Use this mode to verify UV rectangles, pivots and tint values before tuning lights. Fixing art "
        "here is cheaper than fighting a shadow that only hides a layout mistake.",
        kUnlitLegend.data(),
        static_cast<std::uint32_t>(kUnlitLegend.size()),
        kViewportModeFlagUnlit | kViewportModeFlagNeedsColliders,
    },
    ViewportModeDescriptor{
        ViewportMode::CollisionOverlap,
        "Collision Overlap",
        "Collision",
        "Colliders drawn by collision mode, over a neutral scene, with the physical response implied by colour.",
        "Collision Overlap tints the scene grey and draws one shape per collider. Green is a simple Box2D "
        "collider: a static wall or a dynamic prop. Yellow is a complex SDF collider that also tests against "
        "the distance field. Magenta is a trigger or query-only shape: it reports overlaps but never pushes. "
        "Grey means NoCollision and must be intentional.",
        "The overlay reads the same CollisionMode that the physics bridge applies, so a mismatch between "
        "drawn colour and behaviour is a real configuration bug, not a visual artefact.",
        "Optimisation: keep level geometry on simple static shapes, reserve complex dynamic shapes for "
        "characters and props that need slope response, and remove NoCollision from sprites that still "
        "receive overlap queries.",
        kCollisionLegend.data(),
        static_cast<std::uint32_t>(kCollisionLegend.size()),
        kViewportModeFlagOverlay | kViewportModeFlagNeedsColliders,
    },
    ViewportModeDescriptor{
        ViewportMode::ShaderComplexity,
        "Shader Complexity / Overdraw",
        "Overdraw",
        "Estimated per-pixel cost from sprite overlap and lighting, shown as a green to red heat ramp.",
        "Shader Complexity estimates the work one pixel receives: the number of sprite quads covering it "
        "multiplied by the lights that pass their cone and radius tests. Green is a single fill, yellow is "
        "two to three overlapping fills, orange and red are four or more.",
        "A red full-screen band usually means a background quad under every subsequent layer; red islands "
        "under a cluster of lamps mean lighting saturation rather than overdraw. Both are additive, so the "
        "overlay colours combine the two causes.",
        "Optimisation: cut overlapping transparent quads, merge small sprites into an atlas, cull off-screen "
        "items through RenderMask, and replace several decorative glows with one global light.",
        kComplexityLegend.data(),
        static_cast<std::uint32_t>(kComplexityLegend.size()),
        kViewportModeFlagOverlay | kViewportModeFlagNeedsLights | kViewportModeFlagNeedsColliders,
    },
    ViewportModeDescriptor{
        ViewportMode::LightComplexity,
        "Light Complexity",
        "Lights",
        "Enabled lights reaching each sprite after cone and radius tests, static or dynamic.",
        "Light Complexity counts every enabled light that reaches each sprite after radius and cone tests, "
        "including static lamps, because the overlay reports authoring load rather than the per-frame solve. "
        "Blue is ambient only, green is one light, yellow is two or three, red is four or more where "
        "tonemapping starts to compress highlights.",
        "A lamp that does not appear is outside its Radius, outside its Cone Angle, disabled, or excluded by "
        "the light source RenderMask. The static contribution cache never removes a light from this count.",
        "Optimisation: mark lamps Static, reduce Radius, narrow the Cone Angle of spot and arc lights, and "
        "prefer Sleeping Static props and lights that wake only on contact or on a program trigger.",
        kLightLegend.data(),
        static_cast<std::uint32_t>(kLightLegend.size()),
        kViewportModeFlagOverlay | kViewportModeFlagNeedsLights,
    },
    ViewportModeDescriptor{
        ViewportMode::SdfVisualizer,
        "SDF Visualizer",
        "SDF",
        "Signed distance to the nearest solid surface: red inside, yellow on the surface, cyan in open space.",
        "SDF Visualizer samples the distance field at every sprite and draws the baked occluders on top. "
        "Red is solid interior, yellow is the zero band, cyan is open space, violet is a baked occluder "
        "outline that drives shadows and SDF collision.",
        "A sprite that is red while standing in a corridor means the field is shifted or too coarse; a "
        "corridor that is cyan where the art shows a wall means the field was not rebuilt after the edit.",
        "Optimisation: increase the cell size on large open maps, rebuild the field only after edits, and "
        "keep static terrain in the field while moving props use dynamic occluder components.",
        kSdfLegend.data(),
        static_cast<std::uint32_t>(kSdfLegend.size()),
        kViewportModeFlagOverlay | kViewportModeFlagNeedsSdf | kViewportModeFlagNeedsColliders,
    },
};

[[nodiscard]] float Clamp01(float InValue) noexcept
{
    return std::clamp(InValue, 0.0f, 1.0f);
}
}

std::span<const ViewportModeDescriptor> AllViewportModes() noexcept
{
    return {kViewportModes.data(), kViewportModes.size()};
}

const ViewportModeDescriptor& DescribeViewportMode(ViewportMode InMode) noexcept
{
    const std::uint32_t ScopedIndex = static_cast<std::uint32_t>(InMode);
    if (ScopedIndex >= kViewportModeCount)
    {
        return kViewportModes[0];
    }
    return kViewportModes[ScopedIndex];
}

const char* ViewportModeName(ViewportMode InMode) noexcept
{
    return DescribeViewportMode(InMode).ShortLabel;
}

bool ParseViewportMode(std::string_view InName, ViewportMode* OutMode) noexcept
{
    if (OutMode == nullptr)
    {
        return false;
    }
    for (const ViewportModeDescriptor& ScopedDescriptor : kViewportModes)
    {
        if (InName == ScopedDescriptor.ShortLabel || InName == ScopedDescriptor.Label)
        {
            *OutMode = ScopedDescriptor.Mode;
            return true;
        }
    }
    return false;
}

ViewportMode CycleViewportMode(ViewportMode InMode, std::int32_t InDelta) noexcept
{
    const std::int32_t ScopedCount = static_cast<std::int32_t>(kViewportModeCount);
    std::int32_t ScopedIndex = static_cast<std::int32_t>(InMode) + InDelta;
    ScopedIndex %= ScopedCount;
    if (ScopedIndex < 0)
    {
        ScopedIndex += ScopedCount;
    }
    return static_cast<ViewportMode>(ScopedIndex);
}

Color32 ComplexityHeatColor(float InNormalized) noexcept
{
    const float ScopedValue = Clamp01(InNormalized);
    // Two linear legs: green to yellow, then yellow to red. The result is
    // opaque so the heat overlay replaces the sprite colour instead of tinting
    // through it.
    float ScopedRed = 0.0f;
    float ScopedGreen = 0.0f;
    if (ScopedValue < 0.5f)
    {
        ScopedRed = ScopedValue * 2.0f;
        ScopedGreen = 1.0f;
    }
    else
    {
        ScopedRed = 1.0f;
        ScopedGreen = 1.0f - (ScopedValue - 0.5f) * 2.0f;
    }
    const auto ScopedByte = [](float InValue)
    {
        return static_cast<std::uint8_t>(std::lround(Clamp01(InValue) * 255.0f));
    };
    return Color32::FromBytes(ScopedByte(ScopedRed), ScopedByte(ScopedGreen), ScopedByte(0.15f), 255);
}

bool ViewportModeIsOverlay(ViewportMode InMode) noexcept
{
    return (DescribeViewportMode(InMode).Flags & kViewportModeFlagOverlay) != 0;
}

bool ViewportModeNeedsUnlit(ViewportMode InMode) noexcept
{
    return (DescribeViewportMode(InMode).Flags & kViewportModeFlagUnlit) != 0;
}

bool ViewportModeNeedsSdf(ViewportMode InMode) noexcept
{
    return (DescribeViewportMode(InMode).Flags & kViewportModeFlagNeedsSdf) != 0;
}
}
