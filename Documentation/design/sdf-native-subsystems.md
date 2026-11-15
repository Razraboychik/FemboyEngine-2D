# SDF-native subsystems: architecture review

This document classifies ten proposed subsystems against the architecture in
this tree and identifies which ones fit the existing design. Each entry answers
two questions: what follows from the current design, and what is new and how it
should be shaped as ECS components and systems without exceeding the frame
budget.

Several proposals assume an SDF-raymarched renderer with no sprites, no Box2D and
no upscalers. FemboyEngine2D has all three, so those assumptions must be handled
in the design rather than left implicit.

## Baseline: what the tree already has

| Capability | Where | State |
|---|---|---|
| Dense distance view | `Source/Engine/Physics/SdfCollision.hpp` (`SdfField`, non-owning `mdspan`, bilinear `Sample`, central-difference `Gradient`) | shipped |
| Sparse bricks | `Source/Engine/Physics/SdfBrickMap.hpp` (16x16 bricks, `Empty`/`Full`/`Narrow`, CPU payload + GPU table) | shipped |
| Analytic palette | `Source/Engine/Physics/SdfPrimitives.hpp` (20 exact shape families, 32-byte `SdfShape`, gradient, reflection) | shipped |
| SDF vs rigid bodies | `Source/Engine/Physics/SdfCollision.cpp` (penetration, gradient normal, `pushOut`, `restitution`, `maxImpulse`, `subSamples`) | shipped, correction pass |
| Soft shadows, CPU | `Source/Engine/ECS/Lighting.hpp` (sphere trace, `Light2DOccluderComponent`, 64 casters) | shipped |
| Soft shadows, GPU | `sdf_shadow.comp` at half resolution, 256x256 R8 field, 16 casters, `SdfLightingFrame` | shipped |
| Indirect light, CPU | `Source/Engine/Physics/SdfGi.hpp` (4-lobe probe volume, sliced update, one-bounce reference) | reference only |
| Deformables | `Cloth.hpp` (Verlet, render mesh), `Fluid.hpp` (Gerstner + 64x64 shallow water), `Particles.hpp` (GPU emitters) | shipped |
| Atmosphere | `weather_*.comp` (GPU rain/snow, noise clouds, fog, procedural fire) | shipped |
| Navigation | `Source/Engine/Navigation/Navigation.hpp` (128x128 cost grid, A*, steering) | shipped, fixed capacity |
| Determinism | `Core/Time.hpp`, `DeterministicRng`, `Snapshot.hpp` (full + delta), `CommandLog.hpp` (v3, 9 command types), `NetRollback.hpp` | shipped |
| Parallelism | `Core/JobSystem.hpp` (`ParallelFor`, per-thread `ThreadLoad`), `ECS/ParallelForEach.hpp` | shipped |
| Plugins | `Core/PluginHost.hpp`, `Core/PluginAPI.h` (system table, `ComponentRegistry` with `layoutHash`) | shipped, no code reload |
| Graph editing | `RHI/MaterialGraph.hpp` + `Editor/Panels/NodeCanvasPanel.hpp` (generic transactional canvas) | shipped |
| Frame counters | `RHI::FrameStats`, `JobSystem::ThreadLoad` | shipped, no visualizer |
| Upscaling | DLSS / FSR2 / XeSS adapters, custom and FSR3 frame generation, dynamic resolution, temporal resolve | shipped |

The gap that matters most is not in this table. `SdfField` has **no producer in
the engine**. The editor demo builds a 128x96 field by hand in
`Source/Apps/Editor/Main.cpp`, and weather consumes a field handed to it. Every
subsystem below therefore depends on one shared prerequisite.

## Prerequisite: the SDF world pipeline

All ten proposals assume a field that describes the real level, changes
incrementally and is cheap to query. Nothing in the tree does that yet. This is
the piece to build first, and it is the one place where a mistake is expensive.

### Data

`.fsdf` asset:

| Field | Type | Meaning |
|---|---|---|
| magic, version | char[8], UInt32 | format identity |
| origin, cellSize | Vector2D x2 | world mapping of cell (0,0) |
| brickSize | UInt32 | 16, matching `SdfBrickSize` |
| brickCountX, brickCountY | UInt32 | table dimensions |
| payloadBytes | UInt64 | narrow-band bytes |
| contentHash | UInt64 | cache key |
| brick headers | `SdfBrickHeader[]` | state + offset, already defined |
| payload | float[] narrow band | distances; positive outside |

