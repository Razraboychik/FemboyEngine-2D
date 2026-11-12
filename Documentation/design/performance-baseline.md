# Performance baseline

This page records the first 0.4 alpha measurements. They are not marketing
numbers: the exact hardware is listed and the probes are reproducible from the
test tree.

## FCS versus EnTT (historical)

The fixture that created 100k entities with `Position` and `Velocity` and ran 50
mutation passes has been removed with EnTT; the recorded run is:

| Storage | Time, 50 passes | Relative |
|---|---|---|
| EnTT `view<Position, Velocity>` | 15.1 ms | 1.00 |
| FCS `ForEach<Position, Velocity>` | 4.0 ms | 0.26 |

The FCS pass is about 3.8 times faster on this workload because every chunk
walks two contiguous columns; the EnTT view resolves the second component
through its sparse index for every entity. This is an iteration microbenchmark,
not a full-frame benchmark: physics, upload and GPU time are outside it.

## Engine frame baseline

`fby_requirements_benchmark` builds 10k and 100k sprite scenes, composes 120
frames with `FrameComposer` and reports average frame time and peak resident
memory. It runs without a GPU or window, so the numbers are the CPU-side engine
baseline; the editor adds ImGui, asset caches and the project browser on top.

| Scene | Average frame | Peak RSS | Instance buffers |
|---|---|---|---|
| 10k sprites | 0.20 ms | 8.1 MiB | 1.4 MiB |
| 100k sprites | 3.0 ms | 27.0 MiB | 13.7 MiB |

## Editor backbone baseline

`fby_editor_requirements_benchmark` measures the headless editor backbone:
component registration, an inspector-style traversal of the scene and a bounded
undo transaction. It deliberately excludes ImGui draw cost; the full editor delta
is the sum of this probe and the GPU/UI numbers measured on the reference
project.

| Scene | Inspector pass | Undo transaction | PIE start | Peak RSS |
|---|---|---|---|---|
| 10k sprites | 0.09 ms/frame | 0.94 us/edit | 10.9 ms | 15.0 MiB |
| 100k sprites | 0.87 ms/frame | 0.89 us/edit | 9.2 ms | 33.3 MiB |

The scene build itself is outside the per-frame column; the probe reports peak
RSS after the scene and the transaction loop have run.

A real editor startup was sampled on the same machine: `fby_editor.exe` with the
project dashboard and no project loaded held **291-335 MiB RSS** after 8-12 s
(two runs). A generated reference project with a 10k-entity scene held
**293.4 MiB** and a 100k-entity scene held **302.2 MiB**; the scene logs show
about 0.6 s and 2.1 s from "applied" to "loaded" respectively. These numbers
include ImGui, the asset database and the GPU device, so they are the practical
editor memory floor on this machine.

The same editor was run with `FBY_EDITOR_NOVSYNC=1`, `FBY_EDITOR_STATS=1` and
`FBY_EDITOR_QUIT_AFTER_FRAMES`: at 2560x1600 borderless fullscreen the 10k scene
held **~82-87 FPS**, while the 100k scene held **~32-34 FPS** (~12 ms and ~30 ms
per editor frame). The editor frame includes ImGui, the outliner, the viewport
and the same ECS extract as the sandbox.

## Vulkan 1.4 smoke

`mesh_indirect_smoke` was run on an RTX 4060 Laptop (driver API 1.4.341) at
2560x1600 with three frames in flight. It exercised static and skinned meshes,
indirect draws, the skin dispatch, the SDF shadow mask and the 144 MiB instance
ring; the AMD iGPU on the same machine reported API 1.3.302 and was correctly
rejected by the 1.4 device gate.

## How to reproduce

```bat
cmake --build build --target fby_requirements_benchmark fby_editor_requirements_benchmark
build/bin/tests/fby_requirements_benchmark.exe
build/bin/tests/fby_editor_requirements_benchmark.exe
build/bin/tests/fby_mesh_indirect_smoke_test.exe
```

Record the machine and compiler with every run. A change that makes FCS slower
than the EnTT baseline on the 100k iteration case blocks the FCS migration.

## Remaining systems profile (0.4 alpha)

These probes are CPU-only and independent of the composer:

| Probe | Result |
| --- | --- |
| `physics_backend_benchmark` (2000 bodies) | Box2D 1.60 ms/frame, DOD 2.05 ms/frame |
| `flow_field_benchmark` (256x256) | 18.4 ms build, 59.6k reachable cells |
| `crowd_benchmark` (16384 agents) | 4.24 ms/step |
| `crowd_render_benchmark` (100k items) | 1.43 ms |
| `profiler_benchmark` | 1M records 27.5 ms; 200k scopes 10.0 ms |

The DOD physics backend is slower than Box2D on this small body count, so the
default physics path should stay Box2D unless a scene specifically benefits from
the DOD layout. Crowd/navigation and profiler costs are significant enough to
profile in the real game loop before changing the composer further.

## High-resolution targets

Native 16K/32K is dominated by color/depth target bandwidth, not sprite count.
The measured matrix and the `--dense` full-screen overdraw case are in
[high-resolution-benchmark.md](high-resolution-benchmark.md). Use render scale
and upscaling for 16K/32K; do not clear/draw a native 32K target in the game.

## Dense overdraw

`fby_optimization_ab --dense` fills the target instead of using the sparse grid.
At 8K the dense baseline was ~6.73 ms GPU; `front-to-back` reduced it to ~3.93 ms
and `prepass` to ~4.17 ms. At 4K the extra depth work made both slower (~1.4 ms
versus ~0.69 ms), and at 16K the measurements were dominated by target bandwidth
and clocks. Front-to-back/depth prepass is therefore a scene policy, not a
universal default.

## Text/UI hot path

`fby_text_ui_benchmark` measures one million shaped-cache hits with the Latin
face: **~28.6 ns/hit**, zero misses after the initial shape. This is the UI text
hot path; full widget layout/build still needs its own frame probe.

## Stable benchmark wrapper

`Source/Tools/Dev/BenchmarkMatrix.py` runs static and dynamic passes in
alternating order with a warmup and reports the median. It is the preferred way
to compare modes:

```bat
python Source/Tools/Dev/BenchmarkMatrix.py --runs 3 --width 7680 --height 4320
```

The wrapper accepts `--timeout` and fails cleanly instead of leaving a 16K
benchmark process stuck. The static GPU path can appear slower than dynamic
because the lighter CPU load lets the GPU downclock; the wrapper reduces, but does
not eliminate, that effect.

## Culling fast path

`FrameComposer::SetWorldBounds(min, max)` lets the caller declare conservative
world bounds. When the camera view fully contains them, the extractor skips
per-sprite visibility tests. The caller is responsible for including every
renderable sprite; `ClearWorldBounds()` restores normal culling. This is covered
by the composer test suite.

## PGO from the Sandbox

The Sandbox now accepts `--frames <count>` so PGO can use a real game loop instead
of a Composer-only probe:

```bat
cmake -S . -B build-pgo-gen -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
  -DFBY_PGO_GENERATE=ON
cmake --build build-pgo-gen --target fby_sandbox
set LLVM_PROFILE_FILE=%CD%\build\pgo-game\sandbox-%p.raw
build-pgo-gen\bin\fby_sandbox.exe 15000 --frames 120 --no-vsync
llvm-profdata merge -o build\pgo-game\default.profdata build\pgo-game\*.raw

cmake -S . -B build-pgo-use -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ ^
  -DFBY_PGO_PROFILE=%CD%\build\pgo-game\default.profdata
cmake --build build-pgo-use --target fby_sandbox
```

The 15k-sprite Sandbox run produced a ~400 KiB profile and exited cleanly after
120 frames. Alternating five-second windows, release vs game-PGO Sandbox:
release median **3024 FPS**, PGO median **3148 FPS** (about **+4%**). The GPU was
warm in both cases; this is the practical PGO delta on this scene, not a promise
for every workload.

## PostFX

The dense 16K scene with the default PostFX set (CRT/vignette/bloom/FXAA) costs
~30.3 ms GPU versus ~16.96 ms without PostFX. Fullscreen effects are a second
bandwidth/fill cost on top of sprite overdraw; keep them half-resolution or
disable them for native 16K stress runs.


