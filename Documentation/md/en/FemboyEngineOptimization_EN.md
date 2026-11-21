# FemboyEngine2D optimization guide

[English](FemboyEngineOptimization_EN.md) | [Русский](../ru/FemboyEngineOptimization_RU.md)

This is the single map of every performance control the engine exposes: editor
budgets, runtime render switches, asset cooking, SDF lighting, packaging and the
profiling tools that tell you which one matters. Use it after the
[build workflow](../../design/build-workflow.md) and before shipping.

All numbers here are measured on the reference machine: Windows 11, RTX 4060
Laptop, 24 logical cores, Vulkan, editor dashboard at 2560x1600. They are
starting points, not promises for other hardware.

## 1. Measure before changing anything

| Tool | What it answers | Where |
|---|---|---|
| Profiler (F7) | frame time, per-category rows, trace | `Profiler` panel, **Write trace** -> `Profiler/trace.json` |
| Crash Diagnostics | did the build crash, what was the report | `Saved/Crashes/`, **Write report now** |
| Network Diagnostics | rollback latency, loss, desync | session view |
| Editor Settings **Query hardware** | CPU/RAM/GPU/VRAM of the machine | Settings > System |
| `Saved/Accessibility` QA | does the UI publish a usable tree | Accessibility QA |
| `PackageDesktop` report | bundle size and five largest files | `--max-size-mb` run |

Change one variable at a time. A frame-time win that only moves work to the
asset cooker or the download is not a win.

## 2. Editor: switchable profiles and memory budgets

Editor Settings > **Memory and performance** exposes the profiles. `Custom`
keeps the individual values; the three presets set them together.

| Profile | Undo arena | Undo commands | Thumbnail descriptors | Frame limit | Throttle unfocused |
|---|---|---|---|---|---|
| Lite | 2 MiB | 128 | 64 | 30 FPS | on |
| Balanced | 4 MiB | 256 | 128 | 60 FPS | on |
| High | 16 MiB | 512 | 256 | unlimited | off |
| Custom | whatever is set | whatever is set | whatever is set | 0-240 | free |

The underlying knobs:

| Key | Default | Effect |
|---|---|---|
| `undo_memory_mb` | 4 | undo arena; lowering reallocates and clears history |
| `undo_command_limit` | 512 | maximum commands in the undo window |
| `thumbnail_cache_limit` | 256 | Content Browser texture descriptors retained |
| `thumbnail_cache_enabled` | true | master switch; disabling frees descriptors |
| `frame_rate_limit` | 0 | sleeps the main loop; 0 is unthrottled |
| `pause_when_unfocused` | false | caps the loop at 5 FPS without focus |
| `static_bake_budget_mb` (project) | 64 | tiled viewport bake cache |
| `viewport_complexity_budget` | 8 | Shader Complexity heat-ramp reference |

**Free undo history and thumbnails** releases both caches immediately. Lowering
the undo budget intentionally drops the history; the project data is unchanged.

Measured dashboard baseline: 411 MiB working set / 675 MiB private, 345 MiB
dedicated VRAM / 393 MiB local, about 1.05 cores, about 21% GPU. The largest
fixed costs are the swapchain, the SDF shadow mask, the instance ring and the
static mesh arena. Lite mostly helps authoring machines; the frame cap and the
unfocused throttle are the two controls that reduce idle CPU/GPU use.

### Editor render and authoring switches

- **VSync**: off reduces input latency, on reduces tearing and can lower GPU
  power. It is a display policy, not a frame-time fix.
- **Viewport mode**: the diagnostic modes (complexity, overdraw, lighting) add
  work. Use them while profiling, not while judging gameplay cost.
- **Static bake** (project setting): reuses the viewport while the scene, camera
  and lights are unchanged. Lower `static_bake_budget_mb` or disable it on
  memory-constrained machines.
- **Asset watcher/auto-refresh**: the OS watcher and Content Browser refresh are
  convenient but cost I/O on network drives. Disable both on a laptop battery.
- **Typography and UI scale**: larger fonts rebuild the atlas; a stable scale is
  cheaper than changing it every session.
- **Logs**: keep `log_max_files` and `log_max_lines` bounded; a journal that is
  compacted continuously competes with the asset thread.
- **Profiler history**: the Profiler ring is fixed capacity; a lower capacity
  costs less cache and makes the plot less smooth. It never grows unbounded.

