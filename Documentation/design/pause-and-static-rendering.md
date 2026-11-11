# Pause and static-frame rendering

## Pause

`Core::PauseController` is the policy object, `SystemGraph::SetPaused(true)` is
the scheduling switch. A paused graph still runs the `Render` phase and skips
every other phase, so the ECS does not change and the renderer can reuse its
previous frame.

| Setting | Default | Effect |
| --- | --- | --- |
| `PauseSimulation` | `true` | pauses `Core::Clock`; the fixed step and gameplay stop |
| `PauseSfx` | `true` | mutes the SFX bus through `AudioSystem::SetBusPaused` |
| `PauseMusic` | `false` | music keeps playing by default |
| `PauseHudAnimations` | `true` | HUD animation gets a zero `uiDt` |

`SystemContext::uiDt` and `UiSystemView::uiDt` are separate from `dt`. An app
that wants HUD animation to continue while the game is paused feeds
`PauseController::HudDelta(simulationDt, clock.RealDelta())` into `uiDt` and
runs the HUD in the `Ui` phase with `graph.SetPaused(true, true)`. The `Ui` phase
is the only non-render phase the paused graph can keep running; it receives the
real delta through `uiDt` while every simulation phase sees a zero delta and is
skipped.

Gameplay audio has its own `AudioBus::GameSfx` bus. Play-In-Editor routes cloned
`AudioSourceComponent` instances and default script `Play` calls there while a
PIE step is running; editor preview keeps using `Sfx`. The PIE pause menu exposes
separate SFX, music and HUD-animation switches and shows `paused (music muted)`
in the playback status when the game pauses music. During a PIE step,
`AudioSystem::SetGameplayBusActive(true)` maps default `Sfx` playback to the
gameplay bus; editor preview calls outside that scope stay on `Sfx`.

`Phase::Ui` is the only non-render phase a paused graph can keep running:
`graph.SetPaused(true, !PauseHudAnimations)` and the app feeds
`uiDt = clock.RealDelta()` on that phase. The editor and Sandbox run the `Ui`
phase every frame before `Render`.

Pause does not stop the audio device thread; it only changes the bus mute flags,
so music continues from its current position. `AudioSystem::SetBusPaused`
combines with `AudioBusComponent` mute, so an unpause never clears a deliberate
component mute.

## Static frame fast path

`Fcs::World::Revision()` is monotonic. Structural changes and mutable component
access bump it; const views and const `ForEach` do not. `FrameComposer` stores
the last revision, camera, texture array and viewport mode.

When all of them are unchanged, no extra mesh/tile/SDF input is present, temporal
history is off and neither SDF field is bound, `BuildFramePacket` returns the
previous item/key/batch packet without walking the ECS. The `Render` phase still
runs, but the CPU extract/sort/gather cost is zero.

The `ViewportPresenter::Prepare` pass is also skipped on a static frame; it is
read-only and would otherwise walk every sprite. `ViewportPresenter::Prepare`
and `CountReachingLights` now take `const Ecs::Registry&`, so a read cannot bump
the revision.

## Static instance upload

`VulkanRHI` keeps one additional static instance slice per frame in flight. A
static packet is uploaded there once per scene version; repeated static frames
reuse that slice and skip both the staging copy and the device copy. The static
slices are frame-indexed, so a scene change while the previous frame is still in
flight cannot overwrite data that the GPU is reading.

If two different static packets (for example two editor viewports) appear in the
same frame, the second falls back to the normal dynamic slice; the cache is
conservative rather than aliasing one viewport's data into another.

## Measured CPU cost

`FrameComposerStaticBenchmark` (20 frames, large view so nothing is culled):

| Scene | Static before | Static after | Dynamic before | Dynamic after |
| --- | --- | --- | --- | --- |
| 10k sprites | 3.36 ms | ~0 ms | 4.4 ms | 1.83 ms |
| 100k sprites | 33.3 ms | ~0 ms | 44.0 ms | 17.1 ms |

The dynamic extract itself went from 3.3 ms to 0.58 ms at 10k and from 33.0 ms
to 4.5 ms at 100k with eight job threads. Temporal motion extract went from 5.3 ms
to 0.84 ms at 10k and from 65.4 ms to ~8.0 ms at 100k; temporal history and motion
items are now updated inside the same chunk-prefix pass and compacted in place.

The motion history is stored as SoA columns and checks both history epoch and
entity generation, so a recycled entity index cannot inherit the previous
entity's transform. Parallel motion output uses a per-item generation stamp
instead of a per-frame cleared byte array.

The dynamic gains came from six safe changes:

- `size_hint()` counts matching archetype rows instead of walking the whole view;
- `ViewportPresenter::Prepare` is skipped in Standard/UnlitWireframe, where no
  tint table is built;
- optional render filters (`RenderMaskComponent`, `Hidden`,
  `Light2DResultComponent`) are checked once per archetype; hidden chunks are
  skipped entirely, and archetypes without a mask/light do no per-row lookup;
- `SpriteOverlay2D` is counted and emitted inside the same chunk job, including
  the base+overlay prefix offsets;
- tile quads are appended deterministically after the parallel sprite ranges;
- temporal history is owned by `FrameComposer`, indexed by entity index, and is
  updated inside the chunk-prefix extract; motion items keep chunk order;