The file stores the **narrow band only**. Bricks in `Empty` state carry no
payload, which keeps a 4096x4096 world at 1 unit per cell near 4-8 MB instead of
64 MB.

### Runtime object

`SdfWorld` owns the field, not the registry:

- double-buffered narrow band and brick map, swapped after a bake;
- a dirty-brick queue sorted by brick index, consumed under a per-frame budget
  (`SdfBakeBudgetCells`, default 16384 cells per fixed step);
- `FieldVersion` incremented on every committed bake; it is already the cache key
  used by `SdfLightingFrame.fieldVersion`, the weather system and the GI volume;
- one non-owning `SdfField` view published for readers, so existing consumers do
  not change.

`SdfWorldRegistry` holds at most four worlds (main, PIE instance, preview,
streaming neighbour) as fixed slots, matching the existing PIE clone model.

### Components

| Component | Size | Contents |
|---|---:|---|
| `SdfWorldComponent` | 16 | `UInt64 WorldId`, `UInt32 Flags`, `UInt32 Reserved` |
| `SdfShapeComponent` | 32 | already exists, becomes a bake source |
| `SdfLayerComponent` | 8 | sort layer, collision layer for bake input |

The world component is a singleton on a dedicated entity. It is reflected and
serialized; the field itself is not, matching the tilemap rule
(`Documentation/design/world-systems.md`): the scene stores a reference, the
runtime rebuilds the grid.

### Bake sources

1. `SdfShapeComponent` entities in `PreUpdate`, unioned with smooth-min;
2. tilemap solid cells, converted box-by-box per 16x16 brick;
3. sprite alpha masks, only when the sprite opts in through a flag;
4. CSG operations from the destruction system (see below).

Bake runs on `JobSystem` workers, one brick per task, fixed brick order, float
math without contraction so a bake is bit-identical across runs and compilers.
That property is what makes the later replay features possible.

### GPU side

The 256x256 R8 field upload must be replaced by a brick atlas before any world
larger than the demo exists:

- atlas of 16x16 tiles, 8-bit unorm, 4 bits of range compressed around the band;
- upload only dirty tiles through the existing staging ring;
- the compute and fragment shaders read through a tile lookup table, exactly the
  `SdfBrickGpu` table `SdfBrickMap` already produces.

Without this, every large-world feature silently degrades to the 256x256 demo
field.

## 1. SDF-native physics and destruction

### 1.1 Gradient depenetration

**Verdict: derived.** Not only conceptually implied, but implemented.
`SdfCollisionSystem` samples penetration, takes `SdfField::Gradient`, removes the
inward velocity, and teleports the Box2D body out by `pushOut` of the
penetration. `CollisionModeUsesSdf` in `Presentation.hpp` and the
`CollisionLayer::SdfTerrain` matrix entry show this is a supported mode, not an
experiment.

What is wrong with it, and what to build:

- **Teleporting is not a force.** `b2Body_SetTransform` skips the solver and
  breaks contact continuity and CCD. Replace with a velocity-level correction:
  mass-scaled normal impulse, restitution, extra tangential friction, clamped by
  `maxImpulse`. This is a solver-quality fix, not an architectural one.
- **No manifold.** One contact per body means a body in a corner jitters between
  two normals. Add `SdfContactManifold`: up to four points per body per step,
  warm-started from the previous step, stored SoA (points, normals, depths,
  impulses, entity ids) in a fixed `kMaxSdfContacts` ring.
- **No broadphase.** "Without AABB" is not achievable and should not be a goal:
  the brick map is the broadphase. Skip bodies whose AABB intersects only `Empty`
  bricks, and use `Full` bricks as a conservative hit. `CollisionFilter.hpp`
  already carries the layer matrix.
- **Gradient artefacts.** On the medial axis of a corridor, central differences
  cancel and the fallback normal is `(0, -1)`. Detect the degenerate case with a
  four-tap support probe and keep the previous normal instead of a constant.

Components:

| Component | Size | Contents |
|---|---:|---|
| `SdfColliderComponent` | 24 | `UInt32 ShapeIndex`, `Float Skin`, `Float Friction`, `Float MaxPushSpeed`, `UInt32 Flags`, `UInt32 Reserved` |

