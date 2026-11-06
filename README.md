# FemboyEngine2D

[English](README.md) | [Русский перевод](README_RU.md)

**A data-oriented 2D game engine written from scratch in C++26.**

[![License: MPL 2.0](https://img.shields.io/badge/License-MPL_2.0-brightgreen.svg)](LICENSE)
[![C++26](https://img.shields.io/badge/C%2B%2B-26-blue.svg)]()
[![CMake 3.28+](https://img.shields.io/badge/CMake-3.28%2B-064F8C.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS%20%7C%20Android-lightgrey.svg)]()
[![Status: 0.3.0-alpha.1](https://img.shields.io/badge/status-0.3.0--alpha.1-blue.svg)]()
[![Website](https://img.shields.io/badge/website-FemboyEngine2D-blue.svg)](https://razraboychik.github.io/FemboyEngine-2D/)

FemboyEngine2D is a compact, deterministic, data-oriented engine for 2D games:
a hand-written Vulkan/Metal RHI, an EnTT-based ECS, a declarative parallel
`SystemGraph`, Box2D v3 physics, rollback networking, a transactional asset
pipeline, a full editor and a clean-slate project generator.

The runtime has no `std::function` on the frame path, no allocation on the frame
path, no JSON in the shipped runtime and no hidden object graph. Components are
trivially copyable data; systems are free functions; the frame is a flat
`RenderItem` array submitted through one RHI packet.

**Status: 0.3.0-alpha.1.** This is the current public alpha line. The tree
includes a working FemboyScript debugger (source
spans, breakpoints, stepping, locals, self fields), isometric and tile-based maps
with a map editor, skeletal IK, retargeting and clip compression (`.fanim`), grid
navigation, 2D lighting, a UDP transport with reliability and MTU fragmentation,
live spatial audio, texture thumbnails in the editor and import backpressure.
Public CMake targets and C++ namespaces are PascalCase
(`FemboyEngine2D::Engine`, `Fby::Ecs`, `Fby::Rhi`); no legacy compatibility
aliases remain. The current gate passes the full CTest suite with Clang 23.1.4
on Windows and Clang on Linux; MSVC is not a supported compiler. The style gate
passes, and a generated Clean Slate project builds and launches with the null RHI.
The current line also includes hybrid SDF destruction, transport groups for
elevators, trains and moving platforms, FSR/DLSS-class upscaling and frame
generation, and voice effects with conditions. The engine is strictly 2D: there
is no 3D renderer, 3D physics or 3D scene format.

---

## Clean Slate projects

A new project is deliberately empty. `CreateProject()` writes only:

```text
<Name>/
├─ CMakeLists.txt          links FemboyEngine2D::Engine
├─ GameMain.cpp            window -> RHI -> registry -> fixed step -> extract
├─ project.femproj         project descriptor (paths, simulation, render, build)
├─ assets/
│  └─ scenes/main.toml
├─ build/
├─ archives/
└─ stores/
```

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

| Domain | Highlights |
|---|---|
| Core | `Definitions.hpp`, `FemCast<>`, checked `Cast<>`, `FORCEINLINE`, `Vector2D`, `Vector3D`, `Vector4D`, Mirror reflection, jobs, SDL3 platform, plugin ABI |
| RHI | Persistent frame rings, 48-byte `RenderItem`, 64-bit keys, Vulkan indirect MDI, one compute dispatch for LBS, Metal and null backends, FSR/DLSS-class upscaling and frame generation contracts |
| ECS | EnTT registry, trivially copyable components, four-phase schedule, parallel `SystemGraph` with per-call semaphores, Snapshot v4 |
| Input | Fixed-capacity `InputSystem` actions/contexts/modifiers/triggers, per-user action state, event ticks/timestamps, SDL3 gamepad hotplug and dead zones; no wheel input |
| Gameplay | Project tag database edited in Project Settings, manually authored hierarchical `GameplayTags`, attribute registry, data-driven instant/duration/periodic effects with stacks, skills with cost/cast/cooldown |
| AI | `StateTree` → command buffer → `AIController` → perception, navigation and skills; `ActionId` is data for game-owned ECS systems, tag-driven targets, grid and convex navmesh, raw/LOS/Chaikin/Catmull-Rom paths, deterministic route randomization |
| Physics | Two backends under one interface: Box2D v3 for joints, CCD and general rigid bodies, and a data-oriented AABB solver for large joint-free strategy worlds. Per-level gravity, attractor/repulsor fields, rope/chain/spring joints, damage and cuts, Verlet cloth, shallow-water fields, SDF collision/destruction, transport groups, and configurable hitscan/swept/dynamic projectiles |
| Network | `NetRollbackSession`, 256-tick snapshot ring, input prediction, late-input replay, stable `NetEntityId`, relevance filtering, UDP `NetTransport` with acks, retransmit and MTU fragmentation, and versioned `InputPacket`s that feed `SubmitRemoteInput` |
| Assets | VFS with owning `Pin()`, `.femeta` transactions, BC7/ASTC texture cooker, audio/localization cookers, watcher, `.fanim` compressed clips |
| Distribution | `FemArchiver` `.femarc`, optional LZMA, AES-256-GCM / ChaCha20-Poly1305, `fby_fempak secure` |
| Platform | C-ABI `PlatformService`, Steam/EGS providers, deterministic achievements/cloud saves |
| Animation | 2D skeleton SoA, LBS, blend cycles, `AnimGraph`, animation timeline, PIE seqlock hand-off, two-bone/FABRIK IK, retargeting, pose and clip quantization |
| Isometry | `Grid::ProjectionMode` side/top-down/iso-rect/iso-hex, cell/world conversion, depth keys, `TilemapComponent` with a culled render bridge and a tilemap editor |
| Navigation | Cost-grid A* on rect and hex layouts, tilemap-derived maps, authored `NavAgentSettings` separated from runtime paths, per-frame repath budget |
| Lighting | `Light2DComponent` radial lights, ambient, emissive targets, optional Reinhard tonemap, screen-space SDF shadow mask (render-graph compute pass, static `SdfField` + dynamic `Light2DOccluderComponent` casters), sampled per pixel |
| Audio | DSP graph with buses/ducking/reverb, zones, limiter, Ogg/Opus, live spatial gain and pan, voice capture/AEC/RNNoise and uplink/downlink effects with conditions |
| UI | Procedural PinkUI SDF primitives, DPI scaling, themes, hypertext, a widget kit (border, slider, checkbox, toggle, radio, scrollbar, switcher, overlay, tooltip, editable text input, text block, nine-slice, custom painters, text buttons), Unicode line/word/grapheme boundaries through libunibreak, opt-in Tab/arrow focus with an optional ring, inherited visibility, CPU clip rects, a host `TextRuntime` with a HarfBuzz `TextFontStack` in the apps, shaped line-window viewports, accessibility kernel |
| Editor | Transactional `CommandStack`, ImNodes canvases, project manager, PIE Play/Pause/Stop, Play (Multiplayer) with a second client-world viewport, script debugger, tilemap editor, texture thumbnails and the Transport Groups panel; starts borderless fullscreen (Windowed / Borderless / Exclusive in Editor Settings) |
| Script | LogicGraph to bytecode VM and LogicGraph to C++26 AOT plugin, physics backend/capability nodes and natives, debug info (spans, locals), VM breakpoints and stepping; `Compile + Nativize` and an AOT manifest cook gate so a build runs native modules only (`FBY_WITH_SCRIPT_VM=OFF`) |

---

## Physics backends and projectiles

`PhysicsDesc::backend` selects `Auto`, `Box2D` or `Dod`. `Auto` keeps Box2D
unless the project body budget is at least 4096 or deterministic mode is
requested; joints always force Box2D. The same choice is available in
**Project Settings > Physics**, from project code through
`PhysicsSystem::SelectBackend` / `SetProjectPolicy`, and from FemboyScript
(`physics_get_backend`, `physics_set_backend`, `physics_get_capabilities`).

The data-oriented backend stores bodies in SoA arrays, uses a uniform-grid
broadphase and handles circles/AABB/segments without joints or CCD. The Box2D
backend remains the general-purpose choice. Both share the ECS components,
segment queries and contact event shape, so gameplay code does not branch on the
backend.

`Projectile2D` has `Auto`, `Hitscan`, `Swept` and `Dynamic` modes. `Auto`
promotes a projectile to a continuous query when its per-step displacement
exceeds half of `MinWallThickness`; developers can force a mode, speed, radius or
substep budget. Hitscan is the right default for bullets, swept is for rockets
and arrows, and dynamic keeps a regular CCD body. This prevents tunneling
through thin walls without paying for CCD on every projectile.

## Camera motion and distortion

The camera stack keeps an unshaken **base** pose for gameplay and a **resolved**
pose for rendering. Trauma shake uses seeded value noise and scales with the
accessibility motion setting. Scripts compose generic effects rather than a
named gameplay state: double vision, anisotropic stretch, radial warp, chromatic
split and low-frequency wobble are available through
`camera_distort(flags, strength, paramX, paramY, duration)` alongside
`camera_add_trauma`, `camera_offset`, `camera_zoom`, `camera_roll`,
`camera_clear_effects` and `camera_set_motion_scale`. LogicGraph has matching
nodes. See [camera effects](Documentation/design/camera-effects.md).

## Transport physics

Transport is a moving-reference-frame system: `TransportPath` and
`TransportMotion` drive a carrier by fixed-step distance, `TransportSurface`
provides friction and conveyor velocity, and `CarrierMemberComponent` modes
cover rigid cargo, carried bodies, contact support and conveyors.
`Support` and `Conveyor` use AABB support/tangent logic shared by Box2D and DOD;
the DOD backend reports that welded trains and CCD require Box2D. See
[transport physics](Documentation/design/transport-physics.md).

## Vehicle wheels

`WheelSystem` provides Box2D wheel-joint suspension, per-wheel drive and braking.
`Vehicle2D` owns throttle/brake/limits; `Wheel2D` owns the chassis anchor,
suspension and per-wheel drive/brake weights. Steering geometry, engine RPM,
gears and tire slip stay on the game side; an opt-in arcade yaw assist is
available. DOD has no wheel joints. See
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
SetupScripts/Windows/SetupUpscalers.bat
```

```bash
./SetupScripts/Linux/Setup.sh          # Ubuntu/Debian, everything
./SetupScripts/Linux/SetupCpp.sh       # compiler, CMake, source dependencies
./SetupScripts/Linux/SetupArchiver.sh  # 7-Zip
./SetupScripts/Linux/SetupUpscalers.sh # optional DLSS/FidelityFX/XeSS SDKs
./SetupScripts/macOS/Setup.command     # macOS, also works from Finder
./SetupScripts/macOS/SetupCpp.command
./SetupScripts/macOS/SetupArchiver.command
./SetupScripts/macOS/SetupUpscalers.command
```

Versions are fixed at SDL3 `release-3.4.18`, Vulkan-Headers `v1.4.366`, glslang
`15.3.0`, miniaudio `0.11.23`, Box2D `v3.2.0`, libunibreak `6.1` and LZMA SDK
`25.01`. Vendor
upscaler SDKs are optional and are not part of the archive; see
[Frame Generation](Documentation/md/en/FemboyEngineFrameGeneration_EN.md).

---

## Build

Requirements: CMake 3.28+, Ninja, Clang 19+ (clang-cl on Windows, clang++ on
Unix) or GCC 16+. On Windows, run from a shell that provides the Windows SDK
headers and libraries; the MSVC compiler itself is not a supported toolchain.
The setup scripts install both compiler families: clang-cl / clang++ for the
validated build and GCC 16+ (MinGW-w64 UCRT on Windows, Homebrew or the
distribution package elsewhere) for the second-opinion toolchain. On Windows,
`SetupScripts/Windows/SetupCpp.bat` downloads the official **LLVM 23.1.4** MSI and verifies the
`clang-cl` version. The Unix scripts install the distribution Clang (19+); CI
pins LLVM 23.1.4 for the authoritative Windows gate.

The validated build uses **clang-cl 23.1.4 on Windows and clang++ elsewhere**;
that is what CI and the release gate run. GCC 16.2.0 (MinGW-W64 UCRT) is
installed by `SetupScripts/Windows/SetupCpp.bat` and recorded in the dependency lock; a `-std=c++2c`
smoke program and an engine header syntax check pass with it, but the full gate
has not been run with GCC yet. The honest status for this release is therefore
`GCC not run` for the full build, until a Linux GCC runner joins CI.

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
the Linux and macOS CI jobs are advisory until their first hosted run is green.

The per-platform status, seams and cook flow are tracked in
[Platform support](Documentation/design/platform-support.md).

Main executables:

```text
build/bin/fby_sandbox
build/bin/fby_editor
build/bin/tools/fby_fempak
build/bin/tools/fby_scene_tool
build/bin/tools/fby_aotc
```

Optional vendor upscalers are opt-in: DLSS and FidelityFX SDKs are not committed.
Install the SDK locally and pass `-DFBY_WITH_DLSS=ON -DFBY_DLSS_ROOT=<path>` or
`-DFBY_WITH_FIDELITYFX=ON -DFBY_FIDELITYFX_ROOT=<path>`; portable FSR1/FSR2 and
custom frame generation do not require either SDK. See
[Frame Generation](Documentation/md/en/FemboyEngineFrameGeneration_EN.md).

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

The style gate is machine-checked:

```bash
python Source/Tools/Dev/CheckStyle.py
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
is [README_RU.md](README_RU.md).

- [Architecture specification (EN)](Documentation/md/en/FemboyEngineArchitecture_EN.md)
- [Architecture specification (RU)](Documentation/md/ru/FemboyEngineArchitecture_RU.md)
- [Application guide (EN)](Documentation/md/en/FemboyEngineApplicationGuide_EN.md)
- [Application guide (RU)](Documentation/md/ru/FemboyEngineApplicationGuide_RU.md)
- [API reference (EN)](Documentation/md/en/FemboyEngineAPIReference_EN.md)
- [API reference (RU)](Documentation/md/ru/FemboyEngineAPIReference_RU.md)
- [Complete reference manual (EN)](Documentation/md/en/FemboyEngineCompleteReference_EN.md)
- [Complete reference manual (RU)](Documentation/md/ru/FemboyEngineCompleteReference_RU.md)
- [CMake project and engine management (EN)](Documentation/md/en/CMakeProjectManagement_EN.md)
- [CMake project and engine management (RU)](Documentation/md/ru/CMakeProjectManagement_RU.md)
- [FemboyScript manual (EN)](Documentation/md/en/FemboyScript_EN.md)
- [FemboyScript manual (RU)](Documentation/md/ru/FemboyScript_RU.md)
- [Physics, SDF destruction and transport groups (EN)](Documentation/md/en/Physics_EN.md)
- [Physics, SDF destruction and transport groups (RU)](Documentation/md/ru/Physics_RU.md)
- [Frame generation and upscaling (EN)](Documentation/md/en/FemboyEngineFrameGeneration_EN.md)
- [Frame generation and upscaling (RU)](Documentation/md/ru/FemboyEngineFrameGeneration_RU.md)
- [Viewport display modes (EN)](Documentation/md/en/ViewportModes_EN.md)
- [Viewport display modes (RU)](Documentation/md/ru/ViewportModes_RU.md)
- [Editor settings reference (EN)](Documentation/md/en/EditorSettingsReference_EN.md)
- [Editor settings reference (RU)](Documentation/md/ru/EditorSettingsReference_RU.md)
- [Project settings recommendations (EN)](Documentation/md/en/ProjectSettingsRecommendations_EN.md)
- [Project settings recommendations (RU)](Documentation/md/ru/ProjectSettingsRecommendations_RU.md)
- [Code style](Documentation/style/CodeStyle.md)
- [Naming conventions](Documentation/style/NAMING.md)
- [Roadmap](Documentation/ROADMAP.md)
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
├─ Source/
│  ├─ Engine/                    Core, RHI, ECS, Physics, Audio, Network,
│  │                             Assets, Script, UI, Video, Weather
│  ├─ Editor/                    Panels + Infrastructure
│  ├─ Apps/                      Editor + Sandbox executables
│  ├─ Tests/                     plain CTest executables
│  ├─ Tools/                     shader compiler, cooker, AOT, docs tools
│  └─ Plugins/                   C-ABI fixtures and storefront providers
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

The 0.2 alpha line is the current feature batch on top of the first public
baseline. It will develop with community feedback; 1.0 is reserved for the
own-library milestone.

---

## Hardware

These are development-machine targets for the editor and a debug/release build.
They are not runtime requirements for a shipped 2D game: a finished game can be
much lighter than the tools that produced it.

| Component | Small 2D game (minimum) | Medium 2D game (recommended) |
|---|---|---|
| CPU | 4 cores / 8 threads, e.g. Ryzen 3 3100 or Core i3-10100 | 8 cores / 16 threads, e.g. Ryzen 7 5800X or Core i7-12700 |
| RAM | 8 GB absolute minimum, 16 GB comfortable | 32 GB |
| GPU | Vulkan 1.4-capable driver or Metal 2, 2 GB VRAM, e.g. GTX 1050, RX 560, Iris Xe | Vulkan 1.4 or Metal 3, 6-8 GB VRAM, e.g. RTX 3060, RX 6600, Apple M1 Pro |
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
suite without hardware-specific workarounds. Vulkan is used on Windows/Linux;
Metal is used on macOS; the null RHI keeps headless CI machines usable.

---

[Русский перевод](README_RU.md)
