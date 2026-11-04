// FemboyEngine2D - shared settings metadata for editor and game UI.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace Fby::Editor
{

enum class SettingScope : std::uint8_t
{
    Project = 0,
    User = 1,
    Scene = 2,
    Effect = 3,
    Hud = 4
};

enum class SettingKind : std::uint8_t
{
    Boolean = 0,
    Float = 1,
    Integer = 2,
    Enum = 3
};

struct SettingDesc
{
    std::string_view key;
    std::string_view label;
    std::string_view tooltip;
    SettingScope scope = SettingScope::Project;
    SettingKind kind = SettingKind::Boolean;
    float minimum = 0.0f;
    float maximum = 1.0f;
    bool requiresRestart = false;
};

inline constexpr std::array<SettingDesc, 25> kRenderSettingsSchema{{
    {"upscale_policy", "Upscaler policy",
     "Auto picks the best available backend, Universal always uses the FSR chain, Off disables upscaling.",
     SettingScope::User, SettingKind::Enum, 0.0f, 2.0f, false},
    {"upscaler_backend", "Upscaler backend",
     "Auto, FSR1, FSR2, DLSS or MetalFX. MetalFX is filtered by platform/backend; unsupported backends fall back automatically.",
     SettingScope::User, SettingKind::Enum, 0.0f, 5.0f, false},
    {"dlss_quality", "DLSS quality",
     "DLSS render scale: Ultra Performance, Performance, Balanced or Quality.",
     SettingScope::User, SettingKind::Enum, 0.0f, 3.0f, false},
    {"render_scale", "Render scale",
     "Internal scene resolution relative to the viewport. UI stays native.", SettingScope::User,
     SettingKind::Float, 0.25f, 1.0f, false},
    {"upscale_mode", "Upscale",
     "Nearest for pixel art, Bilinear/Bicubic for painted 2D, FSR1 for edge-aware spatial upscale.",
     SettingScope::User, SettingKind::Enum, 0.0f, 4.0f, false},
    {"fsr1_sharpness", "FSR1 sharpness",
     "RCAS sharpening strength for the FSR1 spatial path. 0 disables sharpening.",
     SettingScope::User, SettingKind::Float, 0.0f, 1.0f, false},
    {"oit_enabled", "Order-independent transparency",
     "Enable the project's OIT policy for transparent items. Requires a project mode other than Off.",
     SettingScope::User, SettingKind::Boolean, 0.0f, 1.0f, false},
    {"oit_mode", "OIT mode",
     "Off = back-to-front blend; Weighted = alpha-weighted approximation; Accurate = exact per-pixel list.",
     SettingScope::Project, SettingKind::Enum, 0.0f, 2.0f, false},
    {"opaque_mode", "Front-to-back opaque",
     "Off, On, or Auto. Auto requires cooked cutout alpha metadata for every opaque layer.",
     SettingScope::Project, SettingKind::Enum, 0.0f, 2.0f, false},
    {"opaque_art_cutout", "Opaque art is cutout",
     "Manual override for projects without cooked alpha metadata. Hard alpha cutoff at 0.5.",
     SettingScope::Project, SettingKind::Boolean, 0.0f, 1.0f, false},
    {"opaque_alpha_cutoff", "Opaque alpha cutoff",
     "Alpha-test threshold for the blendless opaque pipeline.", SettingScope::Project,
     SettingKind::Float, 0.0f, 1.0f, false},
    {"static_bake_enabled", "Static layer bake",
     "Reuse the editor/PIE viewport while the scene, camera and lights are unchanged.",
     SettingScope::Project, SettingKind::Boolean, 0.0f, 1.0f, false},
    {"static_bake_tile_size", "Bake tile size",
     "Tile size reserved for the tiled bake cache.", SettingScope::Project, SettingKind::Integer,
     64.0f, 1024.0f, false},
    {"static_bake_budget_mb", "Bake budget MB",
     "Maximum memory reserved by the bake cache.", SettingScope::Project, SettingKind::Integer,
     8.0f, 1024.0f, false},
    {"quality_preset", "Quality preset",
     "Player-facing low/medium/high selection. Presets map to pipeline and effect defaults.",
     SettingScope::User, SettingKind::Enum, 0.0f, 2.0f, false},
    {"window_mode", "Window mode",
     "Windowed, borderless fullscreen, or exclusive fullscreen.", SettingScope::User,
     SettingKind::Enum, 0.0f, 2.0f, true},
    {"allow_temporal_upscale", "Allow temporal upscaling",
     "Project policy. Off hides FSR2/DLSS entirely; no temporal backend is built in yet.",
     SettingScope::Project, SettingKind::Boolean, 0.0f, 1.0f, false},
    {"temporal_upscale", "Temporal upscaler",
     "Off, FSR2, DLSS, or Auto. Only selectable when the project allows it and the backend is compiled in.",
     SettingScope::Project, SettingKind::Enum, 0.0f, 3.0f, false},
    {"hud_render_scale", "HUD render scale",
     "Independent from the game render scale. 1.0 keeps text and UI native.", SettingScope::Hud,
     SettingKind::Float, 0.25f, 1.0f, false},
    {"hud_upscale", "HUD upscale",
     "Nearest preserves pixel edges; bilinear softens small HUD scales.", SettingScope::Hud,
     SettingKind::Enum, 0.0f, 1.0f, false},
    {"hud_visible", "Show HUD",
     "Hidden is intended for screenshots, benchmarks and clean frame-generation captures.",
     SettingScope::Hud, SettingKind::Boolean, 0.0f, 1.0f, false},
    {"allow_frame_generation", "Allow frame generation",
     "Project policy. Off hides every frame-generation backend.", SettingScope::Project,
     SettingKind::Boolean, 0.0f, 1.0f, false},
    {"frame_generation_policy", "Frame generation policy",
     "Auto prefers DLSS FG on NVIDIA then FSR3/custom; Universal uses FSR3/custom; Off disables.",
     SettingScope::User, SettingKind::Enum, 0.0f, 2.0f, false},
    {"frame_generation", "Frame generation",
     "Off, custom, FSR3 or DLSS. Only selectable when the backend is available.",
     SettingScope::Project, SettingKind::Enum, 0.0f, 4.0f, false},
    {"hud_target", "HUD target",
     "Auto uses the swapchain overlay without frame generation and an offscreen HUD target with it.",
     SettingScope::Hud, SettingKind::Enum, 0.0f, 2.0f, false},
}};

[[nodiscard]] constexpr const SettingDesc* FindSettingDesc(std::string_view InKey) noexcept
{
    for (const SettingDesc& ScopedSetting : kRenderSettingsSchema)
    {
        if (ScopedSetting.key == InKey)
        {
            return &ScopedSetting;
        }
    }
    return nullptr;
}

}
