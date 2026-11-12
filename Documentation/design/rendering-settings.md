# Rendering project settings

All values live in `ProjectSettings` and are serialized under `[render]`.

| Key | Default | Meaning |
|---|---|---|
| `render_scale` | `1.0` | Internal scene resolution relative to the viewport. UI stays native. |
| `upscale_mode` | `1` | `0 Nearest`, `1 Bilinear`, `2 Bicubic`, `3 FSR1`, `4 FSR2`. Prefer `upscale_policy` for player-facing control. |
| `upscale_policy` | `auto` | `auto`, `universal`, `off`. Auto prefers DLSS on NVIDIA, then FSR2/FSR1. |
| `frame_generation_policy` | `auto` | `auto`, `universal`, `off`. Auto prefers DLSS FG, then FSR3 FG, then custom. |
| `oit_mode` | `0` | `0 Off`, `1 Weighted`, `2 Accurate`. Travels in `FramePacket`; `OitPass` renders the weighted pass. |
| `opaque_mode` | `2` | `0 Off`, `1 On`, `2 Auto`. Auto requires atlas cutout metadata. |
| `opaque_art_cutout` | `false` | Manual cutout override for projects without cooked alpha metadata. |
| `opaque_alpha_cutoff` | `0.5` | Alpha-test threshold for the opaque blendless pipeline. |
| `static_bake_enabled` | `false` | Reuse the editor/PIE viewport target while the scene is static. |
| `static_bake_tile_size` | `256` | Tile size reserved for the tiled bake cache. |
| `static_bake_budget_mb` | `64` | Bake cache budget. |
| `vsync` | editor setting | Presentation mode. |
| `texture_format` | `bc7-srgb` | Cooked texture format. |
| `max_texture_size` | `2048` | Import size cap. |

## Alpha metadata

The cooker scans source alpha and writes:

- `kTextureFlagAlphaCutout` — only 0/255;
- `kTextureFlagAlphaBlend` — intermediate alpha.

`TextureArrayAsset::LayerAlphaModes()` exposes the per-layer classification.
`FrameComposer::SetOpaqueLayerModes()` consumes it. A layer without metadata is
treated as `Blend`, so Auto stays off.

## Upscale modes

- Nearest: pixel art, integer blocks, no smoothing.
- Bilinear: cheap soft upscale; blurs detail.
- Bicubic: sharper spatial reconstruction; mild ringing.
- FSR1: spatial edge-aware upscale plus sharpening; no motion vectors.
- FSR2: temporal reconstruction with depth, motion vectors and history.

The player-facing UI uses `upscale_policy` rather than a vendor list:

- Auto: picks DLSS on NVIDIA/Vulkan, otherwise FSR2, otherwise FSR1;
- Universal (FSR): FSR2, otherwise FSR1;
- Off: bilinear.

DLSS is NVIDIA-only. MetalFX remains a reserved serialized value with no adapter.

## Temporal upscaling contract

Temporal upscaling is separated from spatial upscale:

```toml
[render]
allow_temporal_upscale = true
temporal_upscale = 3 # 0 Off, 1 FSR2, 2 DLSS, 3 Auto
upscale_policy = "auto"
frame_generation_policy = "auto"
```

Optional vendor build flags:

```cmake
FBY_WITH_DLSS=OFF
FBY_WITH_FIDELITYFX=OFF
```

Build flags control whether vendor code is compiled. Runtime capabilities still
decide which backend is used. `Rhi::UpscalerCaps` and
`Rhi::BackendCapabilities` gate every path; missing motion vectors, HUD
separation, GPU/OS or DLL falls back.

Frame generation exists:

- custom FG;
- FSR3 FG (FidelityFX dynamic DLL);
- DLSS FG (NVIDIA NGX).

`ProjectSettingsPanel::SetUpscalerCaps` receives
`Rhi::DeviceInfo::upscalers`. The allow/backend/auto controls are drawn only
when `motionVectors && (fsr2 || dlss)`; otherwise the section is absent, not a
disabled dead switch.

## Player settings

`UserSettings` is stored separately from the project:

```text
Saved/Config/UserSettings.toml
```

```toml
[graphics]
window_width = 2560
window_height = 1600
window_mode = 1
vsync = true
quality_preset = 2
render_scale = 1.0
upscale_policy = "auto"
frame_generation_policy = "auto"
oit_enabled = true
```

`ResolveRenderSettings(project, user)` combines project policy with the player
choice. Project-only fields (cutout, static bake defaults, opaque mode) cannot be
changed by the player.

`SettingsSchema.hpp` is the shared UI metadata source; `UserSettingsPanel`
renders the player fields with the same English tooltips.

Editor integration:

- `Saved/Config/UserSettings.toml` is loaded on `EditorSystem::Initialize`;
- the menu item **Player Settings** opens `UserSettingsPanel`;
- changes are clamped and saved back to the same file;
- `Main` uses `ResolveRenderSettings(project, userConfig)` for OIT and upscale
  policy every frame.

`renderScale` now sizes the editor viewport render target while ImGui presents
it at native display size. The current presentation filter is the ImGui
texture sampler (linear); a dedicated FSR1/nearest shader is still pending.
OIT and upscale policy are applied every frame.

## Runtime

`renderScale`, `oitMode`, `opaqueMode` and `staticBakeEnabled` are plain
`ProjectSettings` fields. A game can change them while running; the editor
panel is only one writer. Runtime OIT/upscale changes take effect when the
corresponding pass is wired.