`ShapeIndex` indexes into a world shape pool; the shape itself stays
`SdfShapeComponent` data. This keeps the collider 24 bytes instead of 56 and lets
two entities share one shape.

Cost per body per fixed step: one bilinear sample plus four gradient taps, so
five fetches. One thousand SDF bodies on the internal 120 Hz rigid-body substep
is 600k fetches per second, well inside a worker slice. Sort bodies by entity
index before the pass; never accumulate contacts with atomics if replay matters.

**Author boxes first.** An SDF collider costs five fetches and a bake; an
authored box collider costs a few flops. SDF is the representation for irregular
and procedural surfaces, not the default one. Tilemap solids convert to plain
Box2D boxes at bake time, and only art that needs a distance field
becomes one. A universal engine has to keep the cheap path the default path, or
every game pays for the most demanding one.

### 1.2 Soft bodies, fluids, slime and mud through blending

**Verdict: partially derived, mostly new.** The tree has Verlet cloth, a
shallow-water height field and GPU particle emitters, so deformable data already
exists. It has no SDF blending, no metaball field and no unified particle pool.

Design:

- **One pool, two solvers.** `SoftBodyPool` is SoA: position, previous position,
  velocity, inverse mass, radius, group, material; plus constraint arrays for
  cloth-like tops. Cloth keeps its existing `ClothSystem` and mesh path; slime,
  mud and goo use the pool with shape matching (O(n), no adjacency) and a
  viscosity term. Liquids use position-based fluids over the same pool.
- **Blending is a render-time field, not a world CSG.** A particle cluster owns a
  local coverage region, `32x32` to `128x128` cells at 4-8 units, rebuilt each
  frame. Blending operator is smooth union with a per-material `k` (slime high,
  mud low, water zero). Never subtract metaballs from the world field: the cost
  and the invalidation surface are unbounded.
- **Contacts use the region field.** Once a region exists, particle-vs-wall
  contact is one bilinear sample plus push-out, the same code path as 1.1. That
  removes pairwise wall tests entirely.
- **ECS split.** Authoring stays in the registry, simulation lives in the pool,
  exactly the pattern `ClothId`/`ClothComponent` and `EmitterId` already use.

| Component | Size | Contents |
|---|---:|---|
| `SoftBodyComponent` | 16 | `UInt64 BodyId`, `UInt32 Material`, `UInt32 Flags` |
| `MetaballRegionComponent` | 16 | `UInt64 RegionId`, `Float CellSize`, `UInt16 Layer`, `UInt16 Flags` |

Budget: 4096 particles at 120 Hz with a 16-neighbour cap is about 1M pair
updates per second, single-digit percent of one core with the existing spatial
hash (shared with 6.2).

**Rendering it.** Do not raymarch the screen. A tile-local coverage buffer is
thresholded and the surface polyline is extracted with marching squares on the
same cells the solver already visits. The polyline feeds the normal sprite path,
so depth sorting, the batching and the upscalers keep working; a raymarched
surface would need motion vectors that the sprite path does not have.

### 1.3 CSG destruction

**Verdict: new.** Nothing in the tree does boolean subtraction on any
representation.

Design rules that make it survivable:

1. **Destroy locally, never globally.** A destructible is its own small field:
   4x4 bricks at 1-2 units per cell covers a 64-128 unit object. Subtracting a
   projectile is a handful of brick rebakes, not a world rebake.
2. **Ops are the source of truth.** A subtract operation is a record, not an
   immediate write: shape index, transform, blend width, time, op kind
   (`SdfOpKind::Subtract`, `Intersect`, `Union`). The brick payload is a cache of
   the op list. Saves and replays store the list, not the pixels.
3. **Analytic during the transient.** While a brick bake is queued, queries
   evaluate the analytic op list on top of the cached bricks. Physics never waits
   for a bake.
4. **Imprint, don't merge.** When an object settles, its field is merged into the
   world field once, as a bake input. Until then the world field does not see it,
   which keeps the world version stable and the lighting cache warm.
5. **Debris from the changed band.** Marching squares over the dirty brick band
   gives fragment outlines; spawning uses the command buffer, so replay matches.