## 3. Game: runtime settings that matter

Project Settings and `project.femproj` own the shipping policy. The editor's
own settings never reach a game build.

| Key | Default | Cost / effect |
|---|---|---|
| `render_scale` | 1.0 | internal scene resolution; UI stays native. 0.75 is the usual 1440p -> 1080p win |
| `upscale_mode` | Bilinear | Nearest for pixel art, FSR1 for edge-aware spatial upscale |
| `upscaler_backend` | Auto | FSR, DLSS or MetalFX where available; unsupported backends fall back |
| `upscale_policy` | Auto | Auto/Universal/Off; Off removes the upscale pass |
| `allow_temporal_upscale` | false | enables temporal upscalers and their motion-vector cost |
| `allow_frame_generation` | false | generated frames when a vendor backend is present |
| `oit_mode` | Off | Off is cheapest; Weighted and Accurate add passes |
| `opaque_mode` | Auto | front-to-back opaque path; Auto only when every sprite is opaque |
| `opaque_art_cutout` | false | declares hard alpha; avoids blending but requires correct art |
| `static_bake_enabled` | false | caches an unchanged view; costs viewport memory |
| `virtual_texture` | off | streams pages instead of loading full mips; costs page table/IO |
| `texture_format` | BC7 sRGB | BC7 desktop, ASTC mobile; RGBA8 is uncompressed and largest |
| `max_texture_size` | 2048 | hard cap during import/cook |
| `vsync` | true | presentation policy |

Rules of thumb:

1. Try `render_scale` before adding a new effect. It scales the whole scene
   uniformly and is trivial to validate.
2. Use `opaque_mode=Auto` and correct `opaque_art_cutout` for 2D art with hard
   alpha. Soft alpha belongs in the blend path.
3. Enable OIT only for emitters that need order-independent transparency. A
   scene that can sort by depth should not pay for it.
4. Enable static bake for a paused editor/PIE viewport, not for a moving camera.
5. Turn on virtual texturing only when the texture set exceeds the VRAM budget;
   otherwise it adds indirection to every sample.
6. Keep `max_texture_size` at the target resolution. A 4K source imported at
   2048 saves three quarters of the texture memory and usually looks identical
   at 1080p.

## 4. SDF lighting and shadows

- The scene owns one `SdfSunComponent`; the panel exposes direction, softness,
  ambient, occlusion radius/strength and **steps**.
- `steps` is the per-ray budget (1-64). Halving it is the largest single SDF
  saving and makes the penumbra harder.
- Dynamic occluders are collected up to `Rhi::kMaxSdfGpuOccluders` (16). Extra
  occluders are ignored; the panel warns at the budget.
- Static lights are cached until the scene, occluders or the light change. Do
  not mark a moving torch as static.
- A lower SDF shadow-mask resolution or fewer layers is a project/backend
  budget; use it before removing the sun.

## 5. ECS, fixed step and gameplay

- FixedUpdate frequency is a project policy. 120 Hz is deterministic but costs
  twice the simulation of 60 Hz; `max_fixed_steps` bounds catch-up work.
- Entities and components are fixed-capacity SoA storage. Budgets are compile
  time or project settings; overflow is reported, not silently allocated.
- Physics backend `Auto` picks the DOD backend for large joint-free worlds using
  `physics_body_budget`. Benchmark both before forcing one.
- Render extraction is allocation-free and bounded by the project's item budget.
  Raise it intentionally; do not treat overflow as a normal path.
- AOT scripting keeps the VM out of a shipped build. AOT also removes the
  interpreter's dispatch cost, so measure AOT before micro-optimizing the script.

## 6. Asset cooking and package size

- Import textures as BC7 (desktop) or ASTC (mobile), with mips and the lowest
  acceptable `max_texture_size`.
- Audio cooks to Opus; keep speech at a lower bitrate than music. Unused WAV
  sources never need to ship.
- `fby_fempak cook --exclude <substring>` removes editor-only samples, source
  art and test fixtures from a pak. Match both destination and source paths.
- Localization: ship only the languages the game exposes; a locale table is
  small but 42-language voice packs are not.
- `PackageDesktop.py --game` builds a runtime-only bundle. `--features` selects
  FSR/DLSS/XeSS; `--assets` cooks `game/assets.fempak`; `--max-size-mb` is a
  hard gate with a five-largest-files report.
