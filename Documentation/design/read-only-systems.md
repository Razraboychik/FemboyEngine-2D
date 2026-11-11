# Read-only systems

A system that only inspects the ECS must take a read-only view. It then cannot
`create`, `destroy`, `emplace` or `remove`, and every component view yields
`const T&`, so a write is a compile error instead of a convention.

## Infrastructure

- `Fcs::ConstView<...>` and `Registry::view() const` — const component iteration.
- `World::ForEach` const overload — const component callback.
- `Ecs::ConstCoreSystemView` + `BindConstCoreSystem` — core systems with a
  `const Registry&`.
- `Ecs::ConstRenderSystemView` + `BindConstRenderSystem` — render/extract
  systems with a `const Registry&` and const render/accessibility bundles.

`Fcs::View` and the mutable view types stay for systems that write. A system
with mixed access keeps `Registry&` and declares `ComponentReads` and
`ComponentWrites` in `SystemAccess`.

## Audit of scheduled systems

Read-only systems, migrated:

| System | Registration | Evidence |
| --- | --- | --- |
| `ClothRenderSystem` | `Physics/ClothRenderSystem.cpp` | reads `ClothComponent`, writes only its own draw list |
| `RenderExtractSystem` | `Apps/Sandbox/Main.cpp` | reads `Transform2D`, `Sprite2D`, `DrawOrder`, writes the extract buffer |

The helper `ExtractRegistry` in `Apps/Sandbox/Main.cpp` also takes
`const Registry&`.

Systems that mutate ECS and therefore stay mutable:

| System | Reason |
| --- | --- |
| `AIControllerSystem`, `AIPerceptionSystem` | emplace controller/perception components |
| `AnimGraphSystem` | advances animation state and poses |
| `BoneAttachmentSystem` | writes `Transform2D` from bone matrices |
| `SpriteAnimationSystem` | advances sprite animation state |
| `StateTreeSystem` | advances state-tree state |
| `SkeletonSystem` | evaluates bone matrices |
| `LightingSystem`, `LightSourceSystem` | emplace/remove light result components |
| `NavigationSystem` | emplaces missing nav-agent components |
| `PhysicsFixedStepSystem`, `GravitySystem`, `SdfCollisionSystem` | advance bodies and write transforms |
| `CardBoardSystem` | emplaces `Transform2D` and `DrawOrder` |
| `VideoSystem` | advances `VideoStreamComponent` |
| `WeatherSystem` | advances `WeatherContextComponent` |
| `SteamAchievementSystem`, `EgsStoreSystem`, `GogAchievementSystem`, `GogStorageSystem` | advance request/cloud-save components |
| Editor PIE systems (`MovementSystem`, `GravityStepSystem`, `ContactToSpriteAnimationSystem`, `IkSystem`, `SkeletalAnimateSystem`, `SkeletalEvaluateSystem`, `PhysicsInterpolateSystem`, `LightingStepSystem`, `SpriteAnimationStepSystem`, `AudioUpdateSystem`, `AIPerceptionStepSystem`, `StateTreeStepSystem`, `AICommandApplySystem`, `AIControllerStepSystem`, `NavigationStepSystem`, `EventsClear`) | each advances or creates PIE state; they are the mutation boundary of the game module |

`ScriptSystem` takes the full `SystemContext` because it executes gameplay
scripts through a command buffer; it is not read-only.

## Access masks and parallel opt-in

`SystemAccess` is declared for every scheduled engine system: `ClothRenderSystem`,
`CardBoardSystem`, `BoneAttachmentSystem`, `SpriteAnimationSystem`,
`VideoSystem`, `WeatherSystem`, `WeatherPreconciliatedSystem`,
`NavigationSystem`, `LightingSystem`, `LightSourceSystem`, `AIControllerSystem`,
`AIPerceptionSystem`, `AnimGraph`, `StateTree`, `PhysicsFixedStepSystem`,
`GravitySystem`, `SdfCollisionSystem` and the platform store/achievement
systems. Component masks document reads/writes even while `ParallelSafe` stays
false; without that flag the graph still serializes same-phase systems.

`ParallelSafe` is enabled only where the system has no engine-service side
effects and its ECS writes are known:

- `ClothRenderSystem` — reads `ClothComponent`, writes only its own draw list.
- `CardBoardSystem` — owns board/slot/transform/draw-order data, no services.
- Sandbox `RenderExtractSystem` — reads sprites, writes the extract buffer. The
  Render phase is inline by design, so this is a declaration for the future.

Complex systems (`AIController`, `AIPerception`, `AnimGraph`, `StateTree`,
`PhysicsFixedStep`, `Gravity`, `SdfCollision`, Editor PIE systems) stay
`ParallelSafe = false`: they share services, static state or a command buffer,
or their access set is not yet audited. Do not flip the flag without that audit.

## Debug flags

- `FBY_ENABLE_ACCESS_VALIDATION=1` (Debug) — `SystemGraph` publishes the current
  system's masks through thread-local storage, and FCS asserts when a view or
  component access was not declared. Release compiles the checks to empty calls.
  Toggle with `-DFBY_DEBUG_ACCESS_VALIDATION=OFF`.
- `FBY_ENABLE_SYSTEM_TIMING=1` (Debug) — `SystemGraph` records `LastNs` per node;
  `SetTimingEnabled(false)` removes the clock reads at runtime. Toggle with
  `-DFBY_DEBUG_SYSTEM_TIMING=OFF`.
- The iteration guard is always on and costs one increment/decrement per view,
  not per entity; it never appears in the per-row loop.
- The style gate also rejects pointer/reference, `std::string`, `std::vector`,
  `std::string_view` and `std::span` fields in reflected components.

`SystemGraph::LevelCount()` and `SystemLevel(i)` expose the wave layout;
`At(i).LastNs` exposes the measured time after a run.

## Scheduler benchmark

`Source/Tests/SystemGraphBenchmark.cpp` runs eight independent `NoEcsAccess`
nodes with a fixed workload. On the current machine it reports roughly
`serial 7.6 ms / parallel 1.7 ms / 4.5x` on eight worker threads. The number is
a ceiling for the scheduler, not a promise for the engine: if systems touch the
same components or services, the graph correctly serializes them.

## Rule

- New system that only reads: use `ConstCoreSystemView`, `ConstRenderSystemView`
  or a `const Registry&`; never take the mutable view "just in case".
- Mixed system: keep the mutable view and declare both access masks.
- Do not add `const_cast` to a read-only system to write "one small field"; that
  is exactly the bug this split prevents.