| Component | Size | Contents |
|---|---:|---|
| `DestructibleComponent` | 24 | `UInt32 ObjectIndex`, `Float Integrity`, `Float MaxIntegrity`, `UInt32 OpCount`, `UInt32 Flags`, `UInt32 Reserved` |
| `SdfOpComponent` | 40 | `UInt32 ObjectIndex`, `UInt32 ShapeIndex`, `Float4 Transform` (position, rotation, scale), `Float Blend`, `UInt32 Kind`, `UInt32 Tick` |

Op components live in the scene only while an editor session authors them; at
runtime the pool owns the list and the component is a handle. Destruction that
must persist goes into the save as an op list plus a content hash of the baked
bricks.

Cost: one brick is 256 cells; an op over a 4x4 brick area with 20 ops in the
stack is about 20k analytic evaluations, tens of microseconds on a worker.

**Pre-fracture first, carve second.** Runtime CSG is resolution-bound: a thin
cut thinner than two cells disappears, and the silhouette changes with the cell
size. A universal destruction tier therefore has two levels. Structural pieces
are pre-fractured at author time into convex fragments with authored debris and
stable collider shapes; runtime CSG is for surface detail that gameplay does not
depend on. Games that need reliable, network-replicated destruction use the
first level; the second level is for looks and for editor carving.

## 2. Advanced rendering

### 2.1 SDF global illumination and soft shadows

**Verdict: soft shadows are shipped; GI is a CPU reference that needs a GPU
implementation.** `SdfGiVolume` already bakes 4-lobe probes from
`SdfBrickMap` and updates one slice at a time. That design is the right one. It
is not on the GPU and is not wired to the frame path.

Design:

- **World-anchored probe grid**, spacing 8-16 units, allocated only over bricks
  that are `Narrow` or `Empty` plus the camera bounds. `Full` interior bricks
  contribute nothing, so a level made of thick walls costs almost no probes.
- **Four lobes per probe**, matching the CPU reference, so the tests keep
  validating the GPU path.
- **Temporal update in quarters.** One quarter of probes per frame, 8 rays, 24
  steps. On a 2048x2048 region at 16 units, that is 16k probes, so 4k probes per
  frame, 32k rays, about 770k field samples. This is a sub-millisecond compute
  pass on a mid-range GPU; measure it, do not assume it.
- **Resolve and consume.** A 3x3 bilateral filter writes an
  `r16g16b16a16f` irradiance texture; the sprite fragment adds one tap on top of
  the existing direct shadow term.
- **Bake the static world.** A level's static GI is baked into the probe volume
  in the editor and stored next to the `.fsdf`. The runtime updates only probes
  near moving emitters, moving occluders and dynamic CSG. This is what keeps a
  scene with a thousand lights inside budget, and it is the only version that
  runs on the low tier at all.
- **History reset.** Reset on `FieldVersion` change, camera cut, PIE start and
  frame-generation reset. A stale GI history is the classic source of ghosting
  that temporal upscalers then amplify into a visible smear.

Constraints that come from this renderer specifically:

- GI is scene-only. The HUD is composited after frame generation and must never
  receive GI; the split already exists in `FrameComposer` (`RenderToTarget`
  versus `Submit`).
- The probe grid is in world space, so DLSS/FSR internal resolution changes and
  dynamic resolution do not move it. A screen-space probe grid would swim under
  dynamic resolution, and resizing it every scale change would defeat the
  temporal accumulation.
- Motion vectors must stay valid for any element that reads GI. Sprites already
  write them; a future soft-body surface rendered through the polygon path must
  too, or GI and upscalers will disagree at the edges.

### 2.2 Smoke and fire through Navier-Stokes

**Verdict: new.** `weather_fire.comp` is procedural simplex plus Worley noise
shaped by time, and clouds are an advected noise field. Neither is a fluid
solver. The shallow-water grid is a height field with no vorticity and no
buoyancy.

Design:

- **Per-region grids, not a screen grid.** A smoke region covers an emitter
  cluster: 192x108 cells at 8 units, RGBA16F twice (ping-pong) packing velocity
  XY, density, temperature. Cap active regions at 8. A single full-screen fluid
  grid is the usual way this feature dies on integrated GPUs.
- **Passes per fixed step**: advect (semi-Lagrangian), buoyancy, optional
  vorticity confinement, divergence, 20 Jacobi pressure iterations, gradient
  subtraction, then a boundary pass.
- **Boundaries from the brick map.** At region creation, sample the field per
  cell into a mask texture: solid cells clamp velocity to zero and use a Neumann
  pressure condition. Refresh the mask when `FieldVersion` changes, not per
  frame. This is the only new coupling between fluid and SDF, and it is cheap.
