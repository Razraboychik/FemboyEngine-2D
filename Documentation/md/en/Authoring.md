# FemboyEngine2D — P1 Authoring Toolset & IDE Foundations

P1 turns the editor from a viewer into an authoring environment. The runtime
models stay fixed-capacity and allocation-free; the editor layer adds
transactions, metadata and OS integration. This document maps the code.

## 1. Timeline & Curve editor

| Piece | File |
| --- | --- |
| Authored timeline data (tracks, keys, Bezier handles, events) | `Source/Engine/ECS/AnimationTimeline.hpp`, `Source/Engine/ECS/AnimationTimeline.cpp` |
| ImGui/ImPlot panel | `Source/Editor/Panels/AnimationTimelinePanel.hpp`, `Source/Editor/Panels/AnimationTimelinePanel.cpp` |
| Hosting + Ctrl+Z/Y | `EditorSystem::TimelinePanel()`, `EditorSystem::Commands()` |

* Track kinds: `BonePosition`, `BoneRotation`, `BoneScale` write
  `SkeletonComponent` local pose and run `SkeletalAnimation::Evaluate`; `SpriteAnimationClip` selects
  `startFrame/frameCount/currentFrame` on the fixed-step flipbook. Tracks are
  addressed by bone-name hash (`BoneNameHash`) or bone index.
* Interpolation: step, linear and cubic Bezier. Bezier keys store in/out handles
  in (time, value) space; evaluation solves x by fixed-iteration bisection so the
  play/scrub path allocates nothing.
* Events: `TimelineEvent` at a time with `Audio` (audio AssetId + volume) or
  `Script` (program handle + entity). `DispatchEvents(prev, time, looped, sink)`
  fires exactly the crossed events; the panel adapts them to `AudioSystem::Play`
  and `RunScriptOnce` through plain function pointers.
* Tooling: ruler + zoom/scroll, track selection, key drag, key inspector
  (time/value/interpolation/tangents), event authoring lane.

## 2. `.femeta` metadata and hot reload

| Piece | File |
| --- | --- |
| Metadata payload, `.femeta` TOML parse/write, GUID | `Source/Engine/Assets/AssetMeta.hpp`, `Source/Engine/Assets/AssetMeta.cpp` |
| Transactional table, handles, reload notices | `Source/Engine/Assets/AssetDatabase.hpp`, `Source/Engine/Assets/AssetDatabase.cpp` |
| OS watcher, cook/reload orchestration | `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.hpp`, `Source/Editor/Infrastructure/AssetDatabaseWatchSystem.cpp` |

### Handle model

```
AssetHandle { uint32 slot; uint32 generation; }   // stored in components
AssetMeta   { guid, asset_id, kind, source, cooked,
              source_hash, cooked_hash, import settings,
              content_generation, dependencies }
```

Components migrated in P1: `SkeletonAssetRef`, `SpriteAnimationComponent`, `VideoStreamComponent`
(`media` handle). `VideoSystem::OpenFromAsset` resolves the row's source path.

### Transactions

```
database.BeginTransaction();
database.StageMeta(handle, newMeta);   // readers still see old bytes
database.CommitTransaction();          // atomic swap + reload notice
```

A commit bumps `AssetMeta::contentGeneration` but **not** the handle's
generation: every component referencing the asset stays valid while the RHI
texture array / video texture / animation data is replaced.

### Watch + hot reload

`AssetDatabaseWatchSystem` uses `ReadDirectoryChangesW` (Windows) or a poll
fallback, filters PNG/TGA/WAV/OGG/Opus/WebM/`.femeta`, and for a changed source:

1. resolves the handle by source path;
2. calls the host `cookAndStage` callback (fby_fempak / texture cook / audio cook);
3. stages + commits the new metadata;
4. calls the host `reload` callback to upload the cooked bytes in place.

The OS thread only enqueues fixed-size paths; all database/RHI mutation happens
in `Poll()` on the editor thread. `InjectChange()` is the test seam.

