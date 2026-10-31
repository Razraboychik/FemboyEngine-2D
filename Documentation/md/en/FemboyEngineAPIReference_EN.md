# FemboyEngine2D API Reference Manual

**C++26 data-oriented engine - complete public surface, contracts and a build-from-zero walkthrough**


Version 0.1.0-alpha.1 (Alpha) · 2026-10-31 · English


---


## Table of contents


1. Scope, conformance and engine rules

2. Building, targets and compiler flags

3. Core value types, FORCEINLINE and the cast family

4. FrameArena: the only frame allocator

5. JobSystem and per-thread diagnostics

6. ECS, scheduling and snapshots

7. RHI, render items and frame statistics

8. Materials: cooked graphs, the pipeline cache and the shipped path

9. Physics: Box2D v3, joints, cloth and SdfCollisionSystem

10. NetRollbackSession: deterministic rollback

11. Audio: mixer, DSP graph, limiter

12. PinkUI and WidgetCanvasSystem

13. Accessibility kernel (BlindMode, DeafMode, FlashGuard)

14. Asset pipeline, VFS and localization

15. Platform services: Steam, Epic Games Store, GOG

16. Editor: dashboard, fonts, LogBoy, Scene Diagnostics

17. Step-by-step: a game from an empty folder

18. Testing, style gate and release checklist

19. Known gaps and recommended next work

20. Appendix A - generated symbol index

21. Appendix B - macros and platform defines



## 1. Scope, conformance and engine rules

This manual specifies the public surface of FemboyEngine2D: every header under Source/Engine and the editor infrastructure that a host links against. It is written against the release-candidate tree, so a symbol described here exists in the headers that ship with that tree, and the appendix index is generated from those headers on every run.

The engine targets C++26 with GCC 15 or newer and Clang 19 or newer; the Windows release candidate is built with clang-cl against the MSVC standard library. Four rules are load bearing and every public API is designed around them:

- Simulation state is plain, trivially copyable data. No virtual calls, no owning pointers and no std::string belong in a component, snapshot or network payload.
- Hot paths do not allocate. Frame scratch memory comes from FrameArena, logs go through fixed char buffers, and callbacks are function pointers plus void*, never std::function.
- Determinism is a property of the data, not of the scheduler. Every subsystem exposes a checksummed image, and NetRollbackSession can only replay what it can reproduce byte for byte.
- No RTTI in engine code: dynamic_cast and typeid are rejected by the style gate. Cast<> walks stable type ids instead (see section 3).

> The style gate (Source/Tools/Dev/CheckStyle.py) enforces the naming and structure rules: PascalCase file stems, Allman braces, braced control bodies, In-prefixed parameters, Scoped-prefixed locals, no trailing line comments and no Cyrillic in engine sources.


## 2. Building, targets and compiler flags

One CMake tree produces three kinds of artefact: the engine static library (FemboyEngine2D::Engine), the editor library and application (FemboyEngine2D::Editor), and the offline tools. A shipped game links the engine only; Dear ImGui, ImGuizmo, ImPlot and the TOML writer never enter a shipping target.

| CMake option | Default | Effect |
|---|---|---|
| FBY_BUILD_EDITOR | ON | Builds fby_editor, the editor app and the panel plugins. |
| FBY_BUILD_TESTS | OFF | Builds the ctest suite under Source/Tests. |
| FBY_BUILD_TOOLS | ON | FempakCooker, LocTool, SceneTool, ShaderCompiler, AotCompiler. |
| FBY_BUILD_PLATFORM_PROVIDERS | ON | Steam and Epic Games Store provider plugins. |
| FBY_RHI_BACKEND | auto | auto, vulkan, metal or null. auto selects per platform. |

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DFBY_BUILD_TESTS=ON -DFBY_BUILD_EDITOR=ON
cmake --build build --target fby_editor
ctest --test-dir build --output-on-failure
python Source/Tools/Dev/CheckStyle.py
```

The compiler wrapper (cmake/FbyCompiler.cmake) owns the C++26 flag, the warning set and the sanitizer switches. Do not add flags per target: a target that needs a different dialect is a target that should not exist.


## 3. Core value types, FORCEINLINE and the cast family

Definitions.hpp is the contract header of the engine. It supplies the export macros, the assertion macros (FBY_ASSERT, FBY_VERIFY, FBY_UNREACHABLE), the stable TypeId machinery, the cast family and the component registration macros. Every other header includes it, directly or transitively.

```cpp
#if defined(_MSC_VER)
#define FORCEINLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define FORCEINLINE inline __attribute__((always_inline))
#else
#define FORCEINLINE inline
#endif
```

FORCEINLINE is the only inlining spelling in public headers. It is used for accessors, hot arithmetic and template casts; it never replaces a measured optimisation. The three families are:

| Symbol | Contract |
|---|---|
| FemCast<To>(value) | Unchecked static_cast with reference collapsing. Use when the concrete type is proven by construction (factory output, storage tag). No runtime cost. |
| Cast<To>(object) | Checked cast. Upcast is a compile-time static_cast; downcast compares the object's GetTypeId() against StableTypeId<To>() and its registered parent chain; unrelated types fail to compile. Returns nullptr on a mismatch, never throws, never uses RTTI. |
| CastComponent<To>(registry, entity) | ECS spelling of Cast<>: EnTT proves the component type at compile time and the registry validates the entity. Returns nullptr when the entity lacks the component. |

```cpp
// Downcast through the stable type table, not through RTTI.
Fby::TypeObject* base = factory.Create("Sprite");
if (Fby::Sprite* sprite = Fby::Cast<Fby::Sprite>(base))
{
    sprite->Animate(dt);
}

// Registration binds Sprite to TypeObject and derives its stable id.
FBY_REGISTER_TYPE(Fby::Sprite, Fby::TypeObject);
```

StableTypeId<T>() is a consteval FNV-1a of the type's registered name (or the compiler signature when no name is pinned). A type that changes its registered name changes its id, which is why component names are part of the serialized schema and why Snapshot v4 stores a LayoutHash next to the name.


## 4. FrameArena: the only frame allocator

FrameArena is a bump pointer over caller-owned storage. It is reset at the start of a frame and never frees individual allocations. Systems that need transient arrays take it from SystemContext::frameArena; a system that calls operator new in its frame path is a defect, not a style preference.

```cpp
Fby::FrameArena arena(storage);          // caller owns storage
arena.Reset();                          // once per frame
auto* items = arena.AllocateArray<MeshRenderItem>(count);
if (items == nullptr)
{
    // Arena exhausted: degrade, never allocate on the heap.
    return;
}
const std::size_t used = arena.Used();
const std::size_t capacity = arena.Capacity();
```

- Allocate() returns nullptr instead of throwing when the arena is exhausted; callers must handle the failure.
- AllocateArray<T>() static_asserts std::is_trivially_copyable_v<T>: the arena stores POD payloads only.
- Over-aligned types must pass their alignment explicitly; the default is alignof(std::max_align_t).


## 5. JobSystem and per-thread diagnostics

JobSystem owns a pool of std::jthread workers. Tasks are function pointers plus a context pointer plus a half-open range, so submitting a task never allocates. ParallelFor splits a range into chunks, lets every participant pull the next chunk from an atomic counter, and always runs at least one chunk on the caller, which keeps single-core machines correct and worker re-entry safe.

```cpp
jobs.ParallelFor(entityCount, 4096, [&](std::uint32_t begin, std::uint32_t end)
{
    for (std::uint32_t i = begin; i < end; ++i)
    {
        Integrate(entities[i], dt);
    }
});
jobs.WaitIdle();

// Diagnostics: one row per thread, slot 0 is the main thread.
std::array<Fby::JobSystem::ThreadLoad, Fby::JobSystem::kMaxThreadSlots> loads{};
const std::uint32_t slots = jobs.SampleThreadLoad(loads);
```

ThreadLoad carries busyNanoseconds (wall time inside engine work), taskCount and a 16-byte name. The counters are one cache line per thread, so two workers finishing at the same time do not contend. SceneDiagnosticsPanel turns two samples into a per-thread occupancy percentage.

> ThreadSlotCount() is ThreadCount() + 1 and is clamped by the fixed MCounters array; a device with more than 64 workers reports the first 64 slots.


## 6. ECS, scheduling and snapshots

The registry is EnTT. Systems are registered on a SystemGraph in one of five phases (PreUpdate, FixedUpdate, Update, PostUpdate, Render). SystemContext is the only parameter a system receives; it carries the registry, JobSystem, frame arena, renderer, window, VFS, AssetDatabase, audio, physics, joints, cloth, fluid, camera stack, accessibility kernel and font atlases.

```cpp
Fby::Ecs::Schedule schedule;
schedule.Add(Fby::Ecs::Phase::FixedUpdate, "physics", &PhysicsStepSystem, &physics);
schedule.Add(Fby::Ecs::Phase::Update, "movement", &MovementSystem, nullptr);
schedule.Add(Fby::Ecs::Phase::PostUpdate, "platform", &GogAchievementSystem, &gog);

Fby::Ecs::SystemContext context{registry, jobs};
context.physics = &physics;
context.window = &window;
schedule.Run(Fby::Ecs::Phase::FixedUpdate, context);
```

Snapshot v4 is a canonical, checksummed byte image of the registry. Components are written by hand-written codecs when they contain runtime handles (b2BodyId, b2ShapeId); the snapshot never serializes those handles, and the restore path recreates bodies from the component fields. The schema stores the Mirror name and LayoutHash of every component, so a field change is rejected instead of silently misread.

| API | Contract |
|---|---|
| GetOrAdd<T>(registry, entity) | Adds the component when missing and returns a stable reference. |
| ecs::SnapshotWriter / SnapshotReader | Writes and validates the canonical image; checksum is FNV-1a over the payload. |
| Fby::Net::NetRollbackSession | Consumes snapshot v4 and replays ticks on late input (section 10). |


## 7. RHI, render items and frame statistics

The RHI is a hand-written Vulkan/Metal layer. Frames are recorded into persistent ring buffers; sprite items are 48-byte PODs; meshes are drawn through one indirect call per material/layer; linear-blend skinning is one compute dispatch. FrameStats exposes what the pass actually submitted, which is what SceneDiagnosticsPanel plots.

| FrameStats field | Meaning |
|---|---|
| items / batches / drawCalls | Sprite items, batches and submitted draws. |
| meshItems / meshIndirectCalls | MeshRenderItem count and indirect draws. |
| skinDispatches / skinRanges | Skinning compute dispatches and ranges. |
| uploadedBytes | Bytes uploaded this frame. |

> The editor's Viewport renders into an offscreen target, not into the swapchain. EnsureViewportTarget() sizes that target in physical pixels; a target sized in points is the usual cause of a blurry Viewport on a HiDPI monitor.


## 8. Materials: cooked graphs, the pipeline cache and the shipped path

Material authoring and material compilation live on opposite sides of the engine boundary. `Render::MaterialGraph` is the authoring DAG: fixed capacities (128 nodes, 384 pins, 64 parameters), trivially copyable, and hashed without the editor positions. `Render::MaterialGraphCompiler` turns a validated graph into GLSL 450 text. `fby_matc` runs that compiler and glslang at build time and writes a `.femmat` payload, so a shipped game never links glslang and never parses GLSL. The full rationale is in [ADR 0002](../../adr/0002-materials-in-engine.md).

| Piece | Header | Contract |
|---|---|---|
| `Render::MaterialGraph` | `Engine/RHI/MaterialGraph.hpp` | Authoring DAG; `Hash()` is the pipeline identity and excludes UI positions |
| `MaterialGraphCompiler` | `Engine/RHI/MaterialGraphCompiler.hpp` | DAG to GLSL 450 and the shader contract flags |
| `EncodeMaterialGraph` / `DecodeMaterialGraph` | `Engine/RHI/MaterialAsset.hpp` | `.fgraph` wire format: explicit little-endian fields, no struct memcpy |
| `EncodeCookedMaterial` / `DecodeCookedMaterial` | `Engine/RHI/MaterialAsset.hpp` | `.femmat` payload: 32-byte header, vertex and fragment SPIR-V, zero-copy decoding |
| `Render::MaterialPipelineCache` | `Engine/RHI/MaterialPipelineCache.hpp` | 128 pipelines keyed by graph hash, reference counted, LRU eviction, no heap use |
| `Rhi::RHI::SupportsRuntimeShaderPipelines` | `Engine/RHI/RHI.hpp` | Capability query: Vulkan returns true, Null and Metal return false |

```bash
# Author the graph once, cook it in the build step
build/bin/fby_matc assets/materials/tinted.fgraph -o assets/materials/tinted.femmat

```

```cpp
// `InBytes` is the .femmat payload from the VFS or a package entry.
Render::CookedMaterialView ScopedMaterial;
if (!Render::DecodeCookedMaterial(InBytes, &ScopedMaterial))
{
    return false;
}

// One cache per device: acquire at load time, release at shutdown.
const Rhi::ShaderPipelineHandle ScopedPipeline = InMaterials.Acquire(InRhi, ScopedMaterial);

Rhi::MeshRenderItem ScopedItem;
ScopedItem.materialPipeline =
    ScopedPipeline.IsValid() ? ScopedPipeline.id : Rhi::kDefaultMaterialPipeline;

```

- The graph hash is the pipeline identity. Editing a graph produces a new hash, so the old pipeline becomes an unreferenced cache entry instead of being mutated in place.
- The contract mask travels with the payload: generated vertex stage, fragment view projection, material push constants, world and vertex position varyings. A live entry whose hash arrives with another contract is refused, because that is a different shader.
- `Acquire()` adds a reference and `Release()` drops one. A pipeline at zero references stays resident, so a material acquired once per frame never recreates a pipeline; it is destroyed by LRU eviction, `ReleaseAll()` or `Shutdown()`.
- The cache never allocates and never formats text on the frame path; `LastError()` returns the fixed error buffer.
- Backends without runtime shader pipelines keep the engine shaders. Query `SupportsRuntimeShaderPipelines()` instead of probing for an invalid handle.

| Payload | Fields | Validated on load |
|---|---|---|
| `.fgraph` | magic `FBYG`, version, node/pin/param counts, nodes, pins, params | magic, version, counts against capacity, exact size, node kinds, pin and param indices |
| `.femmat` | magic `FBYM`, version, contract mask, graph hash, vertex and fragment byte lengths | magic, version, size against the lengths, both SPIR-V modules, contract consistency |

The editor keeps live editing on top of the same cache: `Fby::Editor::MaterialGraphRuntime` recompiles a graph when its hash changes, reports per-tab errors and delegates pipeline ownership to `MaterialPipelineCache`. `MaterialGraphRuntime::SetDomain` switches the live compile between the mesh and the sprite contract, and the panel's `SaveGraph`/`LoadGraph` write and read the `.fgraph` payload the cooker consumes. A shipped build skips glslang entirely and only calls `DecodeCookedMaterial` and `Acquire`.

Sprites take materials through the third artefact: `MaterialCompileOptions::domain` selects the sprite domain, where the generated fragment stage matches the `quad.vert` varyings (`vUv`, `vColor`, `vLayer`, and `vWorld` when a node needs it) and samples the item's atlas slice through `vLayer`. `Time` is available: the sprite push block carries a material clock at offset 80, so the generated stage reads `pc.materialParams.x` exactly like a mesh shader. `fby_matc --sprite` cooks that variant. `DrawOrder::MaterialBucket` places the sprite in a bucket, `ChangeMaskRenderExtract::SetMaterialPipelines` maps buckets to cached pipeline ids, and the extractor emits one `DrawBatch` per bucket with `DrawBatch::pipeline` already resolved. The 64-bit render key carries the bucket (16 layer, 8 material, 32 depth, 8 sequence), so a material batch is never split by depth. Nodes that need the mesh view-projection (`Fresnel`, `VertexPosition`) are refused in the sprite domain with a readable reason.


## 9. Physics: Box2D v3, joints, cloth and SdfCollisionSystem

PhysicsSystem owns one b2WorldId and exposes it to extension modules as a packed uint32, so box2d.h never leaks into a public header. Bodies are mirrored from RigidBody2D/Collider2D components into Box2D, stepped at the fixed rate, and read back before interpolation. PhysicsJointSystem adds rope and revolute-link chains; ClothSystem integrates a Verlet grid and projects it against the same Box2D colliders.

SdfCollisionSystem contract:

| Member | Contract |
|---|---|
| SetField(const SdfField&) | Installs a non-owning mdspan over a row-major grid. Negative values are solid; the field's memory is owned by the renderer/terrain, not by the system. |
| Step(registry, physics, fixedDt) | Samples the grid at body corners and centre, converts the gradient to a normal, applies a mass-scaled push impulse, reflects inward velocity and writes the corrected state back. Returns the number of corrected bodies. |
| Update(SystemContext&) | ECS convenience wrapper around Step(); a headless call without physics is a no-op. |
| Contacts() | Fixed array of SdfContact {entity, point, normal, penetration, impulse, sequence}. |
| SdfCollisionDesc | pushOut, restitution, maxImpulse, subSamples, dynamicOnly. |

```cpp
Fby::SdfCollisionDesc desc;
desc.pushOut = 0.65f;
desc.restitution = 0.05f;
desc.subSamples = 2;
sdf.Initialize(desc);
sdf.SetField(terrain.Field());
sdf.Register(graph);                    // Phase::FixedUpdate