- **Particles ride the field.** Rain, snow and the existing particle emitters
  sample the velocity texture directly on the GPU, so smoke pushes rain without a
  readback.
- **Gameplay never reads the GPU field.** The simulation must stay CPU-authoritative.
  A small 64-probe readback, one frame late and explicitly documented as
  cosmetic, is the maximum allowed: the moment smoke affects a hitbox, replay and
  rollback diverge across GPUs.

Cost: 20k cells, about 26 passes, 8 regions, roughly 4M cell updates per frame on
the GPU. Fire is the same grid with a temperature source and a blackbody ramp;
heat distortion reuses the existing post-process chain.

## 3. Gameplay subsystems

### 3.1 Flow-field navigation from SDF

**Verdict: new, with two premises to correct.**

- A distance field of the *walls* gives you clearance, not direction to a goal.
  Stepping along `SdfField::Gradient` of the wall field pushes units **away** from
  walls. The proposal conflates the wall SDF and the goal potential; only the
  second one produces flow.
- "Without grids" is the wrong goal for a DOD engine. The grid is the fast,
  deterministic representation; what is new is that its costs come from the SDF,
  and that it is paged instead of the current fixed `128x128` cap in
  `Navigation.hpp`.

Design:

- **Paged nav grid of 64x64 cells.** Cost per cell: tilemap cost, plus a
  clearance term baked from the SDF field (distance to solid, downsampled). Pages
  are `Empty`, `Sparse` or `Dense`; only dense pages run Dijkstra.
- **Goal potential via bucket-queue Dijkstra** with integer costs. One field per
  active goal group, recomputed on cost change or goal movement at 5-10 Hz,
  budgeted across frames with a one-cell halo for page-boundary correctness.
- **Hierarchical for long paths.** A page graph over 64x64 pages answers
  long-range queries; the per-page flow field handles the last hundred cells.
- **Steering** blends `-gradient(potential)` with `gradient(clearance)` for local
  avoidance. The wall SDF belongs here, and it is also exactly
  the repulsion term from 1.1.
- **Determinism**: integer Dijkstra, ties broken by cell index; floats only in
  steering.

| Component | Size | Contents |
|---|---:|---|
| `FlowFieldAgentComponent` | 16 | `UInt64 FieldId`, `Float Radius`, `UInt16 Layer`, `UInt16 Flags` |
| `FlowFieldGoalComponent` | 24 | `Vector2D Goal`, `Float Radius`, `UInt32 Group`, `UInt32 Priority`, `UInt32 Flags` |

### 3.2 Massive crowd

**Verdict: new.** There is no boid code and no crowd pool. What exists is the
infrastructure to build one well: `JobSystem::ParallelFor`, `ParallelForEach`,
GPU particle simulation, the `StableIds` handle pattern and a flat `RenderItem`
submit path.

Design:

- **Pool, not archetype.** 100k agents in archetype storage would cost a per-entity
  storage walk and a chunk hop per component. Use `CrowdPool`: SoA of position,
  velocity, group, wander phase, flags, 32 bytes per agent, 3.2 MB at 100k, which
  fits comfortably in cache-friendly chunks.
- **Stable ids keep authoring in ECS.** `CrowdAgentComponent` (8 bytes: index and
  generation) points into the pool, the same contract as `ClothComponent` and
  `ParticleEmitterComponent`.
- **Spatial hash shared with SPH.** Counting sort into an 8-byte-per-cell grid
  (head, count), three parallel passes: count, prefix, scatter. Cell size is twice
  the perception radius, so one 3x3 neighbourhood lookup.
- **Capped neighbourhood.** Fixed capacity of 16 neighbours per agent, gathered
  in cell order. The cap is what makes the cost predictable; without it a dense
  crowd degenerates to quadratic.
- **Steering at 60 Hz, render interpolation from the fixed step**, matching the
  existing accumulator pattern.
- **LOD by screen coverage.** Near agents run full boids; far groups advect along
  their flow field with a cheap wander term. This is also where 100k stays
  affordable: the expensive tier is typically a few thousand agents.

Cost estimate at 100k agents, 16 neighbours, 60 Hz: 96M pair updates per second,
which is roughly 1 ms of eight-core time with the existing job system. That is
the honest number to design against, and it should be benchmarked before the
feature is promised at 120 Hz.

