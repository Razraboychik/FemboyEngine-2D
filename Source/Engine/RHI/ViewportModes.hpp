// FemboyEngine2D - editor viewport display modes and their diagnostics contract.
#pragma once

#include "Engine/Core/Types.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace Fby::Rhi
{

// Display modes a 2D viewport can present. The enum order is the order of the
// dropdown and of the keyboard cycle, so append new modes at the end.
enum class ViewportMode : std::uint8_t
{
    Standard = 0,
    UnlitWireframe = 1,
    CollisionOverlap = 2,
    ShaderComplexity = 3,
    LightComplexity = 4,
    SdfVisualizer = 5,
};

inline constexpr std::uint32_t kViewportModeCount = 6;

// Mode flags consumed by the extract and by the editor viewport.
inline constexpr std::uint32_t kViewportModeFlagOverlay = 1u << 0;
inline constexpr std::uint32_t kViewportModeFlagUnlit = 1u << 1;
inline constexpr std::uint32_t kViewportModeFlagNeedsSdf = 1u << 2;
inline constexpr std::uint32_t kViewportModeFlagNeedsColliders = 1u << 3;
inline constexpr std::uint32_t kViewportModeFlagNeedsLights = 1u << 4;

// One colour-coded row of the legend shown under the mode help. The colour is
// the exact tint the presenter uses, so the tooltip cannot drift from the
// rendered overlay.
struct ViewportModeLegend
{
    const char* Label = "";
    Color32 Color = Color32::White();
};

// Everything the viewport needs to explain a mode: the label, a two to three
// paragraph help block, the legend and the feature flags for the extract.
// Paragraph fields are never null; an empty paragraph ends the block early.
struct ViewportModeDescriptor
{
    ViewportMode Mode = ViewportMode::Standard;
    const char* Label = "";
    const char* ShortLabel = "";
    const char* Summary = "";
    const char* Paragraph1 = "";
    const char* Paragraph2 = "";
    const char* Paragraph3 = "";
    const ViewportModeLegend* Legend = nullptr;
    std::uint32_t LegendCount = 0;
    std::uint32_t Flags = 0;
};

[[nodiscard]] std::span<const ViewportModeDescriptor> AllViewportModes() noexcept;
[[nodiscard]] const ViewportModeDescriptor& DescribeViewportMode(ViewportMode InMode) noexcept;

// Stable identifier used by the editor settings file and by scene overrides.
[[nodiscard]] const char* ViewportModeName(ViewportMode InMode) noexcept;
[[nodiscard]] bool ParseViewportMode(std::string_view InName, ViewportMode* OutMode) noexcept;

// Wraps around kViewportModeCount. A negative delta walks the enum backwards.
[[nodiscard]] ViewportMode CycleViewportMode(ViewportMode InMode, std::int32_t InDelta) noexcept;

// Green to yellow to red ramp shared by the complexity modes. Values outside
// zero and one clamp, so a caller can pass raw counts.
[[nodiscard]] Color32 ComplexityHeatColor(float InNormalized) noexcept;

// Diagnostic pass masks used to decide what an overlay pass may read.
[[nodiscard]] bool ViewportModeIsOverlay(ViewportMode InMode) noexcept;
[[nodiscard]] bool ViewportModeNeedsUnlit(ViewportMode InMode) noexcept;
[[nodiscard]] bool ViewportModeNeedsSdf(ViewportMode InMode) noexcept;
}