// Viewport overlay: red vectors whose length is the penetration.
debug.Build(context, Fby::Editor::PhysicsDebugSdf, &sdf);
```

> SdfCollisionSystem is not part of SystemContext. A host that wants SDF physics registers the system explicitly through Register(graph) and keeps the instance alive; the Viewport debug pass takes the same pointer.


## 10. NetRollbackSession: deterministic rollback

A session binds snapshot v4, CommandLog, Pcg32 and GameplayState into one fixed-capacity rollback loop. Everything after Initialize() is allocation-free: one snapshot ring, one input ring per player, a fixed entity map and fixed frame metadata. The simulation callback is a function pointer plus void*.

| Method | Contract |
|---|---|
| Initialize(registry, schema, config, arena) | Allocation-free setup with a caller-owned snapshot arena. |
| Initialize(registry, schema, config) | Convenience overload; allocates one arena (the only allocation the session makes). |
| SetSimulation(fn, user) / SetCommandLog(log) | Binds the pure simulation function and the structural command log. |
| Start(rng) | Captures the base tick and stores the session RNG. Required before Step(). |
| Step(localInput) | Simulates exactly one tick with prediction for missing remote inputs and captures the snapshot. |
| SubmitRemoteInput(player, frame) | Stores a late or current input; a frame for an already simulated tick marks a pending rollback. |
| ReplayPending(report) | Restores the snapshot before the earliest corrected tick, restores RNG and CommandLog cursor, re-simulates, captures new snapshots and reports changed ticks. |
| CompareChecksums(tick, snap, state, out) | Compares a remote report with the stored frame; the two checksums isolate a byte-level desync from a gameplay-state desync. |

```cpp
Fby::Net::NetRollbackSession::Config cfg;
cfg.playerCount = 2;
cfg.localPlayer = 0;
cfg.rollbackTicks = 64;
session.Initialize(registry, schema, cfg, arena);
session.SetSimulation(&SimulateTick, &game);
session.Start(pcg);

while (running)
{
    session.SubmitRemoteInput(remote, received);
    session.Step(localInput);
    if (session.RollbackPending())
    {
        Fby::Net::RollbackReport report;
        session.ReplayPending(&report);
        if (report.changedTicks > 0)
        {
            LogBoy().Log(Fby::Editor::LogBoySeverity::Desync, "net",
                         "rollback {}..{} changed {}", report.fromTick, report.toTick,
                         report.changedTicks);
        }
    }
}
```

- NetInputFrame is 24 bytes of integer axes (int16 fixed point). Floats are not allowed: a float bit pattern is not guaranteed to survive a different compiler.
- Result enumerates every failure mode (NotInitialized, BufferFull, RollbackTooOld, ChecksumMismatch, ...); ResultName() is the log spelling.
- A session is non-copyable and owns its arena when the convenience overload is used.


## 11. Audio: mixer, DSP graph, limiter

AudioSystem owns the miniaudio device and a fixed mixer. DspGraph routes nodes into buses, supports ducking and reverb, and receives commands through a lock-free ring so the game thread never blocks the audio callback. Limiter.hpp is the final true-peak stage.

- DeafMode mutes the graph at the Master root and turns sound triggers into subtitle lines; it is a policy of the audio system, not a post-effect.
- Ogg/Opus is decoded off the audio thread; the mixer only sees cooked .fpcm buffers.


## 12. PinkUI and WidgetCanvasSystem

PinkUI is the in-game UI: a fixed-capacity canvas, mdspan grid layout, anchor resolution, hit testing and one-batch packing. WidgetCanvasSystem turns a WidgetCanvas into FrameOutput (draw items) without allocating; font glyphs come from a .fbyfont atlas mapped through the VFS, so the shipping runtime carries no TTF rasterizer. Glyphs are atlas-only: there is no embedded 8x8 bitmap fallback, and a text frame without a mounted font array is refused instead of drawn with the wrong layer.

| Member | Contract |
|---|---|
| WidgetCanvasSystem::MountFonts(RHI&) | Mandatory cold-start stage on the main thread. Builds the GPU texture array from the VFS .fbyfont set; a failure is a PANIC log plus FBY_ASSERT_MSG, never a fallback. |
| BuildFrame(context, canvas, output) | Lays out and culls one canvas, returning BuildStatus and filling FrameOutput. A text canvas without a current mounted atlas returns FontsNotMounted and writes nothing. Allocation-free; safe to call from Phase::Render. |
| Update(context, canvases, output) | Builds every active canvas into one output; the host submits the result as one batch. A font-contract violation stops the pass and sets fontContractViolated. |
| WidgetCanvas::SetDisplayScale | Density from CameraStackSystem::DisplayScale; font sizes are rounded to whole physical pixels. |
| OledShiftState | Integer 2 px orbit for OLED safety; ApplyOledShift() binds it to a canvas pool. |

WidgetCanvasSystem is also the accessibility surface: BlindMode reads widget labels for the narrative log, DeafMode receives subtitle widgets, and colour-vision matrices plus FlashGuard are applied to the canvas colours before packing.


## 13. Accessibility kernel (BlindMode, DeafMode, FlashGuard)

Accessibility is a frame policy, not an overlay. BlindMode skips the game pass, indirect draws and the skinning dispatch, then publishes a bounded narrative log. DeafMode mutes the Master bus and routes sound triggers into SubtitleLines. FlashGuard compresses sudden luminance changes. Colour-vision correction uses the published Machado 2009 matrices, and SrgbRelativeLuminance()/ContrastRatio() implement WCAG 2.2 for authoring-time validation.

| Item | Contract |
|---|---|
| NarrativeEvent | 128-slot SPSC ring of 160-byte text payloads; the simulation thread pushes, the speech thread pops. |
| ColorVisionMatrix(deficiency) | Returns the 3x3 matrix for protanopia, deuteranopia or tritanopia. |
| ApplyColorVision(color, matrix, strength) | Applies the matrix in linear light to an sRGB Color32. |
| FlashGuard::Evaluate(luminance, dt) | Returns the gain for the current frame; a large delta is compressed to the threshold. |
| EditorSettings::ide* | The same policies applied to the editor window itself: Machado correction, WCAG high-contrast, FlashGuard, keyboard navigation and LogBoy as the narration sink. |


## 14. Asset pipeline, VFS and localization

Assets are addressed by relative path inside the VFS. A .fempak archive is mounted with a priority; a directory mount can shadow it, which is how the editor writes a loose file before the cooker folds it back in. Read() returns a span into an mmap that stays valid until the mount is dropped or the file is hot-reloaded.

| Format | Owner | Purpose |
|---|---|---|
| .fempak | FempakCooker | Immutable shipped archive with an index and optional encryption. |
| .fbyfont | FontAtlas (PinkUI) | Baked glyph atlas: [header][glyph records][pixels]; no runtime TTF rasterizer. |
| .femeta | AssetDatabase | Typed metadata, import settings and stable AssetId. |
| .fbin | LocTool | Compiled localization table; the editor UI itself is English-only. |

> The editor UI is not localizable by design: menus, panel titles, errors and the inspector are English-only. Localization applies to game content and documentation, where a .fbin table and the EN/RU manual cover the shipped languages.


## 15. Platform services: Steam, Epic Games Store, GOG

PlatformService is the engine-side bridge; PlatformAPI.h is a flat pure-C ABI that a store SDK plugin implements. The provider is loaded dynamically, resolves only documented entry points and converts every store-specific type at the boundary, so a game never includes steam_api.h, EOSSDK or Galaxy headers.

| Provider | Status in this tree | Notes |
|---|---|---|
| SteamProvider.cpp | Implemented | Steamworks flat API through a function table. |
| EgsProvider.cpp | Implemented | Epic Online Services provider through the same C ABI. |
| GogProvider.cpp | Implemented (shim) | Loads galaxy64.dll / libgalaxy64.so / libgalaxy64.dylib at runtime and resolves the flat fby_gog_* shim ABI; the game ships the shim built against its SDK revision. Galaxy is a C++ interface library, so a shim is mandatory. GogAchievementSystem and GogStorageSystem write the binary PlatformCommandLog in Phase::PostUpdate. |
| fby_fontc | Implemented | Offline TTF -> .fbyfont cooker (stb_truetype, RGBA8 atlas, verified header). |

```cpp
// Pure C ABI: the provider is a shared library, not a C++ dependency.
typedef struct FbyPlatformApi {
    uint32_t version;
    void* user;
    bool (*init)(void* user);
    void (*shutdown)(void* user);
    void (*unlockAchievement)(void* user, const char* id);
    bool (*writeCloudFile)(void* user, const char* name, const uint8_t* data, uint32_t size);
} FbyPlatformApi;
```


## 16. Editor: dashboard, fonts, LogBoy, Scene Diagnostics

The dashboard is the only editor surface that exists before a project is open. It installs the editor font set before the first frame, so the first window is already Inter at the physical DPI. A new project starts in Projects/ under the engine root, resolved by DefaultProjectsRoot() - never in the process working directory and never in the install directory.

| Component | Contract |
|---|---|
| EditorFonts | Loads Inter and JetBrains Mono from the VFS mmap. Subpixel advances (PixelSnapH=false), auto oversampling, no bitmap fallback, dynamic atlas baked at the viewport pixel density. Legacy backends get RasterizerDensity + inverse FontGlobalScale. |
| ProjectDashboard | Initialize(paths, config, visible, window, vfs, fonts, uiScale): installs typography, resolves the default Projects/ location and creates it. |
| LogBoySystem / LogBoyPanel | Fixed 4096-record ring of 192-byte text payloads with severity Trace/Info/Warning/Critical/Desync, FNV-1a system hashes and colour+hash filtering. |
| SceneDiagnosticsPanel | Per-thread occupancy bars and heatmap, FrameArena bytes, MeshRenderItem count, indirect draws, draw calls and exact simulation microseconds with avg/p99/peak. |
| PhysicsDebugVisualizer | Fixed-buffer line/point geometry for Box2D bodies, ropes, cloth and SDF normals, drawn over the Viewport through a caller-supplied world-to-screen map. |

> The .fbyfont atlas is a baked PinkUI asset and cannot feed ImGui's vector rasterizer. The editor therefore loads the Inter TTF from the same VFS mount; the atlas header is still validated on startup, so a corrupt game-UI atlas is reported by the editor, not by the player.


## 17. Step-by-step: a game from an empty folder

This walkthrough produces a playable project with a sprite, physics, keyboard input, a PinkUI HUD and a deterministic rollback session. Every step maps to a real header from the index.

- 1. Launch the editor and create a project with New Project. The dashboard creates <engineRoot>/Projects/<name> with assets/{textures,audio,scenes}, configs/ and project.femproj.
- 2. Import a sprite into assets/textures. The watcher imports it, writes a .femeta and cooks a .ftex texture array layer.
- 3. Create assets/scenes/main.toml with one SpriteRenderer, one Transform2D and one RigidBody2D + Collider2D.
- 4. Register the systems: input in PreUpdate, physics in FixedUpdate, movement in Update, PinkUI in Render.
- 5. Give the camera a CameraStackSystem and call SetDisplayScale() from SDL_GetWindowDisplayScale so UI glyphs are crisp on HiDPI.
- 6. Add an AccessibilityKernel from project settings; BlindMode and DeafMode are read from project.femproj, not hard-coded.
- 7. Create a NetRollbackSession per match, bind a pure SimulateTick callback and feed NetInputFrame values.
- 8. Log through LogBoy: severity + system hash + fixed-format message. Open the LogBoy panel and filter by Desync while testing netcode.
- 9. Enable Physics Debug Draw in the Viewport while tuning colliders and cloth.
- 10. Cook with FempakCooker, run the suite with ctest and the style gate, then ship the engine-only target.

```cpp
// 4-7 in code: systems, accessibility and rollback for one match.
Fby::Ecs::Schedule schedule;
schedule.Add(Fby::Ecs::Phase::PreUpdate, "input", &InputSystem);
schedule.Add(Fby::Ecs::Phase::FixedUpdate, "physics", &PhysicsStepSystem, &physics);
schedule.Add(Fby::Ecs::Phase::Update, "movement", &MovementSystem);
schedule.Add(Fby::Ecs::Phase::Render, "ui", &UiSystem, &ui);

Fby::AccessibilityKernel kernel;
kernel.Configure(settings);              // blind/deaf/colour/flash

