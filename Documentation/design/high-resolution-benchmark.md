# High-resolution benchmark protocol

The 15k-sprite / 64-light benchmark is sensitive to GPU clocks, target size and
presentation. A single run is not a result.

## Command

```sh
fby_optimization_ab --sprites 15000 --lights 64 --frames 60 \
  --width <width> --height <height> --scale 1 --fullview --only baseline
```

Use `--static` for the cached-static path and `--render-only` to skip the
target-to-swapchain blit. The harness prints the actual render extent and the
color/depth target bytes.

## Protocol

- 20 warmup frames, 60 measured frames, three runs; report the median.
- Record `cpu_ms`, `gpu_ms`, `gpuDepthMilliseconds`, `gpuMainMilliseconds`,
  `drawCalls`, `uploadedBytes` and the target byte count.
- Reject a run whose `gpuTimestampsValid` is false.
- Compare the same scene and the same actual render extent, not only the
  requested `--width/--height`.

## Measured medians (test machine)

| Render extent | Dynamic GPU | Dynamic FPS | Static GPU | Static FPS |
| --- | --- | --- | --- | --- |
| 3840x2160 | ~0.35 ms | ~2890 | - | - |
| 7680x4320 | ~1.55 ms | ~630 | - | - |
| 15360x8640 | ~5.86 ms | ~170 | ~7.37 ms | ~136 |
| 30720x17280 | 62–2643 ms | 0.4–16 | not stable | not stable |

The table is historical 0.4-alpha data. Treat it as a target, not a current
guarantee: the 2026-11-13 re-run on the RTX 4060 Laptop (driver 610.88) at
15360x8640 measured 11.8–27 ms, i.e. roughly 37–85 FPS depending on clock and
thermal state, not 170. The engine now logs the selected device; a benchmark run
is valid only when that log names the discrete GPU and `gpu_timestamps_valid=1`.

## Device selection

On a hybrid laptop the Vulkan loader may expose the iGPU first and the engine's
1.4 gate then rejects it. Select the discrete GPU explicitly:

```powershell
$env:FBY_VULKAN_DEVICE = "NVIDIA"
# or force a single ICD:
$env:VK_ICD_FILENAMES = "C:\Windows\System32\DriverStore\FileRepository\...\nv-vk64.json"
```

The log must contain `vulkan: selected device 'NVIDIA ...'`. Without that line
the run may have fallen back to a headless/invalid path; do not publish it. Lock
clocks or repeat the three-run median if the result differs by more than a few
percent.

The 32K target is 2.1 GB for color plus 2.1 GB for D32 depth (4.25 GB total).
Even ten-frame runs vary from ~62 ms to ~2.6 s, so a median is meaningless: the
target is memory/bandwidth-bound and the GPU/driver state dominates. 32K native
is a stress case, not a game target.

At 16K the main pass is only ~0.002 ms; the cost is the full target clear and
bandwidth. Interleaving static and dynamic runs at 16K does not give static a
consistent GPU advantage: static GPU ranged ~10–23 ms and dynamic ~11–17 ms,
while static CPU stayed ~0.6–0.7 ms and dynamic CPU ~3.8–4.0 ms. The GPU delta is
dominated by clocks/thermal state and target clearing, not by the static cache.

## Render scale

The same 16K request with a smaller render target:

| Requested | Scale | Actual target | GPU | FPS |
| --- | --- | --- | --- | --- |
| 15360x8640 | 1.0 | 15360x8640 | ~5.9 ms | ~170 |
| 15360x8640 | 0.5 | 7680x4320 | ~2.83 ms | ~354 |
| 15360x8640 | 0.25 | 3840x2160 | ~0.59 ms | ~1700 |

This is the practical 16K/32K path: render at 4K/8K and upscale. Native 16K/32K
is a target-bandwidth stress test, not a game mode.

## Full-screen overdraw

`--dense` distributes the same 15k sprites over the whole target instead of the
small top-left grid:

| Render extent | Sparse GPU | Sparse FPS | Dense GPU | Dense FPS | Dense + PostFX GPU |
| --- | --- | --- | --- | --- | --- |
| 3840x2160 | ~0.35 ms | ~2890 | ~0.70 ms | ~1440 | ~0.90 ms |
| 7680x4320 | ~1.55 ms | ~630 | ~3.44 ms | ~290 | ~8.04 ms |
| 15360x8640 | ~5.86 ms | ~170 | ~16.96 ms | ~59 | ~30.32 ms |

The dense scene is a real fill-rate test. At 16K it costs about 11 ms more than
the sparse grid, and the default PostFX pass adds another ~13 ms. Overdraw,
front-to-back/depth-prepass and PostFX are therefore scene policies for a
full-screen sprite scene, not universal defaults.




## Bottleneck order

1. 4K: CPU ~2.5 ms, GPU below 1 ms; optimize CPU (PGO, motion, other systems).
2. 8K: GPU ~1.5 ms; bandwidth starts to dominate.
3. 16K: GPU ~6 ms; target clear/bandwidth dominates, not sprite fill.
4. 32K: target bandwidth makes native rendering impractical.

## Safe next steps

- Render at 4K/8K and upscale for 16K/32K.
- Remove unnecessary attachments; choose the smallest depth format the 2D
  pipelines can use.
- Avoid extra fullscreen copies and keep post effects half-resolution.
- Overdraw/front-to-back only matters for a full-screen sprite scene; the current
  full-view grid leaves most of a 16K target empty.
