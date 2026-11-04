# Upscaler and frame-generation selection

## Default policy

Every backend is off by default. The project enables a policy, the user picks a
mode, and the device capabilities decide whether the option exists. One upscaler
and one frame generator may be active. HUD is always composited at native
resolution outside the temporal history.

## Hardware matrix

| Hardware | Recommended upscaler | Frame generation | Notes |
|---|---|---|---|
| NVIDIA RTX 20/30 | DLSS 2/3 or FSR2 | DLSS FG on RTX 40+, otherwise custom | NGX runtime and driver required |
| NVIDIA RTX 40/50 | DLSS + DLSS FG | DLSS FG | Best NVIDIA path |
| AMD RDNA2/3/4 | FSR2, fallback FSR1 | FSR3 FG or custom | FSR is the portable path |
| Intel iGPU | FSR1 | Off | Sharpen lightly, keep render scale high |
| Apple M-series | FSR2 | Off | MetalFX is reserved but has no adapter yet |
| Steam Deck | FSR1 or FSR2 | Off | Prefer stable frame pacing |
| Low-end GPU | FSR1 | Off | Keep 0.75-1.0 render scale |
| No supported GPU | Bilinear | Off | Native fallback |

## Project matrix

| Project type | Upscaler | Frame generation | Why |
|---|---|---|---|
| Pixel art, integer scale | Nearest or FSR1 | Off | Temporal algorithms soften pixel edges |
| Painted 2D, slow camera | FSR2 or DLSS | Custom on high-refresh displays | Temporal reconstruction recovers detail |
| Fast action | FSR1 or DLSS | Off or custom only with confidence fallback | Smearing and latency are visible |
| HUD-heavy game | FSR1 | Custom with native HUD | Text must not enter temporal history |
| 4K/8K | FSR2, DLSS | Optional | Fill rate dominates |
| Static backgrounds | FSR1 + static bake | Off | Static bake already removes redraw |
| Transparent/OIT-heavy | FSR1 | Off | Reactive masks are mandatory for temporal paths |
| Competitive multiplayer | Off or FSR1 | Off | Input latency and ghosting are unacceptable |
| Cinematic/cutscene | DLSS/FSR2 | Vendor or custom | Stable camera and known motion; MetalFX is reserved |

## Fallback chains

```text
DLSS      -> FSR2 -> FSR1 -> bilinear
MetalFX   -> FSR1 -> bilinear   # reserved; no adapter yet
FSR2      -> FSR1 -> bilinear
Vendor FG -> custom FG -> off
Custom FG -> off
```

A backend that is not compiled, not supported by the GPU, not supported by the
OS, or missing motion vectors is removed before rendering. It is not presented as
a disabled option unless the UI needs to explain why the project policy is
inactive.

## Settings

Project policy:

```toml
[upscaling]
backend = "auto"
allow_fsr1 = true
allow_fsr2 = true
allow_dlss = false
allow_metalfx = false
upscale_policy = "auto"

[frame_generation]
frame_generation_policy = "auto"
allow_custom = false
allow_vendor = false
```

User settings:

```toml
[graphics]
upscale_policy = "auto"
frame_generation_policy = "auto"
fsr1_sharpness = 0.35
```

The in-game menu shows only modes allowed by the project and confirmed by the
device. `Auto` resolves to the best available backend and falls back silently,
reporting the choice in diagnostics.

## Licensing

| Backend | Distribution |
|---|---|
| FSR1/FSR2/FSR3 | FidelityFX license, notices required |
| DLSS | NVIDIA RTX SDKs License, `third_party/dlss`, object code inside the app |
| MetalFX | Apple system framework, Apple SDK terms |

Vendor files stay outside MPL-licensed directories. The engine source remains
MPL 2.0; vendor components keep their own licenses and notices. No vendor SDK is
distributed as a standalone product.

## Acceptance checks

- all modes default off;
- unsupported modes absent;
- one upscaler and one generator;
- HUD native;
- fallback works without SDK;
- FSR1 0.5/0.75 tested;
- FSR2/DLSS/MetalFX capability tests;
- frame-generation latency and ghosting tests;
- notices gate passes.