The recommended product target is 8-16k individually simulated agents plus
tiered groups, not 100k authoritative agents. 100k is a visual number: it is
reached with instanced quads over flow-field advection at 10-15 Hz, while the
near tier runs full boids. Promising 100k authoritative agents at 60 Hz on the
CPU is how a crowd feature gets cut after the first content push.

GPU path: the engine already runs GPU particle simulation, so a compute boids
variant is feasible for 500k+ agents. The rule stays the same as in 2.2: GPU
crowds are visual only. If a crowd must be hit, it runs on the CPU.

## 4. Tooling and production

### 4.1 Hot reload of systems

**Verdict: derived, partially present.** `PluginHost` already separates plugin
system tables from the registry, and `ComponentRegistry` already stores size,
alignment and `layoutHash` per component. Asset reload goes through
`VFS::ReloadFile` and the `AssetDatabase` reload queue. What does not exist is
code reload with preserved data.

Design:

1. Reload only at a frame boundary, after `FixedUpdate`, with the job system
   drained.
2. Call `OnUnload`, unregister the plugin's systems, destroy plugin-owned native
   handles (GPU objects, physics bodies, pools) through a declared resource list.
3. Copy the DLL to a unique name and load it; Windows keeps the old image locked.
4. Validate every registered component: name, size, alignment, `layoutHash`. A
   mismatch rejects the new build and keeps the old one alive.
5. Re-add systems at the same phase and order. `SystemGraph::RemoveUser` already
   supports the unload half; the missing piece is a replace path that keeps the
   slot index so ordering survives the swap.
6. Component data survives because the registry is engine-owned. Plugin-local
   state must be a POD `PluginStateBlock` allocated by the host, or captured and
   restored through the snapshot API.

Determinism: a reload is a recorded event in the command log, tagged with the
build hash. A replay that cannot load the same build must fail loudly instead of
silently diverging.

### 4.2 Time-travel debugging

**Verdict: strongly derived.** Fixed step, `DeterministicRng`, full and delta
snapshots with the RNG state inside, `CommandLog` v3 and rollback netcode already
exist. This is mostly assembly work plus hardening, not new machinery.

Design:

- `ReplaySession` keeps a keyframe every 60 ticks, per-tick deltas, input records,
  the RNG state, and command-log segments.
- `Seek(tick)`: validate, restore the nearest keyframe, apply deltas forward,
  verify the checksum, present.
- Scrubbing restores destructively, so the editor operates on a cloned registry,
  which PIE already does.
- Memory: 10k entities is roughly 640 KB per full snapshot, so ten keyframes plus
  deltas stay inside a 50 MB cap.

Hardening checklist, because determinism is where this feature always fails:

| Hazard | Rule |
|---|---|
| Wall clock in gameplay | already forbidden by `Clock` |
| Unordered container iteration | forbid in simulation phases; index-sorted passes |
| Multithreaded float accumulation | fixed partition and fixed reduction order |
| FMA contraction across compilers | disable contraction in simulation TUs |
| GPU readback into simulation | forbid; see 2.2 and 3.2 |
| Asset load timing | bake results keyed by content hash, never by time |
| RNG sharing | one PCG stream per subsystem, seeded from tick and id |

### 4.3 DOD profiler

**Verdict: partially present.** `FrameStats` and `ThreadLoad` already expose
per-frame counters and per-thread busy time. There is no trace, no archetype
view, and no panel.

Design:

- **Lock-free trace ring per thread**, 64k events of 16 bytes (tick, thread slot,
  system id, begin and end `rdtsc`, item count). One megabyte per thread, no
  allocation, no locks, near-zero cost when disabled.
- **System identity** comes from `SystemGraph` names, so the trace joins the
  scheduler without extra registration.
- **Archetype view** reads the per-view counts and the `RenderEcs` chunk headers:
  fill ratio, dirty block mask, upload bytes, column order. This is the view that
  shows the SoA problems the engine exists to prevent.
- **Core load** from `ThreadLoad`, already collected.
- **Cache misses**: be honest. On Windows there is no portable user-space cache
  counter; ETW kernel sessions need elevation, PCM needs a separate driver. Ship
  software counters by default and expose a `HardwareCounters` hook that reports
  "unavailable" instead of inventing numbers.
- **Panel**: a new `ProfilerPanel` plus Chrome trace export, so external tools
  work too.