Fby::Net::NetRollbackSession session;
if (session.Initialize(registry, schema, cfg, arena) != Fby::Net::NetRollbackSession::Result::Ok)
{
    LogBoy().Log(Fby::Editor::LogBoySeverity::Critical, "net", "rollback init failed");
}
```


## 18. Testing, style gate and release checklist

The suite is deliberately framework-free: every test is an executable with a main() that returns non-zero on failure, so it runs on a headless CI agent, a console devkit and a developer workstation with the same command. The full suite passes at Release.

```bash
ctest --test-dir build --output-on-failure          # the full suite
python Source/Tools/Dev/CheckStyle.py               # style gate
python Documentation/Tools/GenerateAPIReference.py  # this manual
python Documentation/Tools/GenerateDocumentation.py
python Documentation/Tools/GenerateApplicationGuide.py
```

- A release build enables asserts and runs the whole suite once with FBY_ENABLE_ASSERTS=1, because two latent debug-only compile errors existed in JobSystem at RC.
- The POSIX branch of WriteFileAtomically() and LogLine() must be compiled on Linux/macOS in CI; the Windows-only build never saw them.
- The style gate must report zero failures before a commit is eligible for release.


## 19. Known gaps and recommended next work

The live backlog, with file/line evidence and a status per item, is Documentation/ROADMAP.md. The items previously listed here (the GOG shim build step, a canvas mixing text with its own image array, std::function in the window observers/dialogs, BlindMode focused-widget narration, SceneDiagnostics simulation time and fixed-step enforcement) are tracked there; this chapter keeps no second copy.


## 20. Appendix A - generated symbol index

Generated from the headers on every run. A symbol listed here is declared in the shipped tree; the header and line are the authoritative location.


### Editor/Infrastructure (94)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `AIStateTreeCanvasPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 39 |
| `AnimationGraphCanvasPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 40 |
| `AnimationTimelinePanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 41 |
| `AssetDatabaseWatchSystem` | Infrastructure | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp` | 20 |
| `AssetDatabaseWatchSystem` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 42 |
| `AssetImportService` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 30 |
| `AssetImportService` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 43 |
| `Callbacks` | Infrastructure | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp` | 38 |
| `Change` | Infrastructure | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp` | 102 |
| `Codec` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 217 |
| `Command` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 231 |
| `CommandKind` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 30 |
| `CommandStack` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 27 |
| `CommandStack` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 44 |
| `Config` | Infrastructure | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp` | 27 |
| `Config` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 39 |
| `Config` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 85 |
| `ContentBrowserPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 45 |
| `CookRequest` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 115 |
| `CookResult` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 126 |
| `EditorCamera` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 85 |
| `EditorDesc` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 56 |
| `EditorFontRole` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 22 |
| `EditorFonts` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 79 |
| `EditorFonts` | Infrastructure | `Source/Editor/Infrastructure/EditorTypography.hpp` | 7 |
| `EditorFontSpec` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 38 |
| `EditorFontStatus` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 66 |
| `EditorSettings` | Infrastructure | `Source/Editor/Infrastructure/EditorSettings.hpp` | 18 |
| `EditorSettingsPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 46 |
| `EditorSystem` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 94 |
| `Entry` | Infrastructure | `Source/Editor/Infrastructure/TextureThumbnails.hpp` | 52 |
| `ExternalClientDesc` | Infrastructure | `Source/Editor/Infrastructure/PlayInEditor.hpp` | 37 |
| `FrameComposer` | Infrastructure | `Source/Editor/Infrastructure/FrameComposer.hpp` | 28 |
| `ImFont` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 10 |
| `ImGuiIO` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 11 |
| `Impl` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 200 |
| `InteractionTransaction` | Infrastructure | `Source/Editor/Infrastructure/EditTransactions.hpp` | 16 |
| `LoadedSource` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 133 |
| `MaterialGraphPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 47 |
| `MaterialGraphRuntime` | Infrastructure | `Source/Editor/Infrastructure/MaterialGraphRuntime.hpp` | 41 |
| `MaterialRuntimeStats` | Infrastructure | `Source/Editor/Infrastructure/MaterialGraphRuntime.hpp` | 25 |
| `NetSession` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 27 |
| `NetworkDiagnosticsPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 48 |
| `PendingEdit` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 252 |
| `PendingSpawn` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 261 |
| `PhysicsDebugFlags` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 19 |
| `PhysicsDebugFrame` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 47 |
| `PhysicsDebugLine` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 32 |
| `PhysicsDebugPoint` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 40 |
| `PhysicsDebugVisualizer` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 58 |
| `PiePlayerInput` | Infrastructure | `Source/Editor/Infrastructure/PlayInEditor.hpp` | 27 |
| `PlayInEditor` | Infrastructure | `Source/Editor/Infrastructure/PlayInEditor.hpp` | 44 |
| `ProjectConfig` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 52 |
| `ProjectConfig` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 142 |
| `ProjectConfigStore` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 49 |
| `ProjectConfigStore` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 202 |
| `ProjectDashboard` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 50 |
| `ProjectPaths` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 53 |
| `ProjectPaths` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 164 |
| `ProjectPaths` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 21 |
| `ProjectSettings` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 90 |
| `ProjectSettings` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 22 |
| `ProjectSettingsPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 51 |
| `Range` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 246 |
| `ReleaseCookConfig` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 70 |
| `ReleaseCooker` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 154 |
| `ReleaseCookProgress` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 117 |
| `ReleaseCookResult` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 138 |
| `ReleaseCookStage` | Infrastructure | `Source/Editor/Infrastructure/ReleaseCooker.hpp` | 28 |
| `Result` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 40 |
| `Result` | Infrastructure | `Source/Editor/Infrastructure/PlayInEditor.hpp` | 47 |
| `SdfCollisionSystem` | Infrastructure | `Source/Editor/Infrastructure/PhysicsDebugDraw.hpp` | 13 |
| `SdfField` | Infrastructure | `Source/Editor/Infrastructure/FrameComposer.hpp` | 17 |
| `Services` | Infrastructure | `Source/Editor/Infrastructure/PlayInEditor.hpp` | 220 |
| `Slot` | Infrastructure | `Source/Editor/Infrastructure/MaterialGraphRuntime.hpp` | 118 |
| `Source` | Infrastructure | `Source/Editor/Infrastructure/EditTransactions.hpp` | 19 |
| `SpawnComponent` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 275 |
| `SpawnHeader` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 268 |
| `SpawnTransaction` | Infrastructure | `Source/Editor/Infrastructure/EditTransactions.hpp` | 190 |
| `StableSource` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 140 |
| `Stats` | Infrastructure | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp` | 88 |
| `Stats` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 55 |
| `Stats` | Infrastructure | `Source/Editor/Infrastructure/CommandStack.hpp` | 92 |
| `TextureArrayAsset` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 23 |
| `TextureBinding` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 142 |
| `TextureThumbnailCache` | Infrastructure | `Source/Editor/Infrastructure/TextureThumbnails.hpp` | 17 |
| `VirtualFileSystem` | Infrastructure | `Source/Editor/Infrastructure/AssetImportService.hpp` | 20 |
| `VirtualFileSystem` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 15 |
| `VirtualFileSystem` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 18 |
| `VirtualTextureSettings` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 32 |
| `VirtualTextureSettings` | Infrastructure | `Source/Editor/Infrastructure/ProjectConfig.hpp` | 30 |
| `VirtualTextureSystem` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 22 |
| `VisualScriptPanel` | Infrastructure | `Source/Editor/Infrastructure/EditorSystem.hpp` | 54 |
| `WindowManager` | Infrastructure | `Source/Editor/Infrastructure/EditorFonts.hpp` | 16 |

### Editor/Panels (76)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `Action` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 25 |
| `AIStateTreeCanvasPanel` | Panels | `Source/Editor/Panels/AIStateTreeCanvasPanel.hpp` | 55 |
| `AIStateTreeCompiled` | Panels | `Source/Editor/Panels/AIStateTreeCanvasPanel.hpp` | 35 |
| `AIStateTreeDocument` | Panels | `Source/Editor/Panels/AIStateTreeCanvasPanel.hpp` | 24 |
| `AnimationGraphCanvasPanel` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 116 |
| `AnimationGraphDocument` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 79 |
| `AnimationGraphLink` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 65 |
| `AnimationGraphLinkKind` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 33 |
| `AnimationGraphNode` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 45 |
| `AnimationGraphNodeKind` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 26 |
| `AnimationTimelinePanel` | Panels | `Source/Editor/Panels/AnimationTimelinePanel.hpp` | 34 |
| `AssetImportService` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 24 |
| `AudioSystem` | Panels | `Source/Editor/Panels/AnimationTimelinePanel.hpp` | 15 |
| `AudioSystem` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 11 |
| `CanvasModelOps` | Panels | `Source/Editor/Panels/NodeCanvasPanel.hpp` | 21 |
| `CommandStack` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 25 |
| `CommandStack` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 28 |
| `ComponentRegistry` | Panels | `Source/Editor/Panels/ScriptDebuggerPanel.hpp` | 18 |
| `Config` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 35 |
| `ContentBrowserPanel` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 27 |
| `DragPayload` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 60 |
| `EditorFonts` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 20 |
| `EditorFonts` | Panels | `Source/Editor/Panels/VisualScriptPanel.hpp` | 20 |
| `EditorSettingsPanel` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 72 |
| `EditSource` | Panels | `Source/Editor/Panels/AIStateTreeCanvasPanel.hpp` | 58 |
| `EditSource` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 119 |
| `Entry` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 43 |
| `FileDialog` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 140 |
| `ImNodesContext` | Panels | `Source/Editor/Panels/AIStateTreeCanvasPanel.hpp` | 13 |
| `ImNodesContext` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 15 |
| `LicensesPanel` | Panels | `Source/Editor/Panels/LicensesPanel.hpp` | 11 |
| `LogBoyPanel` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 394 |
| `LogBoyRecord` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 105 |
| `LogBoyRing` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 121 |
| `LogBoySeverity` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 33 |
| `LogBoySystem` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 186 |
| `LogicGraphDocument` | Panels | `Source/Editor/Panels/VisualScriptPanel.hpp` | 23 |
| `MaterialGraphPanel` | Panels | `Source/Editor/Panels/MaterialGraphPanel.hpp` | 20 |
| `Mode` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 84 |
| `NetSession` | Panels | `Source/Editor/Panels/NetworkDiagnosticsPanel.hpp` | 6 |
| `NetworkDiagnosticsPanel` | Panels | `Source/Editor/Panels/NetworkDiagnosticsPanel.hpp` | 15 |
| `NodeCanvasPanel` | Panels | `Source/Editor/Panels/NodeCanvasPanel.hpp` | 54 |
| `PIEAnimFrame` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 101 |
| `PieClientViewport` | Panels | `Source/Editor/Panels/PieClientViewport.hpp` | 15 |
| `PIEFrameSlot` | Panels | `Source/Editor/Panels/AnimationGraphCanvasPanel.hpp` | 283 |
| `ProjectConfigStore` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 29 |
| `ProjectConfigStore` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 17 |
| `ProjectDashboard` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 22 |
| `ProjectSettingsPanel` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 31 |
| `QueryKind` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 430 |
| `SceneDiagnosticsPanel` | Panels | `Source/Editor/Panels/SceneDiagnosticsPanel.hpp` | 37 |
| `SceneDiagnosticsSample` | Panels | `Source/Editor/Panels/SceneDiagnosticsPanel.hpp` | 15 |
| `ScriptDebuggerPanel` | Panels | `Source/Editor/Panels/ScriptDebuggerPanel.hpp` | 28 |
| `ScriptRuntime` | Panels | `Source/Editor/Panels/AnimationTimelinePanel.hpp` | 16 |
| `SinkUser` | Panels | `Source/Editor/Panels/AnimationTimelinePanel.hpp` | 88 |
| `SkeletonInspectorState` | Panels | `Source/Editor/Panels/SkeletonAssetPanel.hpp` | 19 |
| `SpawnContext` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 68 |
| `SpriteAnimationInspectorState` | Panels | `Source/Editor/Panels/SpriteAnimationPanel.hpp` | 21 |
| `SystemContext` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 22 |
| `SystemContext` | Panels | `Source/Editor/Panels/SkeletonAssetPanel.hpp` | 13 |
| `SystemContext` | Panels | `Source/Editor/Panels/SpriteAnimationPanel.hpp` | 13 |
| `SystemName` | Panels | `Source/Editor/Panels/LogBoyPanel.hpp` | 189 |
| `TextPreviewPanel` | Panels | `Source/Editor/Panels/TextPreviewPanel.hpp` | 24 |
| `TextureThumbnailCache` | Panels | `Source/Editor/Panels/SpriteAnimationPanel.hpp` | 19 |
| `TilemapEditorPanel` | Panels | `Source/Editor/Panels/TilemapEditorPanel.hpp` | 20 |
| `TimelinePanelContext` | Panels | `Source/Editor/Panels/AnimationTimelinePanel.hpp` | 22 |
| `VirtualFileSystem` | Panels | `Source/Editor/Panels/ContentBrowserPanel.hpp` | 18 |
| `VirtualFileSystem` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 13 |
| `VirtualFileSystem` | Panels | `Source/Editor/Panels/TextPreviewPanel.hpp` | 15 |
| `VirtualTexturePanel` | Panels | `Source/Editor/Panels/VirtualTexturePanel.hpp` | 18 |
| `VirtualTextureSystem` | Panels | `Source/Editor/Panels/VirtualTexturePanel.hpp` | 10 |
| `VisualScriptPanel` | Panels | `Source/Editor/Panels/ScriptDebuggerPanel.hpp` | 24 |
| `VisualScriptPanel` | Panels | `Source/Editor/Panels/VisualScriptPanel.hpp` | 32 |
| `VoiceDevicePanel` | Panels | `Source/Editor/Panels/VoiceDevicePanel.hpp` | 19 |
| `WidgetCanvas` | Panels | `Source/Editor/Panels/SettingsPanels.hpp` | 16 |
| `WindowManager` | Panels | `Source/Editor/Panels/ProjectDashboard.hpp` | 14 |

### Engine/Assets (57)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `AeadAlgorithm` | Assets | `Source/Engine/Assets/FemCrypto.hpp` | 18 |
| `AeadKey` | Assets | `Source/Engine/Assets/FemCrypto.hpp` | 39 |
| `AeadNonce` | Assets | `Source/Engine/Assets/FemCrypto.hpp` | 46 |
| `AeadTag` | Assets | `Source/Engine/Assets/FemCrypto.hpp` | 54 |
| `ArchiveBuildOptions` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 99 |
| `ArchiveCipher` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 29 |
| `ArchiveCodec` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 22 |
| `ArchiveEntryView` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 115 |
| `ArchiveInput` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 72 |
| `ArchiveProgress` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 85 |
| `ArchiveProgressStage` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 78 |
| `ArchiveReader` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 126 |
| `ArchiveReader` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 145 |
| `ArchiveStatus` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 36 |
| `AssetDatabase` | Assets | `Source/Engine/Assets/AssetDatabase.hpp` | 28 |
| `AssetDependency` | Assets | `Source/Engine/Assets/AssetMeta.hpp` | 83 |
| `AssetHandle` | Assets | `Source/Engine/Assets/AssetMeta.hpp` | 22 |
| `AssetImportSettings` | Assets | `Source/Engine/Assets/AssetMeta.hpp` | 45 |
| `AssetMeta` | Assets | `Source/Engine/Assets/AssetMeta.hpp` | 61 |
| `AssetRecord` | Assets | `Source/Engine/Assets/AssetDatabase.hpp` | 19 |
| `AstcBlock` | Assets | `Source/Engine/Assets/TextureCook.hpp` | 25 |
| `AtlasDesc` | Assets | `Source/Engine/Assets/TextureArray.hpp` | 28 |
| `AtlasLayer` | Assets | `Source/Engine/Assets/TextureArray.hpp` | 21 |
| `AudioClipHeader` | Assets | `Source/Engine/Assets/AudioClipFormat.hpp` | 87 |
| `BlockExtent` | Assets | `Source/Engine/Assets/TextureFormat.hpp` | 79 |
| `CleanRenderExtract` | Assets | `Source/Engine/Assets/ProjectScaffold.hpp` | 127 |
| `CookedAudioView` | Assets | `Source/Engine/Assets/AudioClipFormat.hpp` | 116 |
| `CookedTexture` | Assets | `Source/Engine/Assets/TextureCook.hpp` | 41 |
| `CookedTextureView` | Assets | `Source/Engine/Assets/TextureFormat.hpp` | 179 |
| `CookProgressFlags` | Assets | `Source/Engine/Assets/CookProgress.hpp` | 31 |
| `CookProgressRecord` | Assets | `Source/Engine/Assets/CookProgress.hpp` | 38 |
| `CookProgressStage` | Assets | `Source/Engine/Assets/CookProgress.hpp` | 18 |
| `CookTarget` | Assets | `Source/Engine/Assets/TextureCook.hpp` | 17 |
| `FanmHeader` | Assets | `Source/Engine/Assets/AnimationClipAsset.hpp` | 22 |
| `FemArchiver` | Assets | `Source/Engine/Assets/FemArchiver.hpp` | 128 |
| `FempakInput` | Assets | `Source/Engine/Assets/FempakWriter.hpp` | 15 |
| `FempakWriter` | Assets | `Source/Engine/Assets/FempakWriter.hpp` | 22 |
| `LocaleTable` | Assets | `Source/Engine/Assets/Localization.hpp` | 14 |
| `LocalizationGender` | Assets | `Source/Engine/Assets/Localization.hpp` | 21 |
| `LocalizationPack` | Assets | `Source/Engine/Assets/Localization.hpp` | 13 |
| `LocalizationTable` | Assets | `Source/Engine/Assets/Localization.hpp` | 28 |
| `MappedFile` | Assets | `Source/Engine/Assets/VFS.hpp` | 19 |
| `MountRecord` | Assets | `Source/Engine/Assets/VFS.hpp` | 128 |
| `PakEntry` | Assets | `Source/Engine/Assets/PakFormat.hpp` | 26 |
| `PakHeader` | Assets | `Source/Engine/Assets/PakFormat.hpp` | 16 |
| `PcmFormat` | Assets | `Source/Engine/Assets/AudioClipFormat.hpp` | 21 |
| `PinnedFile` | Assets | `Source/Engine/Assets/VFS.hpp` | 63 |
| `ReloadNotice` | Assets | `Source/Engine/Assets/AssetDatabase.hpp` | 86 |
| `StagedRow` | Assets | `Source/Engine/Assets/AssetDatabase.hpp` | 108 |
| `StringEntry` | Assets | `Source/Engine/Assets/Localization.hpp` | 15 |
| `TextureArrayAsset` | Assets | `Source/Engine/Assets/TextureArray.hpp` | 46 |
| `TextureCookOptions` | Assets | `Source/Engine/Assets/TextureCook.hpp` | 31 |
| `TextureFormat` | Assets | `Source/Engine/Assets/TextureFormat.hpp` | 22 |
| `TextureHeader` | Assets | `Source/Engine/Assets/TextureFormat.hpp` | 160 |
| `VfsEntry` | Assets | `Source/Engine/Assets/VFS.hpp` | 55 |
| `VirtualFileSystem` | Assets | `Source/Engine/Assets/TextureArray.hpp` | 16 |
| `VirtualFileSystem` | Assets | `Source/Engine/Assets/VFS.hpp` | 69 |