- motion items are compacted in place, with no second scratch array;
- the simple Standard path extracts chunks in parallel with prefix offsets; the
  serial loop remains the fallback and the deterministic reference.

A separate parallel motion pass was benchmarked and rejected: it cost more than
fusing history into the sprite loop. Motion items are now written by the same
chunk job as the render items.

## Safety contracts

The parallel path runs jobs that only read ECS data and write composer-owned
buffers. It never structurally changes the registry. `FrameComposer` asserts in
debug builds that one instance is built from one thread; the job pool must
outlive the composer. A debug assertion checks every chunk's write count against
its prefix offset. The serial fallback remains the reference and is used for
non-Standard viewport modes and any path that fails the parallel eligibility
checks. A deterministic randomized stress test mixes `Hidden`,
`RenderMaskComponent`, `SpriteOverlay2D`, tile quads and temporal motion, then
compares items, keys and motion vectors byte-for-byte against the serial path.

## Build-level optimisation

Release builds can opt into `-DFBY_ENABLE_LTO=ON` (link-time optimisation) and
PGO with `-DFBY_PGO_GENERATE=ON` for the instrumented run, then
`-DFBY_PGO_PROFILE=<path/to/default.profdata>` for the optimised build. Clang uses
LLVM `-fprofile-instr-generate`/`-fprofile-instr-use`; GCC uses
`-fprofile-generate`/`-fprofile-use`. Neither is enabled by default.

Measured on the 100k composer benchmark, Release builds:

| Build | Extract | Motion | Dynamic |
| --- | --- | --- | --- |
| Release | 1.05 ms | 1.83 ms | 4.19 ms |
| Release + LTO | 1.26 ms | 1.45 ms | 4.14 ms |
| Release + PGO | 1.13 ms | 1.57 ms | 3.92 ms |

The microbenchmark is noisy; PGO shows ~6% on the full dynamic composer, LTO is
within run-to-run noise for this scene. Debug builds are much slower because of
`-O0`, asserts and debug access validation, so they are not a release comparison.

A fused sort/gather was attempted: items were permuted in place and swapped with
the sorted buffer. It was correct only after adding a visited-cycle guard, but the
random permute made 100k gather **7.4 ms** versus **1.5 ms** for the sequential
gather. The fusion was reverted; the copy is cheaper than the random-access walk
on this CPU.

## Dynamic instance deltas

`FrameComposer::SetInstanceDeltasEnabled(true)` records sorted item ranges that
changed since the previous frame. `VulkanRHI` consumes them only when
`framesInFlight == 1`, because then the frame's staging slice still contains the
previous frame; with more frames in flight the slice belongs to an older frame
and applying current deltas would leave stale instances.

With `--frames-in-flight 1`, 100k animated sprites uploaded **48 bytes** per
frame instead of **4.8 MB**; the rest of the instance data stayed in the staging
slice and the device buffer. With the default three frames in flight the path
falls back to the full upload, which is safe but larger. Multi-packet frames and
invalid ranges also fall back to the full upload.

## GPU measurement

`fby_optimization_ab --sprites 100000 --lights 0 --frames 30 --fullview --only
baseline` on the AMD 890M test machine reports CPU ≈12.5 ms, GPU ≈0.96 ms,
4 draw calls and 0 culled items. At 100k the frame is CPU-bound; GPU batching is
not the limiting factor on this scene. The RHI exposes `drawCalls`,
`uploadedBytes`, `gpuFrameMilliseconds`, `gpuMainMilliseconds` and the per-pass
timestamps through `RHI::Stats()`.

## RHI backend benchmark

`fby_rhi_backend_benchmark` is backend-agnostic: it creates the selected RHI,
texture array and render target, composes an animated sprite scene and prints
CPU/GPU time, draw calls, items, batches and uploaded bytes.

```sh
fby_rhi_backend_benchmark --backend vulkan --sprites 100000 --frames 40
fby_rhi_backend_benchmark --backend metal  --sprites 100000 --frames 40
fby_rhi_backend_benchmark --backend vulkan --sprites 100000 --frames 40 --frames-in-flight 1
fby_rhi_backend_benchmark --backend vulkan --sprites 100000 --frames 40 --static
```

On the test machine, Vulkan at 100k animated sprites reports CPU ≈11.8 ms,
GPU ≈0.66 ms, 4 draw calls and 4.8 MB uploaded with three frames in flight; with
`--frames-in-flight 1` the same scene uploads 48 bytes per frame. Metal uses the
same C++ path and reports `unavailable` where the backend or OS lacks Metal.

## Remaining

The GPU still issues the draw calls (the scene has to appear on screen); this
change removes CPU extract, not raster work. Dirty upload is frame-in-flight
limited: a fully incremental path for three frames in flight needs persistent
object slots or per-frame change logs, not just per-frame ranges. Multi-viewport
static packets are intentionally conservative: only one static packet gets the
dedicated slice per frame, the rest use the normal ring.

The gated plan for FIF3 dirty upload and GPU motion vectors is in
[risky-rhi-optimizations.md](risky-rhi-optimizations.md). Neither feature is
enabled: both require flags, fallbacks and three-frame backend tests first.