### 4.4 SDF composer

**Verdict: strongly derived.** `MaterialGraph` is a node model with a compiler,
`NodeCanvasPanel` is a generic transactional canvas over model callbacks, and
`CommandStack` provides undo. The SDF composer is the same shape with a different
evaluator.

Design:

- **`SdfGraph` model** with the same capacity discipline as materials (128 nodes,
  384 pins). Node kinds: primitives from the 20-shape palette, transforms,
  boolean union/subtract/intersect, smooth boolean, domain repeat and mirror,
  polar repeat, noise and FBM, time, output.
- **Compile to a flat program.** A `SdfProgram` is an array of fixed-size
  instructions (opcode, flags, target register, four floats), evaluated per cell
  block by `SdfGraphEvaluator::Evaluate(program, span<Vector2D>, span<float>)`.
  No virtual calls, no per-node allocation, SIMD-friendly across cells.
- **Bake through the world pipeline.** Same brick writer, same dirty bit, same
  version counter. `SdfGraphComponent` is 16 bytes: asset id plus a parameter
  block offset.
- **Editor preview** compiles the same program to a fragment shader through the
  existing `ShaderCompiler` for the viewport only. The shipping path stays the
  CPU bake, so physics, lighting and rendering read one field and cannot drift.
  A golden test asserts CPU and GPU preview agree within tolerance.
- **Incremental rebake** on parameter tweaks: hash each node, rebake only the
  bricks whose subgraph changed.

## 5. Constraints imposed by the current renderer

These are the places where the architecture forces a different implementation,
and they apply to every item above.

- **Kill the idea of a raymarched scene.** The renderer is a flat `RenderItem`
  batcher feeding temporal upscalers and frame generation. A per-pixel SDF
  rasteriser has no motion vectors, no depth, no OIT compatibility and no path
  through `FrameComposer`. SDF stays a data and lookup layer behind sprites.
- **HUD separation is a hard boundary.** GI, smoke, destruction and the SDF
  visualizer must be scene-only; the HUD is composited after generation. The
  existing `SdfVisualizer` path is already on the HUD side and stays there.
- **Dynamic resolution and upscalers dislike screen-space fields.** Probe grids,
  smoke regions and coverage buffers are world-anchored. The half-resolution
  shadow mask is the exception that proves the rule: it is already rebuilt on
  viewport size changes.
- **Temporal history must invalidate on bake.** Any GI, shadow or smoke history
  keyed to `FieldVersion` resets when a brick bake commits. Otherwise a
  destroyed wall keeps lighting the room for half a second.
- **The frame path does not allocate.** Every new system needs fixed capacity,
  a preallocated pool and a documented budget; `SdfGiVolume` and `ClothSystem`
  are the reference style.

## 6. Budgets

Targets for the mid-range tier, 1080p internal, 60 Hz simulation:

| Subsystem | CPU per frame | GPU per frame | Memory |
|---|---:|---:|---:|
| SdfWorld bake | 1.0 ms budget, spread | upload only dirty tiles | 4-8 MB narrow band |
| SDF contacts, 1000 bodies | 0.2 ms | none | 64 KB manifold |
| Soft bodies, 4096 particles | 0.8 ms | coverage + threshold | 1 MB |
| Destruction, 16 objects | 0.5 ms budget | brick atlas edits | 4 MB |
| GI probes, 16k probes | negligible | 0.5-1.0 ms | 2 MB |
| Smoke, 8 regions | negligible | 1.0-1.5 ms | 4 MB |
| Flow fields, 4 goals, 256x256 | 0.5 ms at 10 Hz | none | 1 MB |
| Crowd, 16k individual agents | 1.0-1.5 ms | none | 1 MB |
| Crowd, 100k visuals | none | instanced submit | 4 MB |
| Profiler trace | under 0.05 ms | none | 1 MB per thread |

These are hypotheses to verify with benchmarks, not measurements. Each becomes a
`fby_*_benchmark` target next to `FrameComposerBenchmark`, with a regression
threshold in CI.

## 7. Build order

The dependencies are real: nothing below the world pipeline can be finished
before it, and the tooling depends on determinism that the physics work must not
break.

