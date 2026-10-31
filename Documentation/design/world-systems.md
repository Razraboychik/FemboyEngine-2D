# World systems: tilemaps, skeletal IK and navigation

Three subsystems share the same cell/world mapping and the same fixed-capacity
ECS discipline. This document records their contracts.

## Tilemaps and the grid

`Source/Engine/Core/Grid.hpp` defines `ProjectionMode` (side, top-down,
iso-rect, iso-hex), `CellToWorld`, `WorldToCell` (cube rounding for hex),
`SortDepth`, `Neighbor`, `CellDistance` and `CellExtent`.

`Source/Engine/ECS/Tilemap.hpp/.cpp` defines a 64x64 `TilemapComponent`
(16 KiB), a tile word with index/solid/trigger/height, atlas UV, a culled
`TilemapRenderSystem` that produces keyed `TileQuad`s, and a `.tilemap` text
format (`Tilemap::SaveText`/`LoadText`) that `Tilemap::LoadFromVfs` reads through
the same VFS a cooked archive uses.

- **ECS persistence**: the scene serializes `TilemapAssetRefComponent` (a
  104-byte POD: asset path, sort layer, flags), never the 16 KiB grid. "Bake to
  selection" writes the `.tilemap`, the reference and the runtime grid; the
  editor rebuilds runtime grids from references after a scene load and after the
  PIE clone is created. Snapshots and PIE stay small, and a tilemap edit never
  needs a scene rewrite.
- The editor viewport appends tile quads to the sprite extract, so isometric
  maps sort with sprites through the same 64-bit key path.
- `Grid::CameraView` (`WorldToScreen`, `ScreenToWorld`, `ScreenToCell`) gives the
  editor and game code one orthographic mapping for all four layouts. The
  viewport drag-and-drop position and the isometric cell pick use it; a zero zoom
  is clamped, not divided by.
- Editor panel: `TilemapEditorPanel` switches projection, cell size, map size,
  atlas columns/rows/layer, brush palette, solid/height flags; it paints on
  click with a live isometric preview, saves/loads and bakes into the selected
  entity.
- Tests: `grid_skeletal` (round trip for all four projections, depth order, tile
  API, culling, `.tilemap` save/load and corruption) and `navigation`
  (hex/rect A* over tilemap-derived maps).

## Skeletal IK and animation compression

`Source/Engine/ECS/SkeletonIk.hpp/.cpp` provides an analytic two-bone solver
(circle intersection, pole sign, weight blend), FABRIK for N bones, chain
validation, `IkComponent` (up to four chains) and `IkSystem`.

- ECS authoring: `IkGoalComponent` is the reflected, scene-serializable goal:
  root/mid/tip indices as int32 with -1 = unused, target, tip offset, weight,
  pole, iterations and solver. `SolveIkGoal` builds the fixed `IkChain` POD and
  solves it. `IkSystem` runs authored goals after the runtime chain list, so an
  inspector-authored goal wins. The component is in the scene schema, so the
  generic inspector can add and edit it.
- Retargeting by bone-name hash: `BuildRetargetMap`/`ApplyRetarget`, with a
  per-bone length scale and rotation offset.
- Pose compression: a 1 KiB `QuantizedPose` (rotation 1e-4 rad, position 1/16
  unit, scale 1e-4), `CompressPose`/`DecompressPose`.
- Clip compression: `AnimationCompression.hpp/.cpp` does linear tolerance
  reduction, 16-bit normalized time, int16 values with a per-track scale, keeps
  interpolation and exposes `EvaluateCompressedTrack` for direct playback.
- The timeline panel writes and reads the compressed format directly
  (`SaveClipFile`/`LoadClipFile`, "Save clip"/"Load clip"), so an authored curve
  becomes a `.fanim` and comes back as a playable timeline. `ScriptDebuggerPanelTest`
  covers the file round trip and a truncated-file rejection.
- Asset format: `AnimationClipAsset.hpp/.cpp` defines `.fanim` ("FANM0001"),
  a 120-byte header, table-bounds validation and an FNV-1a content hash.
- Tests: `grid_skeletal` (two-bone reach, out-of-reach straightening, FABRIK,
  authored `IkGoalComponent` for both solvers, invalid-goal rejection, retarget
  scale/rotation, pose tolerance, clip reduction, `.fanim` round trip and
  corrupt-hash rejection).

## Navigation

`Source/Engine/Navigation/Navigation.hpp/.cpp` defines `NavMap` (128x128 cost
grid), A* with a decrease-key binary heap, partial paths, `FollowPath`, and
`NavigationSystem` with fixed-capacity agents that repath on target movement or
on an interval. The repath skips the current cell centre so an agent never walks
backwards.

- ECS split: `NavAgentSettings` is the reflected, serializable authoring
  component (target, speed, arrival radius, intervals, flags);
  `NavAgentComponent` holds only transient path state and is never written to a
  scene. The system performs the structural pass (creating runtime state for a
  settings-only entity) outside its view iteration.
- DOD: `MaxRepathsPerFrame` caps A* solves per fixed step. Agents that miss the
  budget keep their path and are marked `kNavAgentFlagRepathDeferred`, so a crowd
  of new targets cannot spike one frame. Agents standing on their target are
  excluded from the budget.
- Test: `navigation` (rect/hex paths, blocked maps, tilemap-derived maps,
  settings-only entity gets runtime state, arrival, blocked goal, repath budget
  and the deferred flag).