### Engine/Audio (78)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `AudioBus` | Audio | `Source/Engine/Audio/Audio.hpp` | 26 |
| `AudioBusComponent` | Audio | `Source/Engine/Audio/Audio.hpp` | 40 |
| `AudioCaptureMonitor` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 144 |
| `AudioClip` | Audio | `Source/Engine/Audio/Audio.hpp` | 69 |
| `AudioCommand` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 138 |
| `AudioCommandType` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 121 |
| `AudioContextComponent` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 67 |
| `AudioDeviceEnumerator` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 93 |
| `AudioDeviceInfo` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 54 |
| `AudioDeviceKind` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 15 |
| `AudioDeviceList` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 66 |
| `AudioDevicePreferences` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 73 |
| `AudioDeviceStatus` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 21 |
| `AudioLimiter` | Audio | `Source/Engine/Audio/Limiter.hpp` | 23 |
| `AudioListener` | Audio | `Source/Engine/Audio/Audio.hpp` | 94 |
| `AudioSource` | Audio | `Source/Engine/Audio/Audio.hpp` | 79 |
| `AudioSourceComponent` | Audio | `Source/Engine/Audio/Audio.hpp` | 53 |
| `AudioSystem` | Audio | `Source/Engine/Audio/Audio.hpp` | 100 |
| `AudioZoneComponent` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 32 |
| `AudioZoneKind` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 14 |
| `AudioZoneSystem` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 91 |
| `BusAtomics` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 370 |
| `BusFilterParams` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 108 |
| `BusRuntime` | Audio | `Source/Engine/Audio/Audio.hpp` | 212 |
| `BusShadow` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 291 |
| `BusState` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 338 |
| `CommandQueue` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 170 |
| `CookedClipRecord` | Audio | `Source/Engine/Audio/Audio.hpp` | 187 |
| `DspClipView` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 205 |
| `DspGraph` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 216 |
| `DspGraph` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 16 |
| `DspResult` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 34 |
| `DspResult` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 19 |
| `DuckingParams` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 80 |
| `DuckingState` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 298 |
| `FilterState` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 327 |
| `FloatRing` | Audio | `Source/Engine/Audio/Audio.hpp` | 24 |
| `FloatRing` | Audio | `Source/Engine/Audio/AudioStream.hpp` | 16 |
| `FloatRing` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 32 |
| `JitterSlot` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 110 |
| `LimiterDesc` | Audio | `Source/Engine/Audio/Limiter.hpp` | 11 |
| `OggOpusStream` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 131 |
| `OggPacket` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 76 |
| `OggPacketReader` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 86 |
| `OggPage` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 36 |
| `OggPageReader` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 57 |
| `OpusHeadInfo` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 106 |
| `OwnedPcmRecord` | Audio | `Source/Engine/Audio/Audio.hpp` | 194 |
| `Peer` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 120 |
| `RateBucket` | Audio | `Source/Engine/Audio/VoiceNetworkBridge.hpp` | 62 |
| `ResolvedZone` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 113 |
| `ReverbParams` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 96 |
| `ReverbState` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 311 |
| `SourceRuntime` | Audio | `Source/Engine/Audio/Audio.hpp` | 204 |
| `State` | Audio | `Source/Engine/Audio/AudioDevices.hpp` | 164 |
| `Stats` | Audio | `Source/Engine/Audio/AudioZones.hpp` | 94 |
| `VoiceAec` | Audio | `Source/Engine/Audio/VoiceAec.hpp` | 44 |
| `VoiceAecSettings` | Audio | `Source/Engine/Audio/VoiceAec.hpp` | 22 |
| `VoiceCapture` | Audio | `Source/Engine/Audio/AudioStream.hpp` | 64 |
| `VoiceChannel` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 65 |
| `VoiceChannelConfig` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 36 |
| `VoiceChannelStats` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 46 |
| `VoiceCodecConfig` | Audio | `Source/Engine/Audio/VoiceCodec.hpp` | 15 |
| `VoiceDecoder` | Audio | `Source/Engine/Audio/VoiceCodec.hpp` | 51 |
| `VoiceDenoiseTier` | Audio | `Source/Engine/Audio/VoiceDsp.hpp` | 10 |
| `VoiceDspChain` | Audio | `Source/Engine/Audio/VoiceDsp.hpp` | 99 |
| `VoiceDspSettings` | Audio | `Source/Engine/Audio/VoiceDsp.hpp` | 43 |
| `VoiceEncoder` | Audio | `Source/Engine/Audio/VoiceCodec.hpp` | 27 |
| `VoiceNetworkBridge` | Audio | `Source/Engine/Audio/VoiceNetworkBridge.hpp` | 18 |
| `VoiceParticipant` | Audio | `Source/Engine/Audio/VoiceSession.hpp` | 19 |
| `VoicePresetKind` | Audio | `Source/Engine/Audio/VoiceDsp.hpp` | 68 |
| `VoiceRouting` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 30 |
| `VoiceSession` | Audio | `Source/Engine/Audio/VoiceChannel.hpp` | 15 |
| `VoiceSession` | Audio | `Source/Engine/Audio/VoiceSession.hpp` | 31 |
| `VoiceState` | Audio | `Source/Engine/Audio/DspGraph.hpp` | 350 |
| `VoiceSystem` | Audio | `Source/Engine/Audio/VoiceSystem.hpp` | 34 |
| `VoiceSystemConfig` | Audio | `Source/Engine/Audio/VoiceSystem.hpp` | 20 |
| `VorbisIdentInfo` | Audio | `Source/Engine/Audio/OggOpus.hpp` | 121 |

### Engine/Core (87)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `AssetId` | Core | `Source/Engine/Core/AssetId.hpp` | 43 |
| `AssetKind` | Core | `Source/Engine/Core/AssetId.hpp` | 15 |
| `BigIntT` | Core | `Source/Engine/Core/BigInt.hpp` | 179 |
| `BigNumberStore` | Core | `Source/Engine/Core/BigNumberStore.hpp` | 41 |
| `BinaryKind` | Core | `Source/Engine/Core/BigNumberStore.hpp` | 259 |
| `BinaryKind` | Core | `Source/Engine/Core/DecimalStore.hpp` | 206 |
| `BufferTag` | Core | `Source/Engine/Core/Types.hpp` | 301 |
| `BuildInfo` | Core | `Source/Engine/Core/BuildInfo.hpp` | 12 |
| `BuildProfile` | Core | `Source/Engine/Core/BuildProfile.hpp` | 135 |
| `CameraView` | Core | `Source/Engine/Core/Grid.hpp` | 337 |
| `Cell` | Core | `Source/Engine/Core/Grid.hpp` | 74 |
| `CivilDate` | Core | `Source/Engine/Core/Calendar.hpp` | 21 |
| `Clock` | Core | `Source/Engine/Core/Time.hpp` | 28 |
| `ClockTime` | Core | `Source/Engine/Core/Calendar.hpp` | 32 |
| `Color32` | Core | `Source/Engine/Core/Types.hpp` | 203 |
| `ComponentDesc` | Core | `Source/Engine/Core/Mirror.hpp` | 48 |
| `ComponentRegistry` | Core | `Source/Engine/Core/PluginHost.hpp` | 27 |
| `ComponentType` | Core | `Source/Engine/Core/PluginHost.hpp` | 18 |
| `DateTime` | Core | `Source/Engine/Core/Calendar.hpp` | 44 |
| `DecimalStore` | Core | `Source/Engine/Core/DecimalStore.hpp` | 17 |
| `DecimalT` | Core | `Source/Engine/Core/Decimal.hpp` | 34 |
| `DurationStyle` | Core | `Source/Engine/Core/Time.hpp` | 571 |
| `EddsaKeyPair` | Core | `Source/Engine/Core/CryptoAsymmetric.hpp` | 31 |
| `FieldDesc` | Core | `Source/Engine/Core/Mirror.hpp` | 37 |
| `FieldKind` | Core | `Source/Engine/Core/Mirror.hpp` | 17 |
| `FileFilter` | Core | `Source/Engine/Core/WindowManager.hpp` | 154 |
| `FormatSpec` | Core | `Source/Engine/Core/BigInt.hpp` | 36 |
| `FrameArena` | Core | `Source/Engine/Core/FrameArena.hpp` | 12 |
| `FrameTimer` | Core | `Source/Engine/Core/Time.hpp` | 501 |
| `GridDesc` | Core | `Source/Engine/Core/Grid.hpp` | 91 |
| `Handle` | Core | `Source/Engine/Core/BigNumberStore.hpp` | 20 |
| `Handle` | Core | `Source/Engine/Core/Types.hpp` | 275 |
| `Host` | Core | `Source/Engine/Core/Plugin.hpp` | 20 |
| `InputState` | Core | `Source/Engine/Core/WindowManager.hpp` | 98 |
| `JobSystem` | Core | `Source/Engine/Core/JobSystem.hpp` | 25 |
| `Key` | Core | `Source/Engine/Core/WindowManager.hpp` | 30 |
| `KeyLess` | Core | `Source/Engine/Core/FlatMap.hpp` | 198 |
| `LoadedPlugin` | Core | `Source/Engine/Core/PluginHost.hpp` | 64 |
| `LogLevel` | Core | `Source/Engine/Core/Log.hpp` | 12 |
| `Mat4` | Core | `Source/Engine/Core/Mat4.hpp` | 14 |
| `NoiseGenerator` | Core | `Source/Engine/Core/NoiseGenerator.hpp` | 10 |
| `Notation` | Core | `Source/Engine/Core/BigInt.hpp` | 30 |
| `NoticeEntry` | Core | `Source/Engine/Core/ThirdPartyNotices.hpp` | 12 |
| `OptimizationAdvice` | Core | `Source/Engine/Core/BuildProfile.hpp` | 154 |
| `OptimizationLevel` | Core | `Source/Engine/Core/BuildProfile.hpp` | 10 |
| `Pcg32` | Core | `Source/Engine/Core/Pcg32.hpp` | 20 |
| `PipelineTag` | Core | `Source/Engine/Core/Types.hpp` | 303 |
| `PluginHost` | Core | `Source/Engine/Core/PluginHost.hpp` | 76 |
| `PluginSystem` | Core | `Source/Engine/Core/PluginHost.hpp` | 54 |
| `ProjectionMode` | Core | `Source/Engine/Core/Grid.hpp` | 17 |
| `Rect` | Core | `Source/Engine/Core/Types.hpp` | 173 |
| `Reflection` | Core | `Source/Engine/Core/Mirror.hpp` | 66 |
| `Reflection` | Core | `Source/Engine/Core/Mirror.hpp` | 289 |
| `Reflection` | Core | `Source/Engine/Core/Mirror.hpp` | 301 |
| `RngState` | Core | `Source/Engine/Core/Pcg32.hpp` | 11 |
| `RoundingMode` | Core | `Source/Engine/Core/Decimal.hpp` | 22 |
| `SamplerTag` | Core | `Source/Engine/Core/Types.hpp` | 304 |
| `SecureRng` | Core | `Source/Engine/Core/SecureRng.hpp` | 11 |
| `Sha256Context` | Core | `Source/Engine/Core/Sha256.hpp` | 17 |
| `Slot` | Core | `Source/Engine/Core/BigNumberStore.hpp` | 252 |
| `Slot` | Core | `Source/Engine/Core/DecimalStore.hpp` | 199 |
| `Status` | Core | `Source/Engine/Core/BigInt.hpp` | 18 |
| `Stopwatch` | Core | `Source/Engine/Core/Time.hpp` | 209 |
| `Task` | Core | `Source/Engine/Core/JobSystem.hpp` | 96 |
| `TextureTag` | Core | `Source/Engine/Core/Types.hpp` | 302 |
| `TextWriter` | Core | `Source/Engine/Core/Calendar.hpp` | 189 |
| `ThreadCounter` | Core | `Source/Engine/Core/JobSystem.hpp` | 109 |
| `ThreadLoad` | Core | `Source/Engine/Core/JobSystem.hpp` | 31 |
| `TimerFired` | Core | `Source/Engine/Core/Time.hpp` | 255 |
| `TimerHandle` | Core | `Source/Engine/Core/Time.hpp` | 240 |
| `TimerPool` | Core | `Source/Engine/Core/Time.hpp` | 266 |
| `TypeNameOverride` | Core | `Source/Engine/Core/Definitions.hpp` | 40 |
| `TypeNameOverride` | Core | `Source/Engine/Core/Definitions.hpp` | 283 |
| `TypeObject` | Core | `Source/Engine/Core/Definitions.hpp` | 185 |
| `TypeRecord` | Core | `Source/Engine/Core/Definitions.hpp` | 85 |
| `TypeTable` | Core | `Source/Engine/Core/Definitions.hpp` | 93 |
| `TypeTraits` | Core | `Source/Engine/Core/Definitions.hpp` | 170 |
| `TypeTraits` | Core | `Source/Engine/Core/Definitions.hpp` | 267 |
| `Unregistered` | Core | `Source/Engine/Core/Mirror.hpp` | 59 |
| `Vector2D` | Core | `Source/Engine/Core/Types.hpp` | 13 |
| `Vector3D` | Core | `Source/Engine/Core/Types.hpp` | 90 |
| `Vector4D` | Core | `Source/Engine/Core/Types.hpp` | 139 |
| `WindowDesc` | Core | `Source/Engine/Core/WindowManager.hpp` | 175 |
| `WindowExtent` | Core | `Source/Engine/Core/WindowManager.hpp` | 21 |
| `WindowManager` | Core | `Source/Engine/Core/WindowManager.hpp` | 186 |
| `WindowMode` | Core | `Source/Engine/Core/WindowManager.hpp` | 166 |
| `X25519KeyPair` | Core | `Source/Engine/Core/CryptoAsymmetric.hpp` | 20 |