| Phase | Work | Exit gate |
|---|---|---|
| A | `SdfWorld`, `.fsdf`, brick atlas, dirty bake, versioning | 2048x2048 world bakes in under 50 ms; lighting and weather read it; tests cover round trip and corruption |
| B | Contact manifold and impulse correction; destructible pools and op lists | Corner stacks stable under the 2x rigid-body substep; replay of a destruction matches bit for bit |
| C | Flow fields, paged nav, crowd pool, spatial hash | 16k individual agents at 60 Hz in budget; 100k visuals through tiered groups; A* and flow field agree on reachability |
| D | GPU GI, smoke regions, particle coupling | GI under 1 ms; smoke pushes rain; no history ghosting after a bake |
| E | Hot reload, replay session, profiler panel, SDF composer | Reload keeps entity data; scrub to any recorded tick; composer bakes and previews |

Phases A and E can partly overlap: the profiler and the trace format are useful
during A, and the hot-reload validation logic is independent of the field.

## 8. Decisions and recommended answers

These are the answers for a general-purpose engine that must run the same
simulation on a 2015 laptop and a current desktop. Each choice keeps
authoritative state on the CPU, keeps the cheap path the default, and puts every
expensive feature behind a quality tier.

| Decision | Recommended answer | Why |
|---|---|---|
| Field resolution | One world grid with one cell size per world; separate object-local fields at higher resolution; metaball coverage fields are render-only and never queried by gameplay | Two world grids at different resolutions disagree at seams, and the disagreement shows up as units falling through floors. Local fields give detail without a global bake cost |
| Destruction persistence | Op list in the scene and save, baked bricks in a derived cache keyed by content hash; pre-fracture is the default tier | Ops are small, semantic, resolution-independent and replayable. Bricks are a cache, not data |
| GPU authority | Strict CPU authority: GPU output never feeds simulation. Cosmetic fields are a separately typed channel that snapshots and replay ignore | Cross-vendor float differences make GPU-driven simulation untestable, unreplayable and unfair in lockstep |
| Hot reload | Plugin DLLs and script bytecode, editor and iteration builds only, one reload boundary | Shipping builds should not load foreign code. The boundary is the same for both kinds, so it is implemented once |
| Hardware counters | Software counters, Chrome trace export and an optional external profiler backend; no elevated ETW or PCM path | Cache miss counters are not portable and need elevation. A trace format buys the same insight with none of the maintenance |
| Simulation rate | 120 Hz in the generated project, 2x substep for rigid bodies, crowd and fluids at 30-60 Hz with render interpolation | Rate is a project setting, not an engine constant. Light projects start at 60 Hz; doubling the rate doubles every subsystem for a benefit most games cannot show |

Two consequences worth stating explicitly:

- **A 60 Hz setting is not a downgrade.** Rigid-body stability comes from
  substeps and a contact manifold, not from a global 120 Hz tick. A 120 Hz world
  with one-contact teleport correction is less stable than a 60 Hz world with a
  four-point manifold.
- **Quality tiers are part of the contract.** Each of GI, smoke, destruction and
  crowd ships a low tier that is still correct, only coarser: baked probes,
  advected noise instead of a fluid, pre-fracture instead of CSG, flow-field
  groups instead of individual agents.

## 9. Rules that make the result universal and fast

The individual answers follow from six rules. They are worth applying to every
future subsystem, not only the ten in this review.

1. **One authoritative representation per concern.** One collision world, one
   navigation grid, one world SDF, one lighting model. A second parallel world
   that "is only used by the editor" is the most expensive kind of bug factory.
2. **The cheap path is the default path.** Boxes before SDF colliders, grid
   navigation before navmesh, baked probes before dynamic GI, pre-fracture before
   CSG. SDF is the authoring surface and the fallback for irregular geometry, not
   the universal substrate.
3. **Every expensive feature has a lower tier.** Universality is not "it runs on
   the high end"; it is "it degrades without changing gameplay outcomes".
4. **Budgets are contracts.** Fixed capacity, no frame-path allocation, a
   documented millisecond budget per system, and a benchmark that fails CI when
   the budget is exceeded.
5. **Simulation and visuals have a hard boundary.** Anything that affects a
   hitbox, a path, a save or a replay is CPU-side and deterministic. Anything
   that only affects pixels may live on the GPU, in any resolution, at any rate.
6. **Data over code.** Fields, graphs, flow fields, destruction and tuning live
   in assets that hot reload. A content iteration that needs a rebuild is a
   design defect in a universal engine.