- Measured: editor bundle 199.9 MiB (mostly vendor upscaler runtimes), game
  bundle without vendor 15.4 MiB before assets.

## 7. Build options

| Option | Default | Use |
|---|---|---|
| `CMAKE_BUILD_TYPE=Release`/`RelWithDebInfo` | - | Release for players, RelWithDebInfo for testers |
| `FBY_ENABLE_LTO` | OFF | cross-TU inlining; slower link, smaller/faster code |
| `FBY_PGO_GENERATE` / `FBY_PGO_PROFILE` | OFF | profile-guided layout for a known workload |
| `build.optimization` (`O2`/`O3`/`Ofast`) | O2 | O3 for hot builds; Ofast must respect the determinism contract |
| `build.lto` | true in generated projects | same as above per project |
| `build.strip_debug_info` | false | strip PDB/symbols for a public build |
| `build.unity_build` | false | fewer TUs, faster build, worse incremental granularity |
| `build.native_arch` | false | `-march=native`; never ship it to unknown CPUs |
| `FBY_CXX_STANDARD` | 23 | 26 is experimental; the runtime is the same |

Do not ship `native_arch` unless the target CPU is known. Do not mix sanitizer
and release objects in one game build.

## 8. Frame generation and upscalers

- Spatial upscalers (FSR1, bilinear) have no temporal history and no motion
  vectors; they are cheap and robust.
- Temporal upscalers need motion vectors and a stable camera. They cost
  bandwidth and can ghost on pixel art; test with the actual art.
- Frame generation is a vendor feature gated by capabilities. It improves
  perceived smoothness, not simulation responsiveness. Never let a network game
  depend on generated frames for input latency.
- Set `upscaler_backend=Auto` unless you have a reason; the selection code
  filters unsupported backends and falls back.

## 9. Budget checklists

**Lite / laptop editor**

- Profile Lite, frame limit 30, throttle unfocused.
- VSync on, UI scale fixed, watcher off on battery.
- Static bake off unless the scene is static.
- Undo 2 MiB/128, thumbnails 64 or off.

**Balanced / desktop editor**

- Profile Balanced, frame limit 60.
- Thumbnail cache 128, undo 4 MiB/256.
- Static bake on for PIE, off while editing a moving scene.

**Shipped 2D game**

- `render_scale` 0.75-1.0, FSR1 or native, VSync per platform policy.
- BC7 + mips, max texture size = target resolution, Opus audio.
- `game` bundle without editor/tools; select only the upscaler features used.
- `--max-size-mb` in CI; review the five largest files every release.
- Profile on the minimum-spec machine, not the development workstation.
- Run the frame-time and allocation checks in RelWithDebInfo, then confirm in
  Release; Debug numbers are not shipping numbers.



## 10. Sprite pipeline variants

Sprites are batched by an explicit shader variant, not by a per-pixel mode check:

- base;
- normal map;
- SDF text;
- SDF outline;
- SDF glow.

The variant is part of the render key. `FrameComposer` sorts by it, so a batch never
mixes a base sprite with a normal-mapped or SDF sprite. D3D11 and D3D12 use dedicated
base/normal pipelines; D3D12 and Vulkan use a depth-disabled SDF pipeline; Metal uses
the same split at source level.

The D3D12 SDF path can be disabled with `FBY_D3D12_DISABLE_SDF=1`; it then falls back
to the normal sprite pipeline. The SDF smoke test renders a real frame and checks the
centre glyph pixel, so a pipeline mismatch cannot pass silently.

## 11. Related documents

- [Editor settings reference](EditorSettingsReference_EN.md)
- [Project settings recommendations](ProjectSettingsRecommendations_EN.md)
- [Viewport modes](ViewportModes_EN.md)
- [Virtual texturing](../../design/virtual-texturing.md)
- [Upscaler selection](../../design/upscaler-selection.md)
- [Custom frame generation](../../design/custom-frame-generation.md)
- [Pause and static rendering](../../design/pause-and-static-rendering.md)
- [Risky RHI optimizations](../../design/risky-rhi-optimizations.md)
- [Performance baseline](../../design/performance-baseline.md)
- [High-resolution benchmark](../../design/high-resolution-benchmark.md)
- [FCS storage](../../design/fcs.md)
- [Physics backends](../../design/physics-backends.md)