## 3. Command stack (Undo/Redo) and PIE

| Piece | File |
| --- | --- |
| Transactions, sparse byte-delta compression, observers | `Source/Editor/Infrastructure/CommandStack.hpp`, `Source/Editor/Infrastructure/CommandStack.cpp` |
| Registry clone + simulation contour | `Source/Editor/Infrastructure/PlayInEditor.hpp`, `Source/Editor/Infrastructure/PlayInEditor.cpp` |
| Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z | `EditorSystem::Tick` |

* Inspector/gizmo edit flow:

  ```
  stack.BeginTransaction("Move bone");
  const auto before = stack.CaptureComponent(entity, codec, scratch);
  ... mutate component / drag ImGuizmo ...
  stack.RecordAfterMutation(entity, codec, before);
  stack.CommitTransaction();
  ```

* A committed transaction is one undo step. Payload stores only changed byte
  ranges (old+new) — the Snapshot-v2 delta idea at component granularity. A
  20 KB `SkeletonComponent` rotation edit compresses to < 512 bytes.
* `ApplyObserver` lets the editor re-run `SkeletalAnimation::Evaluate` after undo of a bone
  edit. Fixed capacity (512 commands, 4 MiB payload) is configured once.

### PIE

`PlayInEditor` captures the editor registry with Snapshot v2 into a fixed
FrameArena, restores it into a **second** `entt::registry`, and steps the same
`SystemGraph` against the clone. Stop discards the clone; the editor world was
never modified. `CaptureDelta()` exposes the rollback delta of the play world for
debug/rewind. Derived components (Skeleton/Skin matrices) are rebuilt by their
load system, matching how rollback snapshots treat them.

## 4. Editor Step 1: project shell, Content Browser, settings

| Piece | File |
| --- | --- |
| Project tree, `project.femproj` (TOML), atomic config mirror | `Source/Editor/Infrastructure/ProjectConfig.hpp`, `Source/Editor/Infrastructure/ProjectConfig.cpp` |
| Open/New Project startup window | `Source/Editor/Panels/ProjectDashboard.hpp`, `Source/Editor/Panels/ProjectDashboard.cpp` |
| File tree + `.femeta` metadata + drag-to-Viewport spawn | `Source/Editor/Panels/ContentBrowserPanel.hpp`, `Source/Editor/Panels/ContentBrowserPanel.cpp` |
| Deterministic import policy (`extension -> kind/id/guid/cooked`) | `Source/Editor/Infrastructure/AssetImport.hpp`, `Source/Editor/Infrastructure/AssetImport.cpp` |
| Background PNG -> BC7/ASTC -> VFS remap -> RHI layer swap | `Source/Editor/Infrastructure/AssetImportService.hpp`, `Source/Editor/Infrastructure/AssetImportService.cpp` |
| Project/Editor Settings panels | `Source/Editor/Panels/SettingsPanels.hpp`, `Source/Editor/Panels/SettingsPanels.cpp` |
| Shared texture cooker (editor + fby_fempak) | `Source/Engine/Assets/TextureCook.hpp`, `Source/Engine/Assets/TextureCook.cpp`, `FemboyEngine2D::AssetCook` |

### Project tree

`ProjectDashboard::New Project` creates exactly `assets/textures`,
`assets/audio`, `assets/scenes`, `configs`, an empty-but-valid
`assets/scenes/main.toml`, and `project.femproj`. `Open Project` validates the
TOML before handing the paths to `EditorSystem::OpenProject`, which mounts the
project's `assets/` at a higher VFS priority, loads every `.femeta`, starts the
watcher/importer and opens the default scene.

`ProjectConfigStore` owns two trivially-copyable PODs. A settings edit is
recorded as a normal `InteractionTransaction` against an
`AssetDatabase`-independent external buffer registered with
`CommandStack::RegisterExternalBuffer`. On `CommitTransaction()` the store writes
`project.femproj` beside-and-rename and mirrors the editor keys into
`editor.ini`; Ctrl+Z/Ctrl+Y restores the POD and re-applies it to audio, VSync,
the accessibility kernel and the OLED shifter.