### Engine/ECS (185)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `AccessibilityKernel` | ECS | `Source/Engine/ECS/Schedule.hpp` | 21 |
| `AICommandComponent` | ECS | `Source/Engine/ECS/StateTree.hpp` | 150 |
| `AICommandKind` | ECS | `Source/Engine/ECS/StateTree.hpp` | 141 |
| `AnimationTimeline` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 90 |
| `AnimBoneKey` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 70 |
| `AnimClip` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 82 |
| `AnimConditionFlags` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 27 |
| `AnimGraphSystem` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 174 |
| `AnimRestBone` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 100 |
| `AnimStateComponent` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 110 |
| `AnimTransition` | ECS | `Source/Engine/ECS/AnimGraph.hpp` | 47 |
| `AssetDatabase` | ECS | `Source/Engine/ECS/Scene.hpp` | 21 |
| `AssetDatabase` | ECS | `Source/Engine/ECS/Schedule.hpp` | 29 |
| `AudioSystem` | ECS | `Source/Engine/ECS/Schedule.hpp` | 16 |
| `BigIntComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 194 |
| `CachedTarget` | ECS | `Source/Engine/ECS/LightSource.hpp` | 225 |
| `Camera2D` | ECS | `Source/Engine/ECS/Components.hpp` | 80 |
| `CameraStackSystem` | ECS | `Source/Engine/ECS/Schedule.hpp` | 26 |
| `ChainIdTag` | ECS | `Source/Engine/ECS/StableIds.hpp` | 12 |
| `ChainSaveV3` | ECS | `Source/Engine/ECS/SnapshotWorld.hpp` | 28 |
| `ChangeMaskBit` | ECS | `Source/Engine/ECS/ChangeMask.hpp` | 19 |
| `CharacterDesc` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 51 |
| `ClothIdTag` | ECS | `Source/Engine/ECS/StableIds.hpp` | 13 |
| `ClothSaveV3` | ECS | `Source/Engine/ECS/SnapshotWorld.hpp` | 59 |
| `ClothSystem` | ECS | `Source/Engine/ECS/Schedule.hpp` | 23 |
| `ColliderSnapshot` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 181 |
| `CollisionMode` | ECS | `Source/Engine/ECS/Presentation.hpp` | 21 |
| `CollisionPolicyComponent` | ECS | `Source/Engine/ECS/Presentation.hpp` | 101 |
| `Command` | ECS | `Source/Engine/ECS/CommandBuffer.hpp` | 29 |
| `CommandBuffer` | ECS | `Source/Engine/ECS/CommandBuffer.hpp` | 37 |
| `CommandBuffer` | ECS | `Source/Engine/ECS/Schedule.hpp` | 50 |
| `CommandKind` | ECS | `Source/Engine/ECS/CommandBuffer.hpp` | 19 |
| `CommandLog` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 73 |
| `CommandLogHeader` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 47 |
| `CommandRecord` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 59 |
| `CommandType` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 22 |
| `CommandWorldCallbacks` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 37 |
| `ComponentChangeMask` | ECS | `Source/Engine/ECS/ChangeMask.hpp` | 52 |
| `ComponentOps` | ECS | `Source/Engine/ECS/ComponentRegistry.hpp` | 23 |
| `ComponentOpsFor` | ECS | `Source/Engine/ECS/ComponentRegistry.hpp` | 46 |
| `ComponentRegistry` | ECS | `Source/Engine/ECS/ComponentRegistry.hpp` | 107 |
| `CompressedClip` | ECS | `Source/Engine/ECS/AnimationCompression.hpp` | 47 |
| `CompressedKey` | ECS | `Source/Engine/ECS/AnimationCompression.hpp` | 19 |
| `CompressedTrack` | ECS | `Source/Engine/ECS/AnimationCompression.hpp` | 31 |
| `CompressionReport` | ECS | `Source/Engine/ECS/AnimationCompression.hpp` | 63 |
| `Cursor` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 146 |
| `DecimalComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 205 |
| `DrawOrder` | ECS | `Source/Engine/ECS/Components.hpp` | 69 |
| `EditorSelected` | ECS | `Source/Engine/ECS/Components.hpp` | 94 |
| `EmitterIdTag` | ECS | `Source/Engine/ECS/StableIds.hpp` | 14 |
| `EmitterSaveV3` | ECS | `Source/Engine/ECS/SnapshotWorld.hpp` | 79 |
| `EntityRemap` | ECS | `Source/Engine/ECS/Scene.hpp` | 59 |
| `Entry` | ECS | `Source/Engine/ECS/Schedule.hpp` | 120 |
| `FesklHeader` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 37 |
| `FesklNameRef` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 87 |
| `FesklSprite` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 97 |
| `FluidSaveV3` | ECS | `Source/Engine/ECS/SnapshotWorld.hpp` | 71 |
| `FluidWaveSystem` | ECS | `Source/Engine/ECS/Schedule.hpp` | 24 |
| `FontAtlasSet` | ECS | `Source/Engine/ECS/Schedule.hpp` | 37 |
| `FrameArena` | ECS | `Source/Engine/ECS/Schedule.hpp` | 17 |
| `GameplayState` | ECS | `Source/Engine/ECS/Components.hpp` | 132 |
| `GameplayStateFlag` | ECS | `Source/Engine/ECS/Components.hpp` | 106 |
| `GraphSystem` | ECS | `Source/Engine/ECS/SystemGraph.hpp` | 17 |
| `Hidden` | ECS | `Source/Engine/ECS/Components.hpp` | 99 |
| `IkChain` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 43 |
| `IkComponent` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 64 |
| `IkGoalComponent` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 77 |
| `IkSolver` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 19 |
| `Interpolation` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 29 |
| `ITextShaper` | ECS | `Source/Engine/ECS/Schedule.hpp` | 43 |
| `Lifetime` | ECS | `Source/Engine/ECS/Components.hpp` | 88 |
| `Light2DComponent` | ECS | `Source/Engine/ECS/Lighting.hpp` | 33 |
| `Light2DOccluderComponent` | ECS | `Source/Engine/ECS/Lighting.hpp` | 102 |
| `Light2DResultComponent` | ECS | `Source/Engine/ECS/Lighting.hpp` | 65 |
| `Light2DTargetComponent` | ECS | `Source/Engine/ECS/Lighting.hpp` | 49 |
| `LightingSettings` | ECS | `Source/Engine/ECS/Lighting.hpp` | 113 |
| `LightingSystem` | ECS | `Source/Engine/ECS/Lighting.hpp` | 138 |
| `LightSourceComponent` | ECS | `Source/Engine/ECS/LightSource.hpp` | 48 |
| `LightSourceFlags` | ECS | `Source/Engine/ECS/LightSource.hpp` | 32 |
| `LightSourceSettings` | ECS | `Source/Engine/ECS/LightSource.hpp` | 149 |
| `LightSourceSystem` | ECS | `Source/Engine/ECS/LightSource.hpp` | 168 |
| `LightSourceType` | ECS | `Source/Engine/ECS/LightSource.hpp` | 22 |
| `LocalizationTable` | ECS | `Source/Engine/ECS/Schedule.hpp` | 22 |
| `LocalizedTextComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 252 |
| `LocalTransform2D` | ECS | `Source/Engine/ECS/Components.hpp` | 34 |
| `Mat3` | ECS | `Source/Engine/ECS/Skeleton.hpp` | 31 |
| `NameComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 173 |
| `NarrativeLabelComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 183 |
| `NativeCleanupHook` | ECS | `Source/Engine/ECS/RegistryOps.hpp` | 12 |
| `Parent2D` | ECS | `Source/Engine/ECS/Components.hpp` | 42 |
| `Phase` | ECS | `Source/Engine/ECS/Schedule.hpp` | 55 |
| `PhysicsBodyDesc` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 22 |
| `PhysicsJointSystem` | ECS | `Source/Engine/ECS/Schedule.hpp` | 25 |
| `PhysicsShapeKind` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 15 |
| `PhysicsSystem` | ECS | `Source/Engine/ECS/Schedule.hpp` | 18 |
| `PrefabAsset` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 88 |
| `PrefabEntityDesc` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 76 |
| `PrefabEntityFlags` | ECS | `Source/Engine/ECS/Descriptors.hpp` | 98 |
| `PrefabEntityOverride` | ECS | `Source/Engine/ECS/PrefabSystem.hpp` | 18 |
| `PrefabOverrides` | ECS | `Source/Engine/ECS/PrefabSystem.hpp` | 31 |
| `PrefabSystem` | ECS | `Source/Engine/ECS/PrefabSystem.hpp` | 36 |
| `QuantizedPose` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 165 |
| `RenderMaskComponent` | ECS | `Source/Engine/ECS/Presentation.hpp` | 139 |
| `RenderPassMask` | ECS | `Source/Engine/ECS/Presentation.hpp` | 126 |
| `RetargetMap` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 131 |
| `RetargetPair` | ECS | `Source/Engine/ECS/SkeletonIk.hpp` | 118 |
| `RHI` | ECS | `Source/Engine/ECS/Schedule.hpp` | 33 |
| `RigidBodySnapshot` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 167 |
| `SceneComponent` | ECS | `Source/Engine/ECS/Scene.hpp` | 38 |
| `SceneData` | ECS | `Source/Engine/ECS/Scene.hpp` | 52 |
| `SceneEntity` | ECS | `Source/Engine/ECS/Scene.hpp` | 45 |
| `SceneLoadOptions` | ECS | `Source/Engine/ECS/Scene.hpp` | 76 |
| `SceneMeta` | ECS | `Source/Engine/ECS/Scene.hpp` | 31 |
| `Schedule` | ECS | `Source/Engine/ECS/Schedule.hpp` | 115 |
| `SdfLightingFrame` | ECS | `Source/Engine/ECS/Lighting.hpp` | 18 |
| `SdfSunComponent` | ECS | `Source/Engine/ECS/Lighting.hpp` | 77 |
| `SkeletonAssetRef` | ECS | `Source/Engine/ECS/SkeletonAssetRef.hpp` | 20 |
| `SkeletonAssetSource` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 189 |
| `SkeletonAssetView` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 121 |
| `SkeletonBoneName` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 168 |
| `SkeletonComponent` | ECS | `Source/Engine/ECS/Skeleton.hpp` | 121 |
| `SkeletonNameTable` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 175 |
| `SkeletonSystem` | ECS | `Source/Engine/ECS/Skeleton.hpp` | 198 |
| `SkinComponent` | ECS | `Source/Engine/ECS/Skeleton.hpp` | 136 |
| `SkinnedSprite` | ECS | `Source/Engine/ECS/Skeleton.hpp` | 147 |
| `SleepingStaticComponent` | ECS | `Source/Engine/ECS/Presentation.hpp` | 202 |
| `SnapshotBaseline` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 314 |
| `SnapshotComponent` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 76 |
| `SnapshotComponentOp` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 41 |
| `SnapshotExtras` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 265 |
| `SnapshotFlags` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 29 |
| `SnapshotHeader` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 47 |
| `SnapshotRecordFlags` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 35 |
| `SnapshotRestoreExtras` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 270 |
| `SnapshotSchema` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 91 |
| `SnapshotSchema` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 206 |
| `SnapshotView` | ECS | `Source/Engine/ECS/Snapshot.hpp` | 255 |
| `Sprite2D` | ECS | `Source/Engine/ECS/Components.hpp` | 54 |
| `SpriteAnimationComponent` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 21 |
| `SpriteAnimationEventQueue` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 74 |
| `SpriteAnimationSystem` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 118 |
| `SpriteFrameEvent` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 66 |
| `SpriteFrameRect` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 55 |
| `SpriteSheetComponent` | ECS | `Source/Engine/ECS/SpriteAnimation.hpp` | 40 |
| `StableIdAllocator` | ECS | `Source/Engine/ECS/StableIds.hpp` | 25 |
| `StateTreeComponent` | ECS | `Source/Engine/ECS/StateTree.hpp` | 167 |
| `StateTreeDefinition` | ECS | `Source/Engine/ECS/StateTree.hpp` | 130 |
| `StateTreeEvaluator` | ECS | `Source/Engine/ECS/StateTree.hpp` | 48 |
| `StateTreeEvaluatorKind` | ECS | `Source/Engine/ECS/StateTree.hpp` | 26 |
| `StateTreeNode` | ECS | `Source/Engine/ECS/StateTree.hpp` | 94 |
| `StateTreeNodeKind` | ECS | `Source/Engine/ECS/StateTree.hpp` | 84 |
| `StateTreeSystem` | ECS | `Source/Engine/ECS/StateTree.hpp` | 212 |
| `StateTreeTask` | ECS | `Source/Engine/ECS/StateTree.hpp` | 71 |
| `StateTreeTaskKind` | ECS | `Source/Engine/ECS/StateTree.hpp` | 61 |
| `StateTreeTransition` | ECS | `Source/Engine/ECS/StateTree.hpp` | 113 |
| `SystemContext` | ECS | `Source/Engine/ECS/Schedule.hpp` | 83 |
| `SystemGraph` | ECS | `Source/Engine/ECS/Schedule.hpp` | 51 |
| `SystemGraph` | ECS | `Source/Engine/ECS/SystemGraph.hpp` | 29 |
| `TextComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 224 |
| `TextConditionComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 308 |
| `TextEffectComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 278 |
| `TextFormatComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 290 |
| `TextService` | ECS | `Source/Engine/ECS/Schedule.hpp` | 41 |
| `TextShapeCache` | ECS | `Source/Engine/ECS/Schedule.hpp` | 42 |
| `TextShapeComponent` | ECS | `Source/Engine/ECS/Components.hpp` | 267 |
| `TilemapAssetRefComponent` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 130 |
| `TilemapComponent` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 63 |
| `TilemapRenderSystem` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 150 |
| `TileQuad` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 142 |
| `TimelineEvent` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 67 |
| `TimelineEventKind` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 36 |
| `TimelineKey` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 43 |
| `TimelineTrack` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 56 |
| `TimelineTriggerSink` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 83 |
| `TimeScaleComponent` | ECS | `Source/Engine/ECS/Presentation.hpp` | 168 |
| `TrackKind` | ECS | `Source/Engine/ECS/AnimationTimeline.hpp` | 20 |
| `Transform2D` | ECS | `Source/Engine/ECS/Components.hpp` | 23 |
| `Velocity` | ECS | `Source/Engine/ECS/Components.hpp` | 48 |
| `View` | ECS | `Source/Engine/ECS/CommandLog.hpp` | 156 |
| `VirtualFileSystem` | ECS | `Source/Engine/ECS/Scene.hpp` | 18 |
| `VirtualFileSystem` | ECS | `Source/Engine/ECS/Schedule.hpp` | 20 |
| `VirtualFileSystem` | ECS | `Source/Engine/ECS/SkeletonAsset.hpp` | 18 |
| `VirtualFileSystem` | ECS | `Source/Engine/ECS/Tilemap.hpp` | 20 |
| `WindowManager` | ECS | `Source/Engine/ECS/Schedule.hpp` | 19 |
| `WorldSnapshotV3` | ECS | `Source/Engine/ECS/SnapshotWorld.hpp` | 86 |

### Engine/Navigation (7)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `NavAgentComponent` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 161 |
| `NavAgentSettings` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 139 |
| `NavigationSystem` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 184 |
| `NavMap` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 25 |
| `NavPath` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 67 |
| `PathFinder` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 91 |
| `PathStats` | Navigation | `Source/Engine/Navigation/Navigation.hpp` | 80 |

### Engine/Network (89)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `CellCoordinate` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 168 |
| `ChecksumComparison` | Network | `Source/Engine/Network/NetRollback.hpp` | 457 |
| `Config` | Network | `Source/Engine/Network/NetRollback.hpp` | 316 |
| `DatagramSocket` | Network | `Source/Engine/Network/Transport.hpp` | 32 |
| `Endpoint` | Network | `Source/Engine/Network/NetLink.hpp` | 92 |
| `Frame` | Network | `Source/Engine/Network/NetLink.hpp` | 84 |
| `FrameInfo` | Network | `Source/Engine/Network/NetRollback.hpp` | 221 |
| `FrameMeta` | Network | `Source/Engine/Network/NetRollback.hpp` | 473 |
| `InputChannel` | Network | `Source/Engine/Network/NetProtocol.hpp` | 56 |
| `InputChannelStats` | Network | `Source/Engine/Network/NetProtocol.hpp` | 44 |
| `InputPacket` | Network | `Source/Engine/Network/NetProtocol.hpp` | 24 |
| `InputRingBuffer` | Network | `Source/Engine/Network/NetRollback.hpp` | 123 |
| `LobbyError` | Network | `Source/Engine/Network/Lobby.hpp` | 37 |
| `LobbyMessageType` | Network | `Source/Engine/Network/Lobby.hpp` | 25 |
| `LobbyRoomInfo` | Network | `Source/Engine/Network/Lobby.hpp` | 73 |
| `LobbyServer` | Network | `Source/Engine/Network/Lobby.hpp` | 93 |
| `LobbyServerConfig` | Network | `Source/Engine/Network/Lobby.hpp` | 82 |
| `LocalEntry` | Network | `Source/Engine/Network/NetRollback.hpp` | 205 |
| `Member` | Network | `Source/Engine/Network/Lobby.hpp` | 126 |
| `MemoryEndpoint` | Network | `Source/Engine/Network/NetLink.hpp` | 46 |
| `MemoryEndpoint` | Network | `Source/Engine/Network/NetLink.hpp` | 113 |
| `MemoryNetwork` | Network | `Source/Engine/Network/NetLink.hpp` | 53 |
| `NetAddress` | Network | `Source/Engine/Network/Transport.hpp` | 19 |
| `NetDiagnosticsSnapshot` | Network | `Source/Engine/Network/Diagnostics.hpp` | 12 |
| `NetDisconnectReason` | Network | `Source/Engine/Network/Session.hpp` | 65 |
| `NetEntityId` | Network | `Source/Engine/Network/NetRollback.hpp` | 39 |
| `NetEntityMap` | Network | `Source/Engine/Network/NetRollback.hpp` | 167 |
| `NetEntry` | Network | `Source/Engine/Network/NetRollback.hpp` | 200 |
| `NetIdentity` | Network | `Source/Engine/Network/Identity.hpp` | 32 |
| `NetInputFrame` | Network | `Source/Engine/Network/NetRollback.hpp` | 70 |
| `NetInputStatus` | Network | `Source/Engine/Network/NetRollback.hpp` | 99 |
| `NetLink` | Network | `Source/Engine/Network/NetLink.hpp` | 19 |
| `NetLobbyClient` | Network | `Source/Engine/Network/Lobby.hpp` | 160 |
| `NetLobbyClientConfig` | Network | `Source/Engine/Network/Lobby.hpp` | 151 |
| `NetPlayerId` | Network | `Source/Engine/Network/NetRollback.hpp` | 59 |
| `NetPlayerSlot` | Network | `Source/Engine/Network/Session.hpp` | 134 |
| `NetRole` | Network | `Source/Engine/Network/Session.hpp` | 32 |
| `NetRollbackSession` | Network | `Source/Engine/Network/NetRollback.hpp` | 240 |
| `NetSession` | Network | `Source/Engine/Network/Session.hpp` | 189 |
| `NetSessionCallbacks` | Network | `Source/Engine/Network/Session.hpp` | 178 |
| `NetSessionConfig` | Network | `Source/Engine/Network/Lobby.hpp` | 15 |
| `NetSessionConfig` | Network | `Source/Engine/Network/Session.hpp` | 149 |
| `NetSessionMode` | Network | `Source/Engine/Network/Session.hpp` | 22 |
| `NetSessionStatus` | Network | `Source/Engine/Network/Session.hpp` | 46 |
| `NetSlotState` | Network | `Source/Engine/Network/Session.hpp` | 58 |
| `NetTicket` | Network | `Source/Engine/Network/Ticket.hpp` | 35 |
| `NetTickInputs` | Network | `Source/Engine/Network/NetRollback.hpp` | 110 |
| `NetTickMode` | Network | `Source/Engine/Network/Session.hpp` | 40 |
| `NetTransport` | Network | `Source/Engine/Network/Transport.hpp` | 127 |
| `Outstanding` | Network | `Source/Engine/Network/Transport.hpp` | 100 |
| `Peer` | Network | `Source/Engine/Network/SecureSessionTransport.hpp` | 87 |
| `PendingEvent` | Network | `Source/Engine/Network/Session.hpp` | 279 |
| `PendingState` | Network | `Source/Engine/Network/Replication.hpp` | 129 |
| `Phase` | Network | `Source/Engine/Network/SecureChannel.hpp` | 256 |
| `PhysicsSystem` | Network | `Source/Engine/Network/NetRollback.hpp` | 20 |
| `RateEntry` | Network | `Source/Engine/Network/SecureSessionTransport.hpp` | 120 |
| `Reassembly` | Network | `Source/Engine/Network/Transport.hpp` | 112 |
| `RelevanceBand` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 32 |
| `RelevanceCell` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 71 |
| `RelevanceConfig` | Network | `Source/Engine/Network/NetRollback.hpp` | 34 |
| `RelevanceConfig` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 56 |
| `RelevanceEntity` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 81 |
| `RelevanceGrid` | Network | `Source/Engine/Network/NetRollback.hpp` | 36 |
| `RelevanceGrid` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 131 |
| `RelevanceGridStats` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 121 |
| `RelevancePlan` | Network | `Source/Engine/Network/NetRollback.hpp` | 35 |
| `RelevancePlan` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 93 |
| `ReplicationStatus` | Network | `Source/Engine/Network/Replication.hpp` | 28 |
| `Result` | Network | `Source/Engine/Network/NetRollback.hpp` | 243 |
| `RollbackReport` | Network | `Source/Engine/Network/NetRollback.hpp` | 229 |
| `Room` | Network | `Source/Engine/Network/Lobby.hpp` | 132 |
| `SecureChannel` | Network | `Source/Engine/Network/SecureChannel.hpp` | 198 |
| `SecureChannelConfig` | Network | `Source/Engine/Network/SecureChannel.hpp` | 151 |
| `SecureCookieIssuer` | Network | `Source/Engine/Network/SecureChannel.hpp` | 131 |
| `SecureEndpoint` | Network | `Source/Engine/Network/SecureEndpoint.hpp` | 16 |
| `SecureSessionAuthConfig` | Network | `Source/Engine/Network/SecureSessionTransport.hpp` | 24 |
| `SecureSessionTransport` | Network | `Source/Engine/Network/SecureSessionTransport.hpp` | 38 |
| `SecureStatus` | Network | `Source/Engine/Network/SecureChannel.hpp` | 65 |
| `Slot` | Network | `Source/Engine/Network/NetRollback.hpp` | 155 |
| `SnapshotReceiver` | Network | `Source/Engine/Network/Replication.hpp` | 177 |
| `SnapshotReceiverConfig` | Network | `Source/Engine/Network/Replication.hpp` | 166 |
| `SnapshotSender` | Network | `Source/Engine/Network/Replication.hpp` | 94 |
| `SnapshotSenderConfig` | Network | `Source/Engine/Network/Replication.hpp` | 70 |
| `SnapshotSenderStats` | Network | `Source/Engine/Network/Replication.hpp` | 80 |
| `SortableEntity` | Network | `Source/Engine/Network/RelevanceGrid.hpp` | 195 |
| `TicketStatus` | Network | `Source/Engine/Network/Ticket.hpp` | 54 |
| `TransportChannel` | Network | `Source/Engine/Network/Transport.hpp` | 63 |
| `TransportPeer` | Network | `Source/Engine/Network/Transport.hpp` | 85 |
| `TransportStats` | Network | `Source/Engine/Network/Transport.hpp` | 69 |

### Engine/Physics (54)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `BodyRecord` | Physics | `Source/Engine/Physics/Physics.hpp` | 203 |
| `BodyType` | Physics | `Source/Engine/Physics/Physics.hpp` | 22 |
| `ChainError` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 64 |
| `ChainRuntime` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 247 |
| `ClothComponent` | Physics | `Source/Engine/Physics/Cloth.hpp` | 31 |
| `ClothDesc` | Physics | `Source/Engine/Physics/Cloth.hpp` | 63 |
| `ClothFlags` | Physics | `Source/Engine/Physics/Cloth.hpp` | 20 |
| `ClothHandle` | Physics | `Source/Engine/Physics/Cloth.hpp` | 46 |
| `ClothInfo` | Physics | `Source/Engine/Physics/Cloth.hpp` | 99 |
| `ClothRenderSystem` | Physics | `Source/Engine/Physics/ClothRenderSystem.hpp` | 15 |
| `ClothSystem` | Physics | `Source/Engine/Physics/Cloth.hpp` | 110 |
| `Collider2D` | Physics | `Source/Engine/Physics/Physics.hpp` | 59 |
| `CollisionFilterMatrix` | Physics | `Source/Engine/Physics/CollisionFilter.hpp` | 48 |
| `CollisionLayer` | Physics | `Source/Engine/Physics/CollisionFilter.hpp` | 15 |
| `FluidDesc` | Physics | `Source/Engine/Physics/Fluid.hpp` | 114 |
| `FluidInteractorComponent` | Physics | `Source/Engine/Physics/Fluid.hpp` | 208 |
| `FluidWaveSystem` | Physics | `Source/Engine/Physics/Fluid.hpp` | 219 |
| `GerstnerField` | Physics | `Source/Engine/Physics/Fluid.hpp` | 49 |
| `GerstnerSample` | Physics | `Source/Engine/Physics/Fluid.hpp` | 39 |
| `GerstnerWave` | Physics | `Source/Engine/Physics/Fluid.hpp` | 25 |
| `JointChainComponent` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 158 |
| `JointChainDesc` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 98 |
| `JointChainFlags` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 45 |
| `JointChainHandle` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 139 |
| `JointChainInfo` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 190 |
| `JointChainKind` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 37 |
| `JointLinkComponent` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 17 |
| `ParticleEmitterComponent` | Physics | `Source/Engine/Physics/Particles.hpp` | 26 |
| `ParticleEmitterFlags` | Physics | `Source/Engine/Physics/Particles.hpp` | 14 |
| `ParticleEmitterGpu` | Physics | `Source/Engine/Physics/Particles.hpp` | 61 |
| `ParticleSeedPush` | Physics | `Source/Engine/Physics/Particles.hpp` | 84 |
| `ParticleStateGpu` | Physics | `Source/Engine/Physics/Particles.hpp` | 92 |
| `PhysicsBodyRemapEntry` | Physics | `Source/Engine/Physics/SnapshotRemap.hpp` | 17 |
| `PhysicsBodyRemapTable` | Physics | `Source/Engine/Physics/SnapshotRemap.hpp` | 24 |
| `PhysicsContactEvent` | Physics | `Source/Engine/Physics/Physics.hpp` | 87 |
| `PhysicsDesc` | Physics | `Source/Engine/Physics/Physics.hpp` | 98 |
| `PhysicsEventType` | Physics | `Source/Engine/Physics/Physics.hpp` | 80 |
| `PhysicsJointSystem` | Physics | `Source/Engine/Physics/PhysicsJoints.hpp` | 208 |
| `PhysicsSystem` | Physics | `Source/Engine/Physics/Physics.hpp` | 107 |
| `RigidBody2D` | Physics | `Source/Engine/Physics/Physics.hpp` | 42 |
| `RigidBodyFlags` | Physics | `Source/Engine/Physics/Physics.hpp` | 29 |
| `Runtime` | Physics | `Source/Engine/Physics/Cloth.hpp` | 156 |
| `SdfCollisionDesc` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 45 |
| `SdfCollisionSystem` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 71 |
| `SdfContact` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 59 |
| `SdfField` | Physics | `Source/Engine/Physics/SdfCollision.hpp` | 19 |
| `SdfPrimitiveInfo` | Physics | `Source/Engine/Physics/SdfPrimitives.hpp` | 137 |
| `SdfShape` | Physics | `Source/Engine/Physics/SdfPrimitives.hpp` | 82 |
| `SdfShapeComponent` | Physics | `Source/Engine/Physics/SdfPrimitives.hpp` | 127 |
| `SdfShapeType` | Physics | `Source/Engine/Physics/SdfPrimitives.hpp` | 16 |
| `ShallowWaterGrid` | Physics | `Source/Engine/Physics/Fluid.hpp` | 134 |
| `Storage` | Physics | `Source/Engine/Physics/Cloth.hpp` | 155 |
| `Tracked` | Physics | `Source/Engine/Physics/Fluid.hpp` | 248 |
| `WaterView` | Physics | `Source/Engine/Physics/Fluid.hpp` | 85 |

### Engine/Platform (21)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `AchievementFlags` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 95 |
| `AchievementRequest` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 104 |
| `CloudSaveComponent` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 124 |
| `CloudSaveFlags` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 115 |
| `EgsStoreSystem` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 178 |
| `GogAchievementSystem` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 224 |
| `GogStorageSystem` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 258 |
| `PlatformAccountComponent` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 137 |
| `PlatformCommand` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 35 |
| `PlatformCommandLog` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 46 |
| `PlatformCommandType` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 24 |
| `PlatformLogEvent` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 35 |
| `PlatformLogType` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 26 |
| `PlatformService` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 76 |
| `PlatformServiceCallbacks` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 51 |
| `PlatformServiceDesc` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 71 |
| `PlatformServiceStats` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 59 |
| `PlatformState` | Platform | `Source/Engine/Platform/PlatformService.hpp` | 132 |
| `StagedSave` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 204 |
| `StagedSave` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 291 |
| `SteamAchievementSystem` | Platform | `Source/Engine/Platform/PlatformSystems.hpp` | 148 |

### Engine/RHI (112)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `Api` | RHI | `Source/Engine/RHI/Vulkan/VulkanLoader.hpp` | 113 |
| `Backend` | RHI | `Source/Engine/RHI/RHI.hpp` | 19 |
| `CameraData` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 209 |
| `CameraStackDesc` | RHI | `Source/Engine/RHI/CameraStack.hpp` | 27 |
| `CameraStackFlags` | RHI | `Source/Engine/RHI/CameraStack.hpp` | 17 |
| `CameraStackSystem` | RHI | `Source/Engine/RHI/CameraStack.hpp` | 52 |
| `CameraStackSystem` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 16 |
| `CameraStackSystem` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 12 |
| `ChangeMaskRenderExtract` | RHI | `Source/Engine/RHI/ChangeMaskExtract.hpp` | 33 |
| `ColorGradingConstants` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 116 |
| `ColorGradingPass` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 124 |
| `ColorGradingSettings` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 104 |
| `CookedMaterialView` | RHI | `Source/Engine/RHI/MaterialAsset.hpp` | 152 |
| `DeviceDesc` | RHI | `Source/Engine/RHI/RHI.hpp` | 49 |
| `DeviceInfo` | RHI | `Source/Engine/RHI/RHI.hpp` | 74 |
| `DirtyRange` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 190 |
| `DrawBatch` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 170 |
| `Entry` | RHI | `Source/Engine/RHI/MaterialPipelineCache.hpp` | 74 |
| `ExportStatus` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 100 |
| `Extent2D` | RHI | `Source/Engine/RHI/RHI.hpp` | 40 |
| `ExtractStats` | RHI | `Source/Engine/RHI/ChangeMaskExtract.hpp` | 21 |
| `FrameAccessibility` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 231 |
| `FrameAccessibilityFlags` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 218 |
| `FrameExportDesc` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 53 |
| `FrameExporter` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 112 |
| `FramePacket` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 304 |
| `FrameSink` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 85 |
| `FrameStats` | RHI | `Source/Engine/RHI/RHI.hpp` | 182 |
| `HashEntry` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 197 |
| `KeySorter` | RHI | `Source/Engine/RHI/Sort.hpp` | 14 |
| `LookupEntry` | RHI | `Source/Engine/RHI/ChangeMaskExtract.hpp` | 255 |
| `MaterialAssetError` | RHI | `Source/Engine/RHI/MaterialAsset.hpp` | 42 |
| `MaterialCompileOptions` | RHI | `Source/Engine/RHI/MaterialGraphCompiler.hpp` | 27 |
| `MaterialContract` | RHI | `Source/Engine/RHI/MaterialAsset.hpp` | 83 |
| `MaterialDomain` | RHI | `Source/Engine/RHI/MaterialGraphCompiler.hpp` | 21 |
| `MaterialGraph` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 555 |
| `MaterialGraphCompiler` | RHI | `Source/Engine/RHI/MaterialGraphCompiler.hpp` | 58 |
| `MaterialGraphRequirements` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 542 |
| `MaterialNode` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 117 |
| `MaterialNodeFlags` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 111 |
| `MaterialNodeKind` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 29 |
| `MaterialParam` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 99 |
| `MaterialParamKind` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 88 |
| `MaterialPin` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 129 |
| `MaterialPipelineCache` | RHI | `Source/Engine/RHI/MaterialPipelineCache.hpp` | 38 |
| `MaterialPipelineCacheStats` | RHI | `Source/Engine/RHI/MaterialPipelineCache.hpp` | 18 |
| `MaterialShaderProgram` | RHI | `Source/Engine/RHI/MaterialGraphCompiler.hpp` | 39 |
| `MaterialValueType` | RHI | `Source/Engine/RHI/MaterialGraph.hpp` | 78 |
| `MeshDesc` | RHI | `Source/Engine/RHI/Mesh.hpp` | 102 |
| `MeshDrawData` | RHI | `Source/Engine/RHI/Mesh.hpp` | 70 |
| `MeshDrawFlags` | RHI | `Source/Engine/RHI/Mesh.hpp` | 54 |
| `MeshHandle` | RHI | `Source/Engine/RHI/Mesh.hpp` | 87 |
| `MeshItemFlags` | RHI | `Source/Engine/RHI/Mesh.hpp` | 62 |
| `MeshRenderItem` | RHI | `Source/Engine/RHI/Mesh.hpp` | 114 |
| `MeshSkinRange` | RHI | `Source/Engine/RHI/Mesh.hpp` | 46 |
| `MeshVertex` | RHI | `Source/Engine/RHI/Mesh.hpp` | 17 |
| `NativeTextureView` | RHI | `Source/Engine/RHI/RHI.hpp` | 151 |
| `PageSlot` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 188 |
| `PostFxConstants` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 89 |
| `PostFxFlags` | RHI | `Source/Engine/RHI/PostProcess.hpp` | 10 |
| `PostFxPass` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 145 |
| `PostFxSettings` | RHI | `Source/Engine/RHI/PostProcess.hpp` | 27 |
| `RenderCommandRecord` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 52 |
| `RenderFlags` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 17 |
| `RenderGraph` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 119 |
| `RenderGraphContext` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 69 |
| `RenderGraphPass` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 106 |
| `RenderItem` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 59 |
| `RenderPassDesc` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 94 |
| `RenderPassKind` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 26 |
| `RenderResourceKind` | RHI | `Source/Engine/RHI/RenderGraph.hpp` | 33 |
| `RenderTargetDesc` | RHI | `Source/Engine/RHI/RHI.hpp` | 134 |
| `RenderTargetHandle` | RHI | `Source/Engine/RHI/RHI.hpp` | 140 |
| `RHI` | RHI | `Source/Engine/RHI/RHI.hpp` | 228 |
| `SdfLightingFrame` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 279 |
| `SdfLightingOccluder` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 271 |
| `SdfShadowConstants` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 37 |
| `SdfShadowDesc` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 18 |
| `SdfShadowPass` | RHI | `Source/Engine/RHI/RenderPasses.hpp` | 58 |
| `SequenceFooter` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 42 |
| `SequenceHeader` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 31 |
| `ShaderPipelineDesc` | RHI | `Source/Engine/RHI/RHI.hpp` | 111 |
| `ShaderPipelineDomain` | RHI | `Source/Engine/RHI/RHI.hpp` | 105 |
| `ShaderPipelineHandle` | RHI | `Source/Engine/RHI/RHI.hpp` | 122 |
| `SkinnedVertex` | RHI | `Source/Engine/RHI/Mesh.hpp` | 30 |
| `Slot` | RHI | `Source/Engine/RHI/CameraStack.hpp` | 96 |
| `TextureArrayDesc` | RHI | `Source/Engine/RHI/RHI.hpp` | 90 |
| `TextureArrayHandle` | RHI | `Source/Engine/RHI/RHI.hpp` | 204 |
| `TintEntry` | RHI | `Source/Engine/RHI/ViewportPresenter.hpp` | 81 |
| `TransientRing` | RHI | `Source/Engine/RHI/RingBuffer.hpp` | 15 |
| `UiBackendInfo` | RHI | `Source/Engine/RHI/RHI.hpp` | 159 |
| `ViewportMode` | RHI | `Source/Engine/RHI/ViewportModes.hpp` | 15 |
| `ViewportModeDescriptor` | RHI | `Source/Engine/RHI/ViewportModes.hpp` | 46 |
| `ViewportModeLegend` | RHI | `Source/Engine/RHI/ViewportModes.hpp` | 37 |
| `ViewportPresenter` | RHI | `Source/Engine/RHI/ViewportPresenter.hpp` | 29 |
| `ViewportPresenterStats` | RHI | `Source/Engine/RHI/ViewportPresenter.hpp` | 17 |
| `VirtualFileSystem` | RHI | `Source/Engine/RHI/FrameExporter.hpp` | 15 |
| `VirtualFileSystem` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 16 |
| `VirtualTextureAddress` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 68 |
| `VirtualTextureCamera` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 59 |
| `VirtualTextureDesc` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 33 |
| `VirtualTextureHandle` | RHI | `Source/Engine/RHI/RenderItem.hpp` | 248 |
| `VirtualTexturePageLocation` | RHI | `Source/Engine/RHI/VirtualTextureGpu.hpp` | 11 |
| `VirtualTextureResourceDesc` | RHI | `Source/Engine/RHI/RHI.hpp` | 216 |
| `VirtualTextureRhiTarget` | RHI | `Source/Engine/RHI/VirtualTextureGpu.hpp` | 118 |
| `VirtualTextureSample` | RHI | `Source/Engine/RHI/VirtualTextureGpu.hpp` | 58 |
| `VirtualTextureStats` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 45 |
| `VirtualTextureSystem` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 111 |
| `VirtualTextureTableUpdate` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 101 |
| `VirtualTextureTileId` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 92 |
| `VirtualTextureTileUpload` | RHI | `Source/Engine/RHI/VirtualTextureSystem.hpp` | 77 |
| `VisibleSprite` | RHI | `Source/Engine/RHI/ViewportPresenter.hpp` | 87 |

### Engine/Script (62)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `AotBuilder` | Script | `Source/Engine/Script/AotBuilder.hpp` | 71 |
| `AotBuildOptions` | Script | `Source/Engine/Script/AotBuilder.hpp` | 43 |
| `AotBuildResult` | Script | `Source/Engine/Script/AotBuilder.hpp` | 60 |
| `AotBuildState` | Script | `Source/Engine/Script/AotBuilder.hpp` | 19 |
| `AotGraphTranspileOptions` | Script | `Source/Engine/Script/AotGraphCompiler.hpp` | 13 |
| `AotHost` | Script | `Source/Engine/Script/AotHost.hpp` | 32 |
| `AotHost` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 21 |
| `AotModule` | Script | `Source/Engine/Script/AotModule.hpp` | 22 |
| `AotProgramRef` | Script | `Source/Engine/Script/AotHost.hpp` | 22 |
| `AotTranspileOptions` | Script | `Source/Engine/Script/AotTranspiler.hpp` | 12 |
| `BoundProgram` | Script | `Source/Engine/Script/EcsBridge.hpp` | 101 |
| `Code` | Script | `Source/Engine/Script/Vm.hpp` | 25 |
| `CompiledDebug` | Script | `Source/Engine/Script/Bytecode.hpp` | 452 |
| `CompiledProgram` | Script | `Source/Engine/Script/Bytecode.hpp` | 467 |
| `ComponentAccessor` | Script | `Source/Engine/Script/EcsBridge.hpp` | 17 |
| `ComponentBinding` | Script | `Source/Engine/Script/Bytecode.hpp` | 396 |
| `Context` | Script | `Source/Engine/Script/AotAbi.hpp` | 98 |
| `DebugField` | Script | `Source/Engine/Script/ScriptDebug.hpp` | 123 |
| `DebugFrame` | Script | `Source/Engine/Script/ScriptDebug.hpp` | 103 |
| `DebugSectionHeader` | Script | `Source/Engine/Script/Bytecode.hpp` | 145 |
| `DebugSpan` | Script | `Source/Engine/Script/Bytecode.hpp` | 123 |
| `DebugSpanFlags` | Script | `Source/Engine/Script/Bytecode.hpp` | 114 |
| `DebugVariable` | Script | `Source/Engine/Script/Bytecode.hpp` | 136 |
| `EcsBridge` | Script | `Source/Engine/Script/EcsBridge.hpp` | 26 |
| `FieldBinding` | Script | `Source/Engine/Script/Bytecode.hpp` | 406 |
| `FunctionEntry` | Script | `Source/Engine/Script/Bytecode.hpp` | 419 |
| `HostApi` | Script | `Source/Engine/Script/AotAbi.hpp` | 54 |
| `Instruction` | Script | `Source/Engine/Script/Bytecode.hpp` | 376 |
| `LogicCompareOp` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 22 |
| `LogicCompileOptions` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 669 |
| `LogicCompileResult` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 676 |
| `LogicFieldBinding` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 90 |
| `LogicGraph` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 319 |
| `LogicLink` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 308 |
| `LogicMathOp` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 56 |
| `LogicNode` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 290 |
| `LogicNodeKind` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 111 |
| `Module` | Script | `Source/Engine/Script/AotAbi.hpp` | 293 |
| `NativeId` | Script | `Source/Engine/Script/Bytecode.hpp` | 18 |
| `OpCode` | Script | `Source/Engine/Script/Bytecode.hpp` | 194 |
| `PendingField` | Script | `Source/Engine/Script/Bytecode.hpp` | 533 |
| `PinType` | Script | `Source/Engine/Script/GraphCompiler.hpp` | 275 |
| `ProgramBuilder` | Script | `Source/Engine/Script/Bytecode.hpp` | 498 |
| `ProgramHeader` | Script | `Source/Engine/Script/Bytecode.hpp` | 429 |
| `ResolvedComponent` | Script | `Source/Engine/Script/EcsBridge.hpp` | 93 |
| `ScriptComponent` | Script | `Source/Engine/Script/ScriptComponent.hpp` | 16 |
| `ScriptEvent` | Script | `Source/Engine/Script/Bytecode.hpp` | 169 |
| `ScriptRuntime` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 20 |
| `ScriptRuntime` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 24 |
| `ScriptVm` | Script | `Source/Engine/Script/Vm.hpp` | 101 |
| `ScriptVm` | Script | `Source/Engine/Script/Vm.hpp` | 215 |
| `Slot` | Script | `Source/Engine/Script/AotHost.hpp` | 94 |
| `Slot` | Script | `Source/Engine/Script/ScriptSystem.hpp` | 112 |
| `SourceLocation` | Script | `Source/Engine/Script/ScriptDebug.hpp` | 17 |
| `TextCompileOptions` | Script | `Source/Engine/Script/TextScript.hpp` | 14 |
| `Vector2D` | Script | `Source/Engine/Script/AotAbi.hpp` | 28 |
| `VmContext` | Script | `Source/Engine/Script/Vm.hpp` | 87 |
| `VmDebugState` | Script | `Source/Engine/Script/Vm.hpp` | 140 |
| `VmFault` | Script | `Source/Engine/Script/Vm.hpp` | 23 |
| `VmHalt` | Script | `Source/Engine/Script/Vm.hpp` | 113 |
| `VmPauseReason` | Script | `Source/Engine/Script/Vm.hpp` | 104 |
| `VmStopInfo` | Script | `Source/Engine/Script/Vm.hpp` | 120 |

### Engine/Text (67)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `AssetSlot` | Text | `Source/Engine/Text/TextStore.hpp` | 218 |
| `ChainedShaper` | Text | `Source/Engine/Text/TextShaper.hpp` | 133 |
| `CodepointShaper` | Text | `Source/Engine/Text/TextShaper.hpp` | 99 |
| `Entry` | Text | `Source/Engine/Text/TextShaper.hpp` | 247 |
| `FontCandidate` | Text | `Source/Engine/Text/TextFontStack.hpp` | 24 |
| `FontSlot` | Text | `Source/Engine/Text/TextFontStack.hpp` | 59 |
| `FormatEntry` | Text | `Source/Engine/Text/TextService.hpp` | 583 |
| `FormattedLayoutEntry` | Text | `Source/Engine/Text/TextService.hpp` | 566 |
| `FormattedLayoutKey` | Text | `Source/Engine/Text/TextService.hpp` | 556 |
| `HarfBuzzShaper` | Text | `Source/Engine/Text/TextShaper.hpp` | 110 |
| `Impl` | Text | `Source/Engine/Text/TextShaper.hpp` | 128 |
| `ITextShaper` | Text | `Source/Engine/Text/TextShaper.hpp` | 89 |
| `LayoutEntry` | Text | `Source/Engine/Text/TextService.hpp` | 548 |
| `LayoutKey` | Text | `Source/Engine/Text/TextService.hpp` | 528 |
| `MappedTextAsset` | Text | `Source/Engine/Text/TextAssetFile.hpp` | 17 |
| `MarkupStackEntry` | Text | `Source/Engine/Text/TextMarkup.hpp` | 71 |
| `MarkupVariantEntry` | Text | `Source/Engine/Text/TextService.hpp` | 574 |
| `ScriptBackend` | Text | `Source/Engine/Text/TextShapeSystem.hpp` | 111 |
| `ScriptShaper` | Text | `Source/Engine/Text/TextShaper.hpp` | 207 |
| `ShapedGlyph` | Text | `Source/Engine/Text/TextShaper.hpp` | 56 |
| `ShapedGlyphHit` | Text | `Source/Engine/Text/TextShapedLayout.hpp` | 153 |
| `ShapedLayoutResult` | Text | `Source/Engine/Text/TextShapedLayout.hpp` | 26 |
| `ShapedLine` | Text | `Source/Engine/Text/TextShapedLayout.hpp` | 18 |
| `StyledAsset` | Text | `Source/Engine/Text/TextService.hpp` | 595 |
| `TextAction` | Text | `Source/Engine/Text/TextStyle.hpp` | 61 |
| `TextActionKind` | Text | `Source/Engine/Text/TextStyle.hpp` | 51 |
| `TextAnimationParams` | Text | `Source/Engine/Text/TextRender.hpp` | 29 |
| `TextAssetBlock` | Text | `Source/Engine/Text/TextAssetFormat.hpp` | 34 |
| `TextAssetHeader` | Text | `Source/Engine/Text/TextAssetFormat.hpp` | 20 |
| `TextAssetRef` | Text | `Source/Engine/Text/TextAsset.hpp` | 14 |
| `TextAssetStatus` | Text | `Source/Engine/Text/TextAssetFormat.hpp` | 63 |
| `TextAssetView` | Text | `Source/Engine/Text/TextAssetFormat.hpp` | 42 |
| `TextBlock` | Text | `Source/Engine/Text/TextAsset.hpp` | 37 |
| `TextDirection` | Text | `Source/Engine/Text/TextShaper.hpp` | 21 |
| `TextFontStack` | Text | `Source/Engine/Text/TextFontStack.hpp` | 54 |
| `TextFormatArg` | Text | `Source/Engine/Text/TextFormat.hpp` | 45 |
| `TextFormatStatus` | Text | `Source/Engine/Text/TextFormat.hpp` | 33 |
| `TextGradientParams` | Text | `Source/Engine/Text/TextRender.hpp` | 22 |
| `TextInlineHit` | Text | `Source/Engine/Text/TextLayout.hpp` | 203 |
| `TextInlineKind` | Text | `Source/Engine/Text/TextStyle.hpp` | 73 |
| `TextInlineObject` | Text | `Source/Engine/Text/TextStyle.hpp` | 83 |
| `TextInlineSprite` | Text | `Source/Engine/Text/TextStyle.hpp` | 99 |
| `TextLayoutParams` | Text | `Source/Engine/Text/TextLayout.hpp` | 53 |
| `TextLayoutResult` | Text | `Source/Engine/Text/TextLayout.hpp` | 71 |
| `TextLine` | Text | `Source/Engine/Text/TextLayout.hpp` | 21 |
| `TextMarkupStatus` | Text | `Source/Engine/Text/TextMarkup.hpp` | 16 |
| `TextRenderEffects` | Text | `Source/Engine/Text/TextRender.hpp` | 44 |
| `TextRenderParams` | Text | `Source/Engine/Text/TextRender.hpp` | 57 |
| `TextRenderStats` | Text | `Source/Engine/Text/TextRender.hpp` | 76 |
| `TextRenderSystemStats` | Text | `Source/Engine/Text/TextRenderSystem.hpp` | 19 |
| `TextRun` | Text | `Source/Engine/Text/TextShaper.hpp` | 47 |
| `TextRuntime` | Text | `Source/Engine/Text/TextRuntime.hpp` | 17 |
| `TextScript` | Text | `Source/Engine/Text/TextShaper.hpp` | 30 |
| `TextService` | Text | `Source/Engine/Text/TextService.hpp` | 18 |
| `TextShapeCache` | Text | `Source/Engine/Text/TextShapeCache.hpp` | 59 |
| `TextShapeHandle` | Text | `Source/Engine/Text/TextShapeCache.hpp` | 19 |
| `TextShapeKey` | Text | `Source/Engine/Text/TextShapeCache.hpp` | 32 |
| `TextShapeParams` | Text | `Source/Engine/Text/TextShaper.hpp` | 71 |
| `TextShapeResult` | Text | `Source/Engine/Text/TextShaper.hpp` | 82 |
| `TextShapeSystemStats` | Text | `Source/Engine/Text/TextShapeSystem.hpp` | 16 |
| `TextShapeView` | Text | `Source/Engine/Text/TextShapeCache.hpp` | 50 |
| `TextSpan` | Text | `Source/Engine/Text/TextStyle.hpp` | 40 |
| `TextStore` | Text | `Source/Engine/Text/TextStore.hpp` | 19 |
| `TextStyle` | Text | `Source/Engine/Text/TextStyle.hpp` | 27 |
| `TextStyledBlock` | Text | `Source/Engine/Text/TextMarkup.hpp` | 30 |
| `TextStyleFlags` | Text | `Source/Engine/Text/TextStyle.hpp` | 12 |
| `VerticalFormEntry` | Text | `Source/Engine/Text/TextVertical.hpp` | 16 |

### Engine/UI (101)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `AccessibilityEditorSettings` | UI | `Source/Engine/UI/Accessibility.hpp` | 250 |
| `AccessibilityFlags` | UI | `Source/Engine/UI/Accessibility.hpp` | 208 |
| `AccessibilityKernel` | UI | `Source/Engine/UI/Accessibility.hpp` | 304 |
| `AccessibilityOverlay` | UI | `Source/Engine/UI/AccessibilityOverlay.hpp` | 30 |
| `AccessibilityPresentationSettings` | UI | `Source/Engine/UI/Accessibility.hpp` | 233 |
| `AccessibilityProfile` | UI | `Source/Engine/UI/Accessibility.hpp` | 272 |
| `AccessibilitySettings` | UI | `Source/Engine/UI/Accessibility.hpp` | 217 |
| `AudioSystem` | UI | `Source/Engine/UI/Accessibility.hpp` | 26 |
| `AvSpeechTts` | UI | `Source/Engine/UI/Tts.hpp` | 60 |
| `BuildStatus` | UI | `Source/Engine/UI/PinkUI.hpp` | 1440 |
| `CardBoardComponent` | UI | `Source/Engine/UI/CardBoard.hpp` | 49 |
| `CardBoardSystem` | UI | `Source/Engine/UI/CardBoard.hpp` | 74 |
| `ColorMatrix3` | UI | `Source/Engine/UI/Accessibility.hpp` | 42 |
| `ColorVisionDeficiency` | UI | `Source/Engine/UI/Accessibility.hpp` | 34 |
| `Dropdown` | UI | `Source/Engine/UI/PinkUI.hpp` | 534 |
| `FlashGuard` | UI | `Source/Engine/UI/Accessibility.hpp` | 59 |
| `FocusDirection` | UI | `Source/Engine/UI/PinkUI.hpp` | 211 |
| `FocusPolicy` | UI | `Source/Engine/UI/PinkUI.hpp` | 187 |
| `FontAtlas` | UI | `Source/Engine/UI/FontAtlas.hpp` | 101 |
| `FontAtlasHeader` | UI | `Source/Engine/UI/FontAtlas.hpp` | 45 |
| `FontAtlasSet` | UI | `Source/Engine/UI/FontAtlas.hpp` | 167 |
| `FontBuildOptions` | UI | `Source/Engine/UI/FontAtlas.hpp` | 25 |
| `FontBuildOptions` | UI | `Source/Engine/UI/FontBuilder.hpp` | 15 |
| `FontCoverage` | UI | `Source/Engine/UI/FontCoverage.hpp` | 21 |
| `FontFamilyFile` | UI | `Source/Engine/UI/FontAtlas.hpp` | 74 |
| `FontFamilyManifest` | UI | `Source/Engine/UI/FontFamilyAsset.hpp` | 17 |
| `FontPixelFormat` | UI | `Source/Engine/UI/FontAtlas.hpp` | 37 |
| `FontStyle` | UI | `Source/Engine/UI/FontAtlas.hpp` | 65 |
| `FrameOutput` | UI | `Source/Engine/UI/PinkUI.hpp` | 1418 |
| `GlyphRecord` | UI | `Source/Engine/UI/FontAtlas.hpp` | 80 |
| `GridView` | UI | `Source/Engine/UI/PinkUI.hpp` | 424 |
| `HyperlinkRange` | UI | `Source/Engine/UI/Hypertext.hpp` | 51 |
| `HypertextComponent` | UI | `Source/Engine/UI/Hypertext.hpp` | 32 |
| `HypertextFlags` | UI | `Source/Engine/UI/Hypertext.hpp` | 21 |
| `HypertextPool` | UI | `Source/Engine/UI/Hypertext.hpp` | 74 |
| `HypertextView` | UI | `Source/Engine/UI/Hypertext.hpp` | 67 |
| `ITextShaper` | UI | `Source/Engine/UI/PinkUI.hpp` | 33 |
| `LayoutCache` | UI | `Source/Engine/UI/PinkUI.hpp` | 736 |
| `LazyFontFamily` | UI | `Source/Engine/UI/FontFamilyAsset.hpp` | 101 |
| `ListView` | UI | `Source/Engine/UI/PinkUI.hpp` | 560 |
| `LocalizationTable` | UI | `Source/Engine/UI/Accessibility.hpp` | 24 |
| `LocalizationTable` | UI | `Source/Engine/UI/Hypertext.hpp` | 16 |
| `LocalizationTable` | UI | `Source/Engine/UI/PinkUI.hpp` | 39 |
| `NarrativeArchetype` | UI | `Source/Engine/UI/NarrativeTree.hpp` | 22 |
| `NarrativeEvent` | UI | `Source/Engine/UI/Accessibility.hpp` | 83 |
| `NarrativeGroup` | UI | `Source/Engine/UI/NarrativeTree.hpp` | 36 |
| `NarrativeLocale` | UI | `Source/Engine/UI/NarrativeTree.hpp` | 16 |
| `NarrativeLog` | UI | `Source/Engine/UI/Accessibility.hpp` | 99 |
| `NarrativeNode` | UI | `Source/Engine/UI/NarrativeTree.hpp` | 46 |
| `NarrativeTree` | UI | `Source/Engine/UI/NarrativeTree.hpp` | 54 |
| `OledShiftState` | UI | `Source/Engine/UI/PinkUI.hpp` | 463 |
| `PinkSdfDraw` | UI | `Source/Engine/UI/PinkSdf.hpp` | 74 |
| `PinkSdfFlags` | UI | `Source/Engine/UI/PinkSdf.hpp` | 24 |
| `PinkSdfGpuPrimitive` | UI | `Source/Engine/UI/PinkSdf.hpp` | 51 |
| `PinkSdfPrimitive` | UI | `Source/Engine/UI/PinkSdf.hpp` | 33 |
| `PinkSdfShape` | UI | `Source/Engine/UI/PinkSdf.hpp` | 15 |
| `RadialProgress` | UI | `Source/Engine/UI/PinkUI.hpp` | 584 |
| `RetainedFont` | UI | `Source/Engine/UI/FontAtlas.hpp` | 293 |
| `SapiTts` | UI | `Source/Engine/UI/Tts.hpp` | 30 |
| `ScrollView` | UI | `Source/Engine/UI/PinkUI.hpp` | 491 |
| `SlotComponent` | UI | `Source/Engine/UI/CardBoard.hpp` | 27 |
| `SlotFlags` | UI | `Source/Engine/UI/CardBoard.hpp` | 15 |
| `SlotLayout` | UI | `Source/Engine/UI/CardBoard.hpp` | 65 |
| `SoundCueCategory` | UI | `Source/Engine/UI/SoundCues.hpp` | 26 |
| `SoundCueDef` | UI | `Source/Engine/UI/SoundCues.hpp` | 62 |
| `SoundCueRegistry` | UI | `Source/Engine/UI/SoundCues.hpp` | 71 |
| `SoundTrigger` | UI | `Source/Engine/UI/Accessibility.hpp` | 128 |
| `StateTrack` | UI | `Source/Engine/UI/Accessibility.hpp` | 411 |
| `SubtitleLine` | UI | `Source/Engine/UI/Accessibility.hpp` | 139 |
| `SubtitleQueue` | UI | `Source/Engine/UI/Accessibility.hpp` | 163 |
| `SystemContext` | UI | `Source/Engine/UI/PinkUI.hpp` | 28 |
| `TabBar` | UI | `Source/Engine/UI/PinkUI.hpp` | 515 |
| `TextAlign` | UI | `Source/Engine/UI/PinkUI.hpp` | 222 |
| `TextShapeParams` | UI | `Source/Engine/UI/PinkUI.hpp` | 34 |
| `TextVerticalAlign` | UI | `Source/Engine/UI/PinkUI.hpp` | 229 |
| `TimedSoundCue` | UI | `Source/Engine/UI/Accessibility.hpp` | 152 |
| `TooltipPlacer` | UI | `Source/Engine/UI/PinkUI.hpp` | 595 |
| `TtsBackend` | UI | `Source/Engine/UI/Accessibility.hpp` | 27 |
| `TtsBackend` | UI | `Source/Engine/UI/Tts.hpp` | 19 |
| `TtsPriority` | UI | `Source/Engine/UI/Tts.hpp` | 11 |
| `UiPalette` | UI | `Source/Engine/UI/UiTheme.hpp` | 41 |
| `UiPaletteSlot` | UI | `Source/Engine/UI/UiTheme.hpp` | 18 |
| `UiThemeAsset` | UI | `Source/Engine/UI/UiTheme.hpp` | 46 |
| `VirtualFileSystem` | UI | `Source/Engine/UI/FontAtlas.hpp` | 19 |
| `VoiceCaptionEvent` | UI | `Source/Engine/UI/Accessibility.hpp` | 193 |
| `VoiceCaptionProvider` | UI | `Source/Engine/UI/Accessibility.hpp` | 201 |
| `WidgetCanvas` | UI | `Source/Engine/UI/Accessibility.hpp` | 30 |
| `WidgetCanvas` | UI | `Source/Engine/UI/PinkUI.hpp` | 290 |
| `WidgetCanvas` | UI | `Source/Engine/UI/PinkUI.hpp` | 483 |
| `WidgetCanvas` | UI | `Source/Engine/UI/PinkUI.hpp` | 607 |
| `WidgetCanvasSystem` | UI | `Source/Engine/UI/PinkUI.hpp` | 1453 |
| `WidgetComponent` | UI | `Source/Engine/UI/PinkUI.hpp` | 251 |
| `WidgetComponentFlags` | UI | `Source/Engine/UI/PinkUI.hpp` | 263 |
| `WidgetDesc` | UI | `Source/Engine/UI/PinkUI.hpp` | 309 |
| `WidgetFlags` | UI | `Source/Engine/UI/PinkUI.hpp` | 145 |
| `WidgetHandle` | UI | `Source/Engine/UI/PinkUI.hpp` | 80 |
| `WidgetHyperlink` | UI | `Source/Engine/UI/PinkUI.hpp` | 300 |
| `WidgetKind` | UI | `Source/Engine/UI/PinkUI.hpp` | 99 |
| `WidgetLayoutAsset` | UI | `Source/Engine/UI/WidgetLayoutAsset.hpp` | 15 |
| `WidgetPaintContext` | UI | `Source/Engine/UI/PinkUI.hpp` | 291 |
| `WidgetPaintContext` | UI | `Source/Engine/UI/PinkUI.hpp` | 391 |

### Engine/Video (20)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `AssetDatabase` | Video | `Source/Engine/Video/VideoStream.hpp` | 23 |
| `ClusterScan` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 108 |
| `ContainerKind` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 150 |
| `DecodeStatus` | Video | `Source/Engine/Video/VideoTypes.hpp` | 29 |
| `DemuxStatus` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 52 |
| `Element` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 85 |
| `FrameBuffer` | Video | `Source/Engine/Video/VideoPlayer.hpp` | 118 |
| `FrameSlot` | Video | `Source/Engine/Video/VideoPlayer.hpp` | 125 |
| `MappedFile` | Video | `Source/Engine/Video/VideoPlayer.hpp` | 22 |
| `RHI` | Video | `Source/Engine/Video/VideoPlayer.hpp` | 27 |
| `VideoPlayer` | Video | `Source/Engine/Video/VideoPlayer.hpp` | 33 |
| `VideoStreamComponent` | Video | `Source/Engine/Video/VideoStream.hpp` | 31 |
| `VideoStreamFlags` | Video | `Source/Engine/Video/VideoTypes.hpp` | 20 |
| `VideoSystem` | Video | `Source/Engine/Video/VideoStream.hpp` | 49 |
| `VirtualFileSystem` | Video | `Source/Engine/Video/VideoStream.hpp` | 20 |
| `Vp9Backend` | Video | `Source/Engine/Video/Vp9Backend.hpp` | 30 |
| `WebmDemuxer` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 60 |
| `WebmFrame` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 38 |
| `WebmTrackInfo` | Video | `Source/Engine/Video/WebmDemuxer.hpp` | 14 |
| `YuvFrameView` | Video | `Source/Engine/Video/Vp9Backend.hpp` | 12 |

### Engine/Weather (12)

| Symbol | Kind | Header | Line |
|---|---|---|---|
| `ClothSystem` | Weather | `Source/Engine/Weather/Weather.hpp` | 13 |
| `WeatherAtmospherePass` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 128 |
| `WeatherContextComponent` | Weather | `Source/Engine/Weather/Weather.hpp` | 32 |
| `WeatherDesc` | Weather | `Source/Engine/Weather/Weather.hpp` | 91 |
| `WeatherFlags` | Weather | `Source/Engine/Weather/Weather.hpp` | 19 |
| `WeatherGpuConstants` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 46 |
| `WeatherGpuResources` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 18 |
| `WeatherGpuStats` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 68 |
| `WeatherPreconciliatedDesc` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 32 |
| `WeatherPreconciliatedSystem` | Weather | `Source/Engine/Weather/WeatherPreconciliated.hpp` | 78 |
| `WeatherSystem` | Weather | `Source/Engine/Weather/Weather.hpp` | 99 |
| `WeatherWindField` | Weather | `Source/Engine/Weather/Weather.hpp` | 75 |

## 21. Appendix B - macros and platform defines

| Macro | Header | Line |
|---|---|---|
| `FBY_AOT_EXPORT` | `Source/Engine/Script/AotAbi.hpp` | 310 |
| `FBY_AOT_EXPORT` | `Source/Engine/Script/AotAbi.hpp` | 312 |
| `FBY_ASSERT` | `Source/Engine/Core/Assert.hpp` | 13 |
| `FBY_ASSERT` | `Source/Engine/Core/Assert.hpp` | 31 |
| `FBY_ASSERT_MSG` | `Source/Engine/Core/Assert.hpp` | 22 |
| `FBY_ASSERT_MSG` | `Source/Engine/Core/Assert.hpp` | 32 |
| `FBY_DEFINE_PLUGIN` | `Source/Engine/Core/Plugin.hpp` | 56 |
| `FBY_DETAIL_CONCAT` | `Source/Engine/Core/Definitions.hpp` | 261 |
| `FBY_DETAIL_CONCAT_INNER` | `Source/Engine/Core/Definitions.hpp` | 260 |
| `FBY_FIELD` | `Source/Engine/Core/Mirror.hpp` | 279 |
| `FBY_FLUID_HAS_MDSPAN` | `Source/Engine/Physics/Fluid.hpp` | 16 |
| `FBY_FLUID_HAS_MDSPAN` | `Source/Engine/Physics/Fluid.hpp` | 18 |
| `FBY_HAS_MDSPAN` | `Source/Engine/ECS/Skeleton.hpp` | 17 |
| `FBY_HAS_MDSPAN` | `Source/Engine/ECS/Skeleton.hpp` | 19 |
| `FBY_HAS_STATIC_REFLECTION` | `Source/Engine/Core/Mirror.hpp` | 97 |
| `FBY_HAS_STATIC_REFLECTION` | `Source/Engine/Core/Mirror.hpp` | 100 |
| `FBY_HAS_STATIC_REFLECTION` | `Source/Engine/Core/Mirror.hpp` | 104 |
| `FBY_HAS_STD_FLAT_MAP` | `Source/Engine/Core/FlatMap.hpp` | 17 |
| `FBY_HAS_STD_FLAT_MAP` | `Source/Engine/Core/FlatMap.hpp` | 19 |
| `FBY_PLATFORM_ABI_VERSION` | `Source/Plugins/PlatformService/PlatformAPI.h` | 11 |
| `FBY_PLATFORM_CALL` | `Source/Plugins/PlatformService/PlatformAPI.h` | 15 |
| `FBY_PLATFORM_CALL` | `Source/Plugins/PlatformService/PlatformAPI.h` | 18 |
| `FBY_PLATFORM_EXPORT` | `Source/Plugins/PlatformService/PlatformAPI.h` | 14 |
| `FBY_PLATFORM_EXPORT` | `Source/Plugins/PlatformService/PlatformAPI.h` | 17 |
| `FBY_PLUGIN_ABI_VERSION` | `Source/Engine/Core/PluginAPI.h` | 28 |
| `FBY_PLUGIN_API_H` | `Source/Engine/Core/PluginAPI.h` | 18 |
| `FBY_PLUGIN_COMPONENT` | `Source/Engine/Core/Plugin.hpp` | 53 |
| `FBY_PLUGIN_ENTRY` | `Source/Engine/Core/PluginAPI.h` | 115 |
| `FBY_PLUGIN_EXPORT` | `Source/Engine/Core/PluginAPI.h` | 31 |
| `FBY_PLUGIN_EXPORT` | `Source/Engine/Core/PluginAPI.h` | 33 |
| `FBY_PLUGIN_EXPORT` | `Source/Engine/Core/PluginAPI.h` | 35 |
| `FBY_PLUGIN_INFO_SYMBOL` | `Source/Engine/Core/PluginAPI.h` | 107 |
| `FBY_PLUGIN_LOAD_SYMBOL` | `Source/Engine/Core/PluginAPI.h` | 108 |
| `FBY_PLUGIN_UNLOAD_SYMBOL` | `Source/Engine/Core/PluginAPI.h` | 109 |
| `FBY_PROJECT_ASSETS` | `Source/Engine/Assets/ProjectScaffold.hpp` | 123 |
| `FBY_REFLECT_COMPONENT` | `Source/Engine/Core/Mirror.hpp` | 286 |
| `FBY_REFLECT_COMPONENT_EMPTY` | `Source/Engine/Core/Mirror.hpp` | 298 |
| `FBY_REGISTER_TYPE` | `Source/Engine/Core/Definitions.hpp` | 264 |
| `FBY_RELEVANCE_HAS_MDSPAN` | `Source/Engine/Network/RelevanceGrid.hpp` | 19 |
| `FBY_RELEVANCE_HAS_MDSPAN` | `Source/Engine/Network/RelevanceGrid.hpp` | 21 |
| `FBY_RESTRICT` | `Source/Engine/Core/Definitions.hpp` | 26 |
| `FBY_RESTRICT` | `Source/Engine/Core/Definitions.hpp` | 28 |
| `FBY_SDF_SHAPE_FIELD` | `Source/Engine/Physics/SdfPrimitives.hpp` | 172 |
| `FBY_TYPE_NAME` | `Source/Engine/Core/Definitions.hpp` | 280 |
| `FBY_UI_HAS_MDSPAN` | `Source/Engine/UI/PinkUI.hpp` | 21 |
| `FBY_UI_HAS_MDSPAN` | `Source/Engine/UI/PinkUI.hpp` | 23 |
| `FBY_UNREACHABLE` | `Source/Engine/Core/Assert.hpp` | 45 |
| `FBY_VERIFY` | `Source/Engine/Core/Assert.hpp` | 36 |
| `FBY_VK_DECLARE` | `Source/Engine/RHI/Vulkan/VulkanLoader.hpp` | 117 |
| `FBY_VK_DEVICE_FUNCTIONS` | `Source/Engine/RHI/Vulkan/VulkanLoader.hpp` | 36 |
| `FBY_VK_GLOBAL_FUNCTIONS` | `Source/Engine/RHI/Vulkan/VulkanLoader.hpp` | 15 |
| `FBY_VK_INSTANCE_FUNCTIONS` | `Source/Engine/RHI/Vulkan/VulkanLoader.hpp` | 20 |
| `FORCEINLINE` | `Source/Engine/Core/Definitions.hpp` | 17 |
| `FORCEINLINE` | `Source/Engine/Core/Definitions.hpp` | 19 |
| `FORCEINLINE` | `Source/Engine/Core/Definitions.hpp` | 21 |

---

Generated by Documentation/Tools/GenerateAPIReference.py. Do not edit this file by hand: change the generator and regenerate.
