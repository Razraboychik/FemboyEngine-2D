# FCS: Femboy Component System

[English](fcs.md) | [Русский](fcs_ru.md)

FCS is the internal chunked ECS storage for the 0.5.0 line and the only
runtime ECS backend; the editor, plugin ABI and the full test suite use it. The runtime uses the engine's own 32-bit `Fby::Entity` handle, so scenes,
snapshots and component fields keep the same layout without EnTT.

## Layout

- `Source/Engine/ECS/FCS/World.hpp` - `World`, `Archetype`, `Chunk`, `Entity`.
- `Source/Engine/ECS/FCS/CommandBuffer.hpp` - deferred structural commands.
- `Source/Engine/ECS/FCS/Registry.hpp` - the engine-facing adapter with
  `create/destroy/emplace/remove/try_get/view/ctx/storage`.

One archetype owns a signature and a list of 16 KiB chunks. Every component has
a contiguous column; a system walks columns in chunk order. The entity wire
handle is 20 index bits and 12 generation bits, so a recycled slot is rejected
for 4096 cycles.

## Building with FCS

FCS is the default backend:

```bat
cmake -S . -B build -G Ninja ^
      -DCMAKE_CXX_COMPILER=clang++ ^
      -DFBY_BUILD_TESTS=ON
cmake --build build --parallel 8
```

The runtime always compiles the FCS registry; EnTT was removed from the tree
after the comparison baseline was recorded.

## Verified so far

The FCS backend builds the engine, the editor and the plugin fixtures, and
passes all 222 CTest entries with the editor and tools enabled. The last fixes were component-reference freshness
in gameplay systems and zeroing the padding in physics snapshot images so the
rollback digest is stable. The core migration tests pass:

- `fcs_world`, `fcs_registry`;
- `p3_integration` (scene, snapshot, UI, physics);
- `hierarchy`, `lighting`, `rollback_replay`;
- `net_replication_ecs`, `physics_backend`, `sdf_physics`;
- `text_shape_system`, `frame_composer`, `render_weather`, `anim_graph`,
  `sdf_character`.

The historical FCS-vs-EnTT baseline at 100k entities and 50 passes was FCS about
4.0 ms and EnTT about 15.1 ms on the reference machine. The number is kept in
[performance baseline](performance-baseline.md); the fixture is gone.

## Remaining migration

- the runtime uses `Fby::Entity`; EnTT and its third-party tree are no longer in
the build.

The editor and plugin ABI already use the backend alias; `FbyHostApi::registry`
stays opaque and is passed through unchanged.