### Auto-import and hot reload

The watcher now creates `<source>.femeta` for a new PNG/TGA/WAV/OGG/Opus/WebM
(or scene/atlas TOML), using `AssetId::FromPath` and `ComputeAssetGuid` so two
machines produce the same identity. `AssetImportService` owns a fixed request
ring and one `jthread`; the worker calls the shared BC7/ASTC cooker, writes the
`.ftex` atomically, and the editor-thread `Tick()` commits the AssetDatabase
transaction, remaps the cooked file through `VirtualFileSystem::ReloadFile`, and
calls `RHI::UpdateTextureArrayLayer` for every mip of the bound atlas layer.

### Content Browser

The panel walks the loose assets tree into a fixed 1024-entry node table, joins
each file with its database row (AssetId/GUID/type/generation) and hides cooked
artifacts. Dragging a texture or `.toml` prefab to the Viewport produces a
`FBY_CONTENT_ASSET` payload; `SpawnAsset()` wraps the new entity set in a
`SpawnTransaction`, so one drop is one undo step. Undo destroys the entities and
redo recreates them with their original EnTT identifiers.

### Settings panels

`ProjectSettingsPanel` controls FixedUpdate Hz, max fixed steps, Master/SFX/Music
gains, BlindMode, DeafMode, FlashGuard threshold/gain, the OLED pixel shifter and
the cooker target. `EditorSettingsPanel` controls VSync (runtime
`RHI::SetVSync` plus the platform hint), UI scale, asset watching, browser
auto-refresh and `editor.ini` mirroring. Every slider goes through the command
stack; the panels re-apply the POD after undo/redo.

## 5. Editor Step 2: node graph editors and AOT

| Piece | File |
| --- | --- |
| Graph-agnostic transactional ImNodes canvas | `Source/Editor/Panels/NodeCanvasPanel.hpp`, `Source/Editor/Panels/NodeCanvasPanel.cpp` |
| Material graph model + Tilt25D/Distortion | `Source/Engine/RHI/MaterialGraph.hpp`, `Source/Engine/RHI/MaterialGraph.cpp` |
| GLSL 450 emitter | `Source/Engine/RHI/MaterialGraphCompiler.hpp`, `Source/Engine/RHI/MaterialGraph.cpp` |
| Live graph -> GLSL -> SPIR-V -> pipeline driver | `Source/Editor/Infrastructure/MaterialGraphRuntime.hpp`, `Source/Editor/Infrastructure/MaterialGraphRuntime.cpp` |
| Material editor, Apply Shader, MeshRenderItem binding | `Source/Editor/Panels/MaterialGraphPanel.hpp`, `Source/Editor/Panels/MaterialGraphPanel.cpp` |
| VisualScript graph + field/condition/math/spawn nodes | `Source/Engine/Script/GraphCompiler.hpp`, `Source/Engine/Script/GraphCompiler.cpp` |
| Graph -> bytecode -> C++26 front end | `Source/Engine/Script/AotGraphCompiler.hpp`, `Source/Engine/Script/AotGraphCompiler.cpp` |
| Background Clang/GCC `-O3 -shared` cooker | `Source/Engine/Script/AotBuilder.hpp`, `Source/Engine/Script/AotBuilder.cpp` |
| VisualScript editor, PIE compile, AOT status | `Source/Editor/Panels/VisualScriptPanel.hpp`, `Source/Editor/Panels/VisualScriptPanel.cpp` |
| Vendored ImNodes (MIT) | `third_party/imnodes` |

### Transaction model

`NodeCanvasPanel` receives a `CanvasModelOps` table of plain function pointers
and a `void*`, so the same interaction code drives `MaterialGraph` and
`LogicGraphDocument`. The panel registers the entire graph document as one
`CommandStack` external buffer. One drag is:

```
BeginTransaction("Move graph node")
  Record(old graph bytes, current graph bytes)  // once per changed frame
CommitTransaction()                              // one undo step
```

CommandStack compresses only differing byte ranges: moving a node records one
8-byte `Vec2`, connecting a pin records the pin pair. Side inspectors
(constants, field rebinding) use the same `BeginDocumentEdit` /
`RecordDocumentEdit` / `CommitDocumentEdit` hooks, and EditorSystem commits both
canvases before Ctrl+Z/Y.

### Material graph

`MaterialNodeKind` is append-only and still a 20-byte DOD record. `Tilt25D`
emits `uv + slope * height`; `Distortion` emits an animated
`uv + vec2(sin(uv.y * f + t), cos(uv.x * f + t)) * strength` warp. The compiler
sets `needsTime` for both input Time and Distortion, reconstructs world position
when the graph asks for it, and emits a complete fragment shader (plus an
optional replacement vertex stage for `VertexPosition`). `MaterialGraphRuntime`
hashes the semantic graph, refuses to recompile an unchanged hash, caches
pipelines by hash, and keeps the last good pipeline while an edit is invalid.
`MaterialGraphPanel::BindRenderItem()` makes Apply Shader update
`MeshRenderItem::materialPipeline` immediately.

### Logic graph and AOT

`LogicGraph` still has 20-byte nodes and 8-byte links. `LogicFieldBinding` adds a
fixed 80-byte self-describing `(LayoutHash, size, kind, offset, fieldSize)` pair;
GetField/SetField emit `LoadField*`/`StoreField*` against `Mirror` offsets.
Condition is normalized to `1.0f/0.0f` so Branch and Math consume it without a
new opcode; SpawnEntity emits `SpawnPrefab`. `CompileLogicGraph` produces the
canonical bytecode the VM runs.

Release cooking uses the same bytecode as the single artifact boundary:
`TranspileLogicGraphToCpp` compiles the graph, parses the bytes and emits one
flat C++26 function per event. `AotBuilder` writes that file, then invokes the
system compiler (`clang++`/`g++` with `-std=c++26 -O3 -shared -fPIC` on POSIX;
the MSVC-driver form for `clang-cl`/`cl`) on a worker `jthread`, redirects the
compiler log to `<module>.log`, and publishes a revision. On Windows the child
environment is repaired (`TMP`/`TEMP` from a POSIX shell) and
`CreateProcessW` is used instead of a shell. The generated plugin is validated
by `ValidateAotModule` against the live `EcsBridge` layout hashes.

## 6. Verification

`ctest`: **20/20** green. `graph_editor` covers the new MaterialGraph nodes and
SPIR-V path, LogicGraph field/condition/spawn nodes through the VM, sparse graph
undo, the AOT source shape and the background builder failure path. Running
`FBY_AOT_TEST_COMPILER=<clang++ path> fby_graph_editor_test` additionally
compiles and loads a real native plugin end to end. `editor_infrastructure`
still covers project/config round-trip, `.femeta` auto-import, the background
PNG -> BC7 cook and settings undo.

## 7. Known gaps

The live backlog is [ROADMAP.md](../../ROADMAP.md); this chapter keeps no second
copy. Gaps that were listed here are closed: the PIE toolbar has Play/Stop (and a
Step command), and material graphs have a `.fgraph` serializer that the cooker
consumes.

The items that remain true are tracked in the roadmap:

* Timeline panel context (timeline asset, skeleton, SpriteAnimation, audio,
  scripts) is still not bound by the Content Browser selection UI.
* Logic fields are limited to `Float` and `Vec2`; int/bool/string fields need a
  VM conversion opcode and a matching AOT emitter.
* `AotBuilder::Cancel()` cannot interrupt a compiler process that is already
  running; the worker joins after it finishes.
* The command stack stores component bytes and spawn snapshots, not keyframe or
  timeline edits.
* OLED pixel shifting applies to the host's Pink UI canvas pool; the editor does
  not own a game canvas, so the sample app has nothing to shift.
