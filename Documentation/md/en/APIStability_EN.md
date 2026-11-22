# API and Data Format Stability

The engine keeps three separate stability contracts. They move independently so a
renderer change does not force a scene migration and a mod cannot observe an
internal C++ layout.

## 1. Stable host API

`Engine/Core/ApiVersion.hpp` defines `kEngineApiVersion`. A host or editor
extension targets that value and uses the headers named as stable in the
documentation: the C plugin ABI in `Engine/Core/PluginAPI.h`, the safe mod ABI in
`Engine/Modding/ModAPI.h`, the scene format in `Engine/ECS/Scene.hpp` and the
snapshot format in `Engine/ECS/Snapshot.hpp`.

Everything else under `Source/Engine` is internal. Internal headers may change in
any release without a deprecation cycle; including them from a plugin is not a
supported contract.

## 2. Safe mod ABI

`FbyModApi` is a C structure with a leading `struct_size` and `abi_version`.
Callers use `FbyModApiHas` before touching an appended field. The ABI never
passes `std::string`, `std::vector`, pointers into ECS chunks or C++ objects
across the boundary; entities are opaque 32-bit handles and commands are applied
at a safe flush point.

A new capability appends a function pointer and increments the minor ABI value.
Removing or reordering an existing field is a breaking change and requires a new
major ABI line.

## 3. Serialized formats

| Format | Constant | Current value |
|---|---|---|
| Scene TOML and binary | `Scene::kSceneVersion` | 2 |
| ECS snapshot | `Ecs::kSnapshotVersion` | 5 |
| Prefab binary | `PrefabSystem::kBinaryVersion` | 1 |
| Command log | `Ecs::kCommandLogVersion` | 4 |
| Skeleton asset | `Ecs::kFesklVersion` | 1 |

A reader rejects a format newer than it understands and reports the expected
version. A migration is added before the writer starts emitting the new version.
Old files are never rewritten in place during a read.

## 4. Deprecation

A public declaration that is being replaced is marked with `FBY_DEPRECATED` and
the message names the replacement. It remains available for at least one minor
line. The changelog records the removal release; the contract test keeps the
version constants and stable type layout honest.

## 5. Contract tests

`fby_api_contract_test` checks:

- the mod ABI constant matches `FBY_MOD_API_VERSION`;
- scene and snapshot constants match their canonical headers;
- stable ABI records are standard-layout;
- a complete `FbyModApi` passes `FbyModApiHas`, while a probe past
  `struct_size` and a null API fail;
- the current version values are printed in the test log for release records.

Any change to a stable record layout must update the test and the version
constant in the same commit.
