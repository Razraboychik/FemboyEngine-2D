# FemboyEngine2D

[English](README.md) | [Русский перевод](READMEs/README_RU.md)

**A data-oriented 2D game engine written from scratch in C++26.**

[![License: MPL 2.0](https://img.shields.io/badge/License-MPL_2.0-brightgreen.svg)](LICENSE)
[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)]()
[![CMake 3.28+](https://img.shields.io/badge/CMake-3.28%2B-064F8C.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)]()
[![Status: 0.5.0](https://img.shields.io/badge/status-0.5.0-blue.svg)]()
[![Website](https://img.shields.io/badge/website-FemboyEngine2D-blue.svg)](https://razraboychik.github.io/FemboyEngine-2D/)

FemboyEngine2D is a compact, deterministic, data-oriented engine for 2D games:
a hand-written Vulkan/Metal RHI, the FCS chunked ECS storage, a declarative
`SystemGraph` with opt-in parallel execution, Box2D v3 physics, rollback
networking, a transactional asset pipeline, a full editor and a clean-slate
project generator.

The runtime has no `std::function` on the frame path, no allocation on the frame
path, no JSON in the shipped runtime and no hidden object graph. Components are
trivially copyable data; systems are free functions; the frame is a flat
`RenderItem` array submitted through one RHI packet.

**Status: 0.5.0.** FemboyEngine2D is in the public 0.5 release line. Its
runtime includes the FemboyScript debugger (source spans, breakpoints, stepping,
locals and self fields), isometric and tile-based maps with a map editor,
skeletal IK, retargeting and clip compression (`.fanim`), grid navigation, 2D
lighting, UDP transport with reliability and MTU fragmentation, live spatial
audio, texture thumbnails in the editor and import backpressure. Public CMake
targets and C++ namespaces are PascalCase (`FemboyEngine2D::Engine`, `Fby::Ecs`,
`Fby::Rhi`). Hybrid SDF destruction, transport groups for elevators, trains and
moving platforms, FSR/DLSS-class upscaling and frame generation, and voice
effects with conditions are part of the same runtime. The engine is strictly 2D:
there is no 3D renderer, 3D physics or 3D scene format.

---

## Clean Slate projects

A new project is deliberately empty. `CreateProject()` writes only:

```text
<Name>/
├─ CMakeLists.txt          links FemboyEngine2D::Engine
├─ GameMain.cpp            window -> RHI -> registry -> fixed step -> extract
├─ project.femproj         project descriptor (paths, simulation, render, build)
├─ Plan/                   plans and roadmaps (never cooked)
├─ Disdocs/                design documents (never cooked)
├─ assets/
│  └─ scenes/main.toml
├─ build/
├─ archives/
└─ stores/
```

`Plan/` and `Disdocs/` are authoring-only: they sit outside `assets/`, so
nothing from them enters `.fempak`, `.femarc` or a player build.

There are no player, enemy, camera or gameplay components in the template. The
generated `GameMain.cpp` contains exactly the system boundary every project
needs:

```text
WindowManager (SDL3)
        │
        ▼
RHI (Vulkan / Metal / Null)
        │
        ▼
ecs::Registry + SystemContext
        │
        ▼
SystemGraph: PreUpdate -> FixedUpdate -> PostUpdate -> Render
        │
        ▼
allocation-free render extract -> BeginFrame/Submit/EndFrame
```

Gameplay is added by registering free functions on the graph:

```cpp
ScopedGraph.Add(Fby::Ecs::Phase::FixedUpdate, "my_system", &MySystem, &MyState);
```

No `std::function`, no capturing lambda and no per-frame heap allocation is
required. The template is defined once in
[`Source/Engine/Assets/ProjectScaffold.hpp`](Source/Engine/Assets/ProjectScaffold.hpp)
and is used by the editor dashboard and `CreateProject()`.

---

## What is in the engine

FemboyEngine2D is built around three ideas: **ECS** for ownership, **DOD** for
layout and **SDF** for geometry that must stay independent of resolution. The
table maps the subsystems; the architecture section explains why they share one
memory model.

| Domain | Core pieces | Memory and execution contract |
|---|---|---|
| Core | `Definitions.hpp`, `FemCast<>`, checked `Cast<>`, `FORCEINLINE`, vectors, Mirror reflection, jobs, SDL3 platform, plugin ABI | Value types, checked casts compile away, no RTTI in hot paths, worker pool with latched task-fault containment |
| Effects | Fixed-capacity CPU particle pool, 16 editable presets, 12 white-mask shape slots, deterministic gravity/wind/drag/turbulence/swirl | One span of `RenderItem`s per frame, no allocation, the caller owns the mask atlas and palette |
| RHI | Persistent frame rings, 48-byte `RenderItem`, 64-bit keys, engine-side `Rhi::FrameComposer`, Vulkan indirect MDI, one compute dispatch for LBS, Metal and null backends, FSR/DLSS-class upscaling and frame generation contracts | One flat packet per frame, explicit frame lifetime, no per-item allocations, bindless-style keys |
| ECS | FCS chunked registry, trivially copyable components, four-phase schedule, `SystemGraph` with opt-in parallel waves and declared service/component access, Snapshot v5 | Archetype/chunk storage, systems borrow spans instead of owning objects, snapshots are explicit copies; v4 snapshots are rejected |
| Input | Fixed-capacity `InputSystem` actions/contexts/modifiers/triggers, per-user action state, event ticks/timestamps, SDL3 gamepad hotplug and dead zones; no wheel input | No heap in the input path; bounded events and mapping tables; deterministic per-tick sampling |
| Gameplay | Project tag database edited in Project Settings, hierarchical `GameplayTags`, attribute registry, data-driven instant/duration/periodic effects with stacks, skills with cost/cast/cooldown | Data tables and fixed component layouts; effects run through command buffers, not hidden object graphs |
| AI | `StateTree` to command buffer to `AIController` to perception, navigation and skills; `ActionId` is data for game-owned ECS systems, tag-driven targets, grid and convex navmesh, raw/LOS/Chaikin/Catmull-Rom paths, deterministic route randomization | Bounded state trees, per-frame repath budget, no background allocation from the AI tick |
| Physics | Box2D v3 for joints, CCD and general bodies; data-oriented AABB solver for large joint-free worlds; per-level gravity, fields, joints, cloth, shallow water and configurable hitscan/swept/dynamic projectiles | DOD stores bodies in SoA columns and a uniform grid, with per-body time and gravity scale and begin/end touch events; joint/CCD paths route to Box2D; no solver allocation per body |
| Network | `NetRollbackSession`, 256-tick snapshot ring, input prediction, late-input replay, stable `NetEntityId`, relevance filtering, UDP `NetTransport` with acks, retransmit and MTU fragmentation, versioned `InputPacket`s | Ring-allocated history, bounded queued inputs, explicit rollback copies and no hidden per-packet heap churn |
| Assets | VFS with owning `Pin()`, `.femeta` transactions, BC7/ASTC texture cooker, audio/localization cookers, watcher, `.fanim` compressed clips | `mmap` or owning pin, explicit lifetimes, transactional metadata, a sorted path index mounted in one pass, cooker writes outside the frame path |
| Distribution | `FemArchiver` `.femarc`, optional LZMA, AES-256-GCM / ChaCha20-Poly1305, `fby_fempak secure` | Streaming archive writer, bounded block buffers, deterministic layout |
| Platform | C-ABI `PlatformService`, Steam/EGS providers, deterministic achievements/cloud saves | Plain C ABI, fixed provider table, no C++ ABI leak into plugins |
| Animation | 2D skeleton SoA, LBS, blend cycles, `AnimGraph`, animation timeline, PIE seqlock hand-off, two-bone/FABRIK IK, retargeting, pose and clip quantization | Skeleton and pose data stay in flat arrays; the PIE snapshot uses a seqlock, not a locked scene copy |
| Isometry | `Grid::ProjectionMode` side/top-down/iso-rect/iso-hex, cell/world conversion, depth keys, `TilemapComponent` with a culled render bridge and a tilemap editor | Integer grid math and packed depth keys; tile rendering uses a culled span, not per-tile objects |
| Navigation | Cost-grid A* on rect and hex layouts, tilemap-derived maps, authored `NavAgentSettings` separated from runtime paths, per-frame repath budget | Cost grids are contiguous arrays; path storage is bounded and reused across frames |
| Lighting | `Light2DComponent` radial lights, ambient, emissive targets, optional Reinhard tonemap, screen-space SDF shadow mask, static `SdfField` and dynamic `Light2DOccluderComponent` casters | Static SDF is uploaded once per version; dynamic casters live in a bounded UBO; the shadow pass samples textures, not scene objects |
| Audio | DSP graph with buses/ducking/reverb, zones, limiter, Ogg/Opus, live spatial gain and pan, voice capture/AEC/RNNoise, uplink/downlink effects with conditions, `.fbyvoice` preset packs, a plugin preset table and listener-side reaction cues | Preallocated voices and buses, no per-frame graph construction, explicit capture buffers |
| UI | Procedural PinkUI SDF primitives, DPI scaling, themes, hypertext, widget kit, Unicode boundaries through libunibreak, opt-in Tab/arrow focus, CPU clip rects, HarfBuzz text stack, accessibility kernel | Retained data, per-frame command spans, clipped quads and SDF primitives instead of one allocation per widget |
| Editor | Transactional `CommandStack`, ImNodes canvases, project manager, PIE Play/Pause/Stop, Play (Multiplayer), script debugger, tilemap editor, texture thumbnails, Transport Groups panel, borderless fullscreen | Authoring commands are explicit transactions; PIE owns an isolated world and releases it on Stop |
| Script | LogicGraph to bytecode VM and to C++26 AOT plugin, physics/capability nodes and natives, debug info, VM breakpoints and stepping, `Compile + Nativize`, AOT manifest cook gate | Fixed VM stack and register file, native modules for cooked builds, no script allocation in the fixed step |

---

## Architecture: ECS, DOD and SDF

### ECS: data owns behavior

Entities are identifiers. Components are plain, trivially copyable values. Systems
are free functions or small stateless classes that receive an `Ecs::SystemContext`
and borrow spans from the registry. There is no `Update()` on a component, no
virtual dispatch in the tick and no hidden owner for gameplay state. The context
groups subsystem pointers into domain bundles (render, physics, audio, text,
gameplay), and a system reads only the bundles it declares; the core components
live in `CoreComponents.hpp`, so a module that only needs transforms does not pull
in text or big-number headers. A scheduled system receives a narrow view built by
`SystemContext`: `PhysicsSystemView`, `RenderSystemView`, `GameplaySystemView`,
`AudioSystemView`, `UiSystemView`, `AccessibilitySystemView` or `CoreSystemView`.
`Bind*System<&Function>()` adapts that view to the graph's function-pointer ABI.
The editor coordinator, the script host and the plugin thunk keep the full context
because they deliberately bind every domain.

The schedule has four phases: `PreUpdate`, `FixedUpdate`, `PostUpdate` and
`Render`. `SystemGraph` compiles dependency edges from stable names, service
access and component read/write masks. Same-phase systems run serially by
default; a system opts into parallel waves with `ParallelSafe`, and even then a
declared service or component conflict keeps the pair ordered. This makes the
safe path the default while still allowing explicit parallel batches. A system
that mutates a component is registered by stable name, so determinism and replay
do not depend on declaration order. Loading, reloading or unloading a plugin
rebuilds the compiled order before the next frame.

### DOD: memory is the frame budget

DOD is not an extra layer on top of ECS; it is the rule that governs storage.
Components and render items live in flat arrays. The DOD physics backend keeps
positions, previous positions, velocities, accelerations, rotations, inverse
masses, gravity scales, extents, flags and sleep counters in separate columns, so
integration and the uniform-grid broadphase stream only the values they read. Hot
systems iterate contiguous spans, use bounded pools and frame arenas, and do not
allocate on the frame path. A 48-byte `RenderItem` and 64-bit sort keys are part
of that contract: the renderer consumes one flat packet, not a tree of objects.

There is no tracing garbage collector and no hidden ownership graph. Memory is
released by scope, arena reset, mount unmount or an explicit `Shutdown()`. This
is the main reason the engine can keep frame time predictable: cache locality is
the default, and the cost of a system is visible in its data layout.

### Determinism: one arithmetic contract

Rollback compares state by value, so the simulation does not use the platform
libm. `Engine/Core/SimMath.hpp` provides `Sin`/`Cos`/`SinCos`, `Atan2`, `Acos`,
`Cbrt`, `Exp`, `Log`, `Pow` and `WrapPi` built from exact range reduction and
fused multiply-add polynomials, with `ldexp` only for exponent scaling. The
results are bit-identical across Clang and GCC on every supported target, and
every simulation-side angle, falloff and damping curve uses that layer. Box2D
still owns its own internal floating-point path, which is why joints and CCD are
kept on the general-purpose solver rather than mixed into deterministic DOD
worlds.


### SDF: geometry without resolution

Signed distance fields are the resolution-independent geometry layer. The same
field drives collision, CSG carve/destruction, screen-space shadow masks and the
procedural SDF primitives in PinkUI. Static fields are baked at a fixed
resolution and uploaded once per version; moving occluders use small bounded
buffers; sparse brick atlases keep large fields practical. A field can be
evaluated on the CPU for gameplay and sampled on the GPU for lighting without
rebuilding the scene. Deformable characters use bone-bound SDF parts with a
polynomial smooth union; the resolved field is baked into the render mesh bridge
for the GPU and sampled by the same collision path on the CPU. Silhouette and
collision follow the skeleton at every zoom; there is no distance LOD switch.

### Why this matters for high resolution

The render path is built to scale by memory bandwidth, not by object count. That
is why **8K, 16K and even 32K are a realistic target on top-tier hardware**: the
frame is a flat packet, the geometry is resolution-independent, and the SDF
layers do not turn every pixel into a scene-graph traversal. This is a design
ceiling, not a benchmark promise; actual frame time depends on the scene,
overdraw, upscaling mode and VRAM. Measure on the target GPU before shipping a
resolution tier.

The same model keeps memory efficient without a garbage collector: frame pools
and arenas reserve up front, authoring tables grow on demand, snapshots are
explicit, and systems reuse buffers across frames instead of allocating per
entity or per widget.

Related design notes: [camera effects](Documentation/design/camera-effects.md),
[transport physics](Documentation/design/transport-physics.md) and
[vehicle wheels](Documentation/design/vehicle-wheels.md).

---

## Setup

One-click scripts install the pinned toolchain and materialise the exact sources
in `third_party/`; CMake then prefers those local checkouts. The manuals are
published as Markdown, Word, PDF and DjVu; open the DjVu editions with Okular (or
any DjVu viewer). The documentation tooling is local to the publishing machine
and is not an engine dependency - see
[Documentation/README.md](Documentation/README.md).

```bat
SetupScripts/Windows/Setup.bat
SetupScripts/Windows/SetupCpp.bat
SetupScripts/Windows/SetupArchiver.bat
SetupScripts/Windows/Setup.bat -WithVendorSdks -AcceptVendorLicenses
```

```bash
./SetupScripts/Linux/Setup.sh          # Ubuntu/Debian, everything
./SetupScripts/Linux/SetupCpp.sh       # compiler, CMake, source dependencies
./SetupScripts/Linux/SetupArchiver.sh  # 7-Zip
./SetupScripts/Linux/Setup.sh --with-vendor-sdks --accept-vendor-licenses # optional DLSS/FidelityFX/XeSS SDKs
./SetupScripts/macOS/Setup.command     # macOS, also works from Finder
./SetupScripts/macOS/SetupCpp.command
./SetupScripts/macOS/SetupArchiver.command
./SetupScripts/macOS/Setup.command --with-vendor-sdks --accept-vendor-licenses
```

Versions are fixed at SDL3 `release-3.4.18`, Vulkan-Headers `v1.4.366`, glslang
`15.3.0`, miniaudio `0.11.23`, Box2D `v3.2.0`, libunibreak `6.1` and LZMA SDK
`25.01`. Vendor
upscaler SDKs are optional and are not part of the archive; see
[Frame Generation](Documentation/md/en/FemboyEngineFrameGeneration_EN.md).

---

## Build

Requirements: CMake 3.28+, Ninja, and one of **Clang 23.1.4+**
(clang-cl on Windows, clang++ on Unix), **MSVC 19.44+**, **AppleClang 21+** or
**GCC 16.2.0+**. clang-cl is the reference toolchain; the others are supported
alternatives, never an obligation.
On Windows, run from a shell that provides the Windows SDK headers and
libraries. The setup scripts install clang-cl / clang++ as the reference
toolchain and GCC 16.2.0 (MinGW-w64 UCRT) as the second opinion. On Windows,
`SetupScripts/Windows/SetupCpp.bat` downloads the official **LLVM 23.1.4** MSI
and verifies the `clang-cl` version.

The validated build uses **clang-cl 23.1.4 on Windows and clang++ elsewhere**;
that is what CI and the release gate run. **MSVC 19.44+**, **GCC 16.2.0** and
**AppleClang 21+** are supported alternatives; the compiler-selection matrix is in
[build-workflow.md](Documentation/design/build-workflow.md).

```bat
set PATH=C:\Program Files\LLVM\bin;%PATH%

cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
```

Linux and macOS use the matching presets; they select Vulkan and Metal
respectively and build the editor, tools and tests:

```bash
cmake --preset linux-release && cmake --build --preset linux-release
cmake --preset macos-release && cmake --build --preset macos-release
```

The Metal backend is selected on Apple; Vulkan on Windows/Linux; the null
backend keeps CI and servers headless. Windows is the locally validated gate;
the Linux and full Windows gates are green, and the focused macOS build/package
gate is green. The macOS runner does not cover physical Metal GPU behavior.

The per-platform status, seams and cook flow are tracked in
[Platform support](Documentation/design/platform-support.md).

Main executables:

```text
build/bin/fby_sandbox
build/bin/fby_editor
build/bin/fby_server
build/bin/fby_lobby
build/bin/tools/fby_fempak
build/bin/tools/fby_scene_tool
build/bin/tools/fby_aotc
```

Optional vendor upscalers are opt-in: DLSS and FidelityFX SDKs are not committed.
Install the SDK locally and pass `-DFBY_WITH_DLSS=ON -DFBY_DLSS_ROOT=<path>` or
`-DFBY_WITH_FIDELITYFX=ON -DFBY_FIDELITYFX_ROOT=<path>`; portable FSR1/FSR2 and
custom frame generation do not require either SDK. See
[Frame Generation](Documentation/md/en/FemboyEngineFrameGeneration_EN.md).

Applications look for `assets` in `FBY_ASSET_DIR`, `cwd/assets` or the
executable's own directory and up to three parents, so a run from `build/bin`
works without copying files. `Saved/` (logs, config, cache) follows the working
directory; `FBY_STORAGE_DIR` moves it.

The apps resolve `assets` from `FBY_ASSET_DIR`, `cwd/assets`, or the executable's
parent directories, so an IDE launch from `build/bin` works without copying
files. `Saved/` (logs, config, cache) still follows the working directory;
`FBY_STORAGE_DIR` moves it.

---

## Tests

The suite is a set of plain executables, no test framework. It covers snapshots,
rollback, RHI smoke, script VM/AOT, the script debugger (spans, breakpoints,
stepping, fields), audio including live spatial gain/pan, video, cooking,
animation and clip compression, isometric grid math and tilemaps, navigation,
lighting, UDP transport on loopback, editor command transactions, PIE, physics,
SDF collision and destruction, transport groups, weather, accessibility and
data-driven prefabs. The editor/PIE tests verify
that a play session owns an isolated Box2D world and that Stop restores the
authoring scene, and that the PIE client viewport creates a second offscreen
target, renders the replicated client world into it and frees the target on Stop.
The sandbox client test drives `fby_sandbox --pie-client` headlessly: argument
parsing, a baseline and a delta over loopback UDP, and the timeout exit when the
host stops publishing. The PIE voice test carries synthesized client speech to
the host through the PIE loopback voice link and checks mute and push-to-talk
gating. The security tests cover the FBY XX handshake KATs, the signed-ticket
prologue binding, the v2 record layer (`record_layer_v2`), the noise-c XX vector
(`noise_conformance`) and the handshake/voice token buckets (`noise_kats`,
`ticket_binding`, `rate_limit`). The identity test covers the versioned DPAPI /
0600 store and the migration from the bare record (`identity_storage`). The audio
quality tests cover RNNoise wet/dry (`rnnoise_mix`), Opus FEC recovery
(`voice_fec`), the first-packet jitter policy (`jitter_init`), device hash/raw
collisions (`device_id`) and the panel's live output path (`voice_device_panel`).
`ctest -N` prints the current test inventory; this README does not restate a
count that changes with every package.

```bash
ctest --test-dir build --output-on-failure
```

The style and version gates are machine-checked:

```bash
python Source/Tools/Dev/CheckStyle.py
python Source/Tools/Dev/CheckVersion.py
```

---

## Using the editor

The editor opens a project from three places:

- **Explorer or the command line**: `fby_editor path\to\project.femproj`
  (a project folder or `--project <path>` works too). Register the file
  association once for the current user so a double-click opens the editor:

  ```powershell
  powershell -ExecutionPolicy Bypass -File SetupScripts/Windows/RegisterFemboyProjects.ps1
  ```

- **Project dashboard**: it appears when no project is open. Open Project browses
  `*.femproj` files; New Project creates the typed tree under `Projects/` and
  opens it immediately.
- **Working directory**: if the current folder contains `project.femproj`, the
  editor opens it on startup.

The window starts borderless fullscreen; F11 (or View > Fullscreen) toggles it.
Editor typography defaults to 18 px body and 24 px titles (the untouched 16 px
values in older projects are migrated once); the per-category sizes and the UI
scale live in Project Settings > Editor Settings.

**Authoring a scene.** The editor ships without a console window; the log is in
`Saved/Logs/editor`. The sink keeps the newest 20 files per category and the
newest 50,000 lines per file (Editor Settings > Logs changes both limits). The
Hierarchy and the Viewport toolbar have `+ Entity`,
`Duplicate` and `Delete`, and the `Entity` menu repeats them (Focus Selected is
`F`). A new entity gets a Transform, a 64x64 sprite and a draw order at the
viewport centre and is selected immediately; Add Entity and Duplicate are one
undo step (Ctrl+Z), and Delete is undoable too (the entity returns with all of
its components). `File > New Scene...` writes an empty scene next to the project
scenes and opens it; Ctrl+S saves the current scene.

Entities can carry a name: double-click a Hierarchy row to rename it and use the
search field to filter the list. Selection is multi-entity (Ctrl+click toggles,
Shift+click adds, in the Hierarchy and the Viewport); Ctrl+C / Ctrl+V / Ctrl+D
copy, paste and duplicate the whole selection as one undo step. The Viewport
`Snap` toggle rounds the gizmo and the step buttons to the grid (8/16/32),
15 degrees and 0.1 scale.

`Entity > Create Prefab from Selection...` saves the selected entities under
`assets/prefabs`; the prefab can be dragged from the Content Browser into the
Viewport to spawn it. The Content Browser's `File` menu creates folders and
scenes, renames, duplicates and deletes project assets (Delete asks for
confirmation), and double-clicking a `.toml` scene opens it in the editor.

The window title marks unsaved work with `*`. Closing the window or quitting with
a dirty scene asks Save / Discard / Cancel; `File > Autosave scene (60 s)` keeps
saving the open scene while editing. In Play-In-Editor, `Step` (or F10) advances
exactly one fixed tick while the session stays paused.

The viewport is interactive: the wheel zooms around the cursor, the middle or
right mouse button pans (the Camera speed slider scales both the drag pan and the
arrow keys), a left click selects the topmost sprite or collider under the cursor
(empty space clears the selection), Delete removes the selected entity and F
centres the view on it. A second toolbar row below the first one holds the
transform tools: Move/Rotate/Scale (also W/E/R), a World/Local space toggle and
one-shot steps for the selection (`X -`, `X +`, `Y -`, `Y +`, `-5 deg`/`+5 deg`,
`Scale -`/`Scale +`; hold Shift for a bigger step) — every step is a single
undo. Zoom is a left-dragged slider from 25% to 800% and `Fit` frames the
selection or resets the view, so neither zoom nor camera speed needs the mouse
wheel. A HUD in the corner shows the X/Y compass and the zoom factor, and when
the world origin is outside the visible area an arrow on the viewport edge points
to it with its bearing in degrees and the distance in world units (`0,0 209 deg
734 u`). Escape does not close the editor; quit from `File > Quit` or Ctrl+Q.

Automated checks can use `FBY_EDITOR_QUIT_AFTER_FRAMES=<n>` (run n frames and
shut down), `FBY_EDITOR_SMOKE_PROJECT=<folder>` (create the project if it is
missing, then open it) and `FBY_EDITOR_SMOKE_DASHBOARD_OPEN=<project file>`
(press Open on the dashboard after `FBY_EDITOR_SMOKE_DASHBOARD_FRAME` frames,
default 600). The log records every shutdown phase in milliseconds, so a stalled
exit names the phase it is stuck in.

The editor and the games are linked as Windows GUI programs: starting them from
Explorer or a shortcut shows only the game window. Configure with
`-DFBY_CONSOLE_APPS=ON` to keep a console attached for development; assertions
are written to the log file in both cases.

---

## Documentation

The documentation set is published as English and Russian translation, with
independent Markdown, Word, PDF and DjVu editions. Start at
[Documentation](Documentation/README.md); the Russian translation of this README
is [the Russian README](READMEs/README_RU.md).

- [Architecture specification (EN)](Documentation/md/en/FemboyEngineArchitecture_EN.md)
- [Application guide (EN)](Documentation/md/en/FemboyEngineApplicationGuide_EN.md)
- [API reference (EN)](Documentation/md/en/FemboyEngineAPIReference_EN.md)
- [Complete reference manual (EN)](Documentation/md/en/FemboyEngineCompleteReference_EN.md)
- [CMake project and engine management (EN)](Documentation/md/en/CMakeProjectManagement_EN.md)
- [FemboyScript manual (EN)](Documentation/md/en/FemboyScript_EN.md)
- [Physics, SDF destruction and transport groups (EN)](Documentation/md/en/Physics_EN.md)
- [Frame generation and upscaling (EN)](Documentation/md/en/FemboyEngineFrameGeneration_EN.md)
- [Viewport display modes (EN)](Documentation/md/en/ViewportModes_EN.md)
- [Editor settings reference (EN)](Documentation/md/en/EditorSettingsReference_EN.md)
- [Project settings recommendations (EN)](Documentation/md/en/ProjectSettingsRecommendations_EN.md)
- [Code style](Documentation/style/CodeStyle.md)
- [Naming conventions](Documentation/style/NAMING.md)
- [Virtual texturing contract](Documentation/design/virtual-texturing.md)
- [Editor workflows and scene authoring](Documentation/design/editor-workflows.md)
- [Play-In-Editor and the client viewport](Documentation/design/pie-multiplayer.md)
- [Text stack](Documentation/design/text-stack.md)
- [Camera motion and distortion](Documentation/design/camera-effects.md)
- [Transport physics](Documentation/design/transport-physics.md)
- [Vehicle wheels](Documentation/design/vehicle-wheels.md)
- [Input system](Documentation/design/input-system.md)
- [Physics backends design](Documentation/design/physics-backends.md)
- [CodeQL audit](Documentation/design/codeql-audit.md)
- [Time, timers and dates](Documentation/design/time.md)
- [Accessibility contract](Documentation/design/accessibility.md)
- [World systems: tilemaps, IK and navigation](Documentation/design/world-systems.md)
- [Lighting and SDF shadows](Documentation/design/lighting.md)
- [Networking: transport and replication](Documentation/design/networking.md)
- [Scripting: debugger and nativization](Documentation/design/scripting.md)
- [Voice subsystem: capture, DSP, AEC and networking](Documentation/design/voice.md)
- [Security notes: FBY XX, ticket binding, rate limits](Documentation/design/security.md)
- [Protocol v2 binding and finishing checklist](Documentation/design/protocol-v2.md)
- [ADR 0001: standard Noise as the long-term secure transport](Documentation/adr/0001-secure-transport.md)

The generated documentation keeps its historical `FemboyEngine*` file names so
existing links remain valid; the product name inside the documents is
FemboyEngine2D.

---

## Repository layout

```text
FemboyEngine2D/
├─ CMakeLists.txt, cmake/        build configuration and dependency pins
├─ Version.txt                   single source of the engine version
├─ Source/
│  ├─ Engine/                    Core, RHI, ECS, Physics, Audio, Network,
│  │                             Assets, Script, Text, UI, Video, Weather
│  ├─ Editor/                    Panels + Infrastructure
│  ├─ Apps/                      Editor, Sandbox, Server and Lobby executables
│  ├─ Tests/                     plain CTest executables
│  ├─ Tools/                     shader/material/text/audio cookers, AOT compiler, dev gates
│  └─ Plugins/                   example plugins, C-ABI fixtures and storefront providers
├─ Documentation/                MD/DOCX/PDF/DjVu editions, design notes, generators
├─ assets/                       source assets and cooked samples
└─ third_party/                  vendored dependencies
```

Every source file is PascalCase. Inputs are `In*`, outputs `Out*`, locals
`Scoped*`, members `M*`. Braces follow Allman; `if`/`for` bodies are always
braced; comments are English-only, sit above the block they describe and contain
no ASCII-art banners. `.clang-format` owns the layout, `.clang-tidy` mirrors the
naming and brace rules, and `Source/Tools/Dev/CheckStyle.py` binds both to CI.

---

## License

FemboyEngine2D is released under the **Mozilla Public License 2.0**. See
[LICENSE](LICENSE). Third-party notices are in `THIRD_PARTY_NOTICES.md` where
present.

---

## Contributing and security

- [CONTRIBUTING.md](.github/CONTRIBUTING.md)
- [SECURITY.md](.github/SECURITY.md)
- [CODE_OF_CONDUCT.md](.github/CODE_OF_CONDUCT.md)

---

## Hardware

These are development-machine targets for the editor and a debug/release build.
They are not runtime requirements for a shipped 2D game: a finished game can be
much lighter than the tools that produced it. The measured editor table and the
probe methodology are in
[system requirements](Documentation/design/system-requirements.md).

| Component | Small 2D game (minimum) | Medium 2D game (recommended) |
|---|---|---|
| CPU | 4 cores / 8 threads, e.g. Ryzen 3 3100 or Core i3-10100 | 8 cores / 16 threads, e.g. Ryzen 7 5800X or Core i7-12700 |
| RAM | 8 GB absolute minimum, 16 GB comfortable | 16 GB (32 GB for large projects) |
| GPU | Vulkan 1.4-capable driver or Metal 2, 2 GB VRAM, e.g. GTX 1050, RX 560, Iris Xe | Vulkan 1.4 or Metal 3, 6 GB VRAM, e.g. RTX 3060, RX 6600, Apple M1 Pro |
| Storage | 10 GB free on an SSD | 25 GB free on NVMe |
| Display | 1920x1080 | 2560x1440 |
| OS | Windows 10 22H2+/11, Ubuntu 22.04+/24.04, macOS 14+ | Windows 11, Ubuntu 24.04, macOS 14+ on Apple Silicon |

The minimum column is for a small project: a few hundred sprites, one tilemap,
modest audio and a single viewport. The recommended column is for a medium 2D
project: large tilemaps, thousands of sprites, SDF lighting, particles, voice or
multiplayer PIE, and a 1440p editor. A discrete GPU is strongly preferred over
an integrated one for the editor; 8 GB of RAM runs the editor, but 16 GB is the
practical floor once a browser, IDE and debugger are open. The build tree itself
needs roughly 2-6 GB depending on configuration and debug symbols.

The build uses Vulkan-Headers **1.4.366**. The RHI requests the highest Vulkan
version the loader offers, capped at 1.4, and enables 1.2/1.3 features only when
the device exposes them; **Vulkan 1.4 is the minimum supported driver**. A
machine at or above the recommended column runs the editor and the full CTest
suite without hardware-specific workarounds. The 0.4 alpha smoke run on an
RTX 4060 Laptop (Vulkan 1.4.341) executed the mesh/SDF path at 2560x1600 with
three frames in flight. Vulkan is used on Windows/Linux; Metal is used on macOS;
the null RHI keeps headless CI machines usable.

---

[Русский перевод](READMEs/README_RU.md)
