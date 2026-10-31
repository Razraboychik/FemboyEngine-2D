# FemboyEngine2D Code Style Standard

This document is the normative style contract for the FemboyEngine2D source
tree. It applies to the engine, editor, applications, tools, tests and the C++
headers shipped to games. Documentation tooling follows the same language and
repository rules where they apply.

The contract has three goals:

1. make public names unambiguous while reading a call site;
2. keep data layouts explicit and stable across snapshots, scenes and plugins;
3. keep the tree free of formatting noise, comments that do not describe code,
   and generated files that do not belong in the repository.

## Language and toolchain

- Language: C++26 as enabled by the project CMake configuration.
- Validated compilers: Clang 19+ and GCC 15+; the current CI baseline is
  clang-cl 23.1.x with the MSVC standard library on Windows.
- Build system: CMake 3.28+ with Ninja.
- File suffixes: `.hpp` for C++ headers, `.cpp` for C++ translation units,
  `.h` and `.c` for the C plugin ABI, `.mm` for the Objective-C++ platform
  bridges, `.metal` for Metal shaders and `.comp`, `.vert`, `.frag` for GLSL.
- License: every file is covered by the repository MPL 2.0 license. Do not add
  per-file license headers that duplicate the root license.

## Architecture constraints

The engine is data-oriented:

- components are plain, trivially copyable, standard-layout data;
- systems are free functions or small stateless classes that operate on those
  components through the ECS registry;
- the frame path does not allocate, does not use `std::function` and does not
  build hidden object graphs;
- rendering submits a flat `RenderItem` array through one RHI packet;
- component order and field types are part of the serialized layout.

A change that adds ownership, virtual dispatch or per-frame allocation to a
component is a data-layout change, not a style change, and needs a dedicated
review.

## File and directory naming

- Every source file stem is PascalCase: `Vector2D.hpp`, `NetRollbackSession.cpp`,
  `ComponentRegistry.cpp`.
- Headers use `.hpp`; implementation files use `.cpp`.
- Public headers live in the module directory under `Source/Engine/<Module>/`.
- Editor headers live under `Source/Editor/Panels/` or
  `Source/Editor/Infrastructure/`.
- Test executables use the `fby_<name>_test` target name; the test source file
  remains PascalCase, for example `NetRollbackTest.cpp`.
- Do not put a translation unit directly under `Source/Engine/` or
  `Source/Editor/`.
- Generated source files are written to the build tree, never into `Source/`.

## Identifier naming

Public names use PascalCase without underscores:

| Kind | Rule | Example |
|---|---|---|
| Namespace | PascalCase, nested by `::` | `Fby::Ecs`, `Fby::Rhi::Vk` |
| Type, class, struct, union | PascalCase | `RenderItem`, `SystemGraph` |
| Enum type and enumerator | PascalCase | `CommandKind::Destroy` |
| Type alias at namespace scope | PascalCase | `Fby::FlatMap` |
| Function and method | PascalCase | `RestoreSnapshot`, `DestroyAllBodies` |
| Public component field | PascalCase | `Transform2D::Position` |
| Reflected field name | PascalCase | `FBY_FIELD(Sprite2D, MaterialBucket, UInt32)` |
| File stem | PascalCase | `NetRollback.cpp` |

Forbidden in public names:

- leading or trailing underscores;
- internal underscores of any kind;
- lowerCamelCase;
- snake_case;
- Hungarian notation;
- abbreviations that are not established in the engine vocabulary.

Acronyms are capitalised as words: `AICommandKind`, `AISystem`,
`RHI` inside a longer name follows the surrounding word, for example
`RhiDevice`. Do not write `Ai`, `ai_`, `rhi_device` or `RHI_Device`.

### Prefixed internal names

The following prefixes are part of the internal convention and are not
PascalCase violations. The characters after the prefix must still be PascalCase.

| Prefix | Applies to | Example |
|---|---|---|
| `In` | Input parameter | `InRegistry` |
| `Out` | Output parameter | `OutBytes` |
| `Scoped` | Local variable | `ScopedParent` |
| `M` | Non-public data member | `MEntries` |
| `k` | Compile-time constant | `kMaxComponents` |
| `FBY_` | Public macro | `FBY_ASSERT` |
| `fby_` | Frozen C ABI symbol | `fby_plugin_entry` |

Members of registered components are data, not private state. They use plain
PascalCase (`Position`, `UVMin`, `MaterialBucket`) and never the `M` prefix.

### Exceptions

These names are intentionally outside the PascalCase rule:

- `main` is the C++ entry point;
- operators and conversion functions use the language spelling;
- `Fby::FlatMap` exposes the standard-library-style member names `begin`,
  `end`, `empty`, `size`, `clear`, `find`, `count`, `at`, `insert` and
  `lower_bound` so it can be used as a container;
- the C ABI symbols prefixed `fby_` in `PluginAPI.h` are frozen binary
  interface and cannot be renamed without a new ABI version;
- third-party code under `third_party/` is never renamed.

System graph labels (the strings passed to `SystemGraph::Add`) are diagnostic
labels, not C++ identifiers. They may use snake_case when they name a phase or a
tool operation, but new labels should be PascalCase when there is no established
name.

## Component and layout rules

- Every component is trivially copyable and standard layout.
- Prefer fixed-size arrays, `Vector2D`, `Color32`, `Entity`, integer types and
  enums. Do not put `std::string`, `std::vector` or owning pointers in a
  component.
- Field order defines binary offsets. Do not reorder fields to make a pretty
  declaration.
- Adding or renaming a field changes `Reflect::LayoutHash<T>()` and therefore
  the snapshot/scene layout. A serialized field rename requires all of:
  1. `FBY_FIELD` macro update;
  2. TOML and scene-format migration;
  3. snapshot or project-format version increase;
  4. a test that loads the previous format or an explicit rejection test.
- New reflected components are registered in `ComponentRegistry.cpp` and get a
  test that round-trips every field.
- Unregistered helper PODs are still subject to naming rules and must not use
  underscores.

## Formatting

The canonical formatter configuration is [`.clang-format`](../../.clang-format).
The project uses:

- Allman braces on every control statement, function and type;
- four spaces per indent, never tabs;
- a 120-column limit;
- left-aligned pointers and references (`Type* Name`, `Type& Name`);
- case labels indented inside `switch`;
- one blank line between definitions and no empty line at the start of a block;
- no single-line `if`, `for`, `while`, function or namespace bodies.

`clang-format` owns whitespace and line breaking. Do not hand-align
declarations. Run it on touched files:

```bash
clang-format -i --style=file <files>
```

Do not run a repository-wide format pass in a functional change. Format the
files that the change already touches.

## Comments

All comments are English and ASCII. The only exception is a quoted external
notice or a UTF-8 test fixture that is data, not a comment.

Placement:

- a comment sits above the function, branch, block or group of fields it
  describes;
- never put a comment to the right of code;
- never put a footer comment such as `// End of namespace` below a closing
  brace;
- do not put a comment inside an empty block; move the explanation above the
  block or delete the empty block;
- do not insert a comment between two statements when it only restates the next
  statement;
- do not comment out code. Delete it; git keeps the history.

Content:

- explain why the code exists, an invariant, a wire-format rule or a lifetime;
- do not restate the function name;
- keep the comment close to the decision it explains;
- use `TODO` only with an actionable condition, not as a permanent marker.

ASCII-art banners, box drawing, long separator rules and decorative banners are
forbidden. A short `//` line is the only comment form in new code.

## Headers and includes

- Every header uses `#pragma once`.
- Include order is: matching header first in a `.cpp`, then engine headers, then
  third-party headers, then standard headers. The formatter preserves this order
  because `SortIncludes` is disabled.
- Include what you use. Do not rely on a transitive include.
- Forward-declare when the implementation does not need the complete type.
- Keep private implementation details in the `.cpp` where possible.

## ECS and systems

- A system is a named free function or a stateless class with a `Run` method.
- Systems receive `Ecs::SystemContext&` and take data from the registry.
- Do not store registry references, entity handles or frame pointers in a
  component.
- Reading a component field is direct member access; do not add getters for
  plain data.
- A system that mutates a component must be registered in the schedule or graph
  with a stable name.
- A system must be deterministic for a fixed input, tick and RNG state.

## Error handling and assertions

- Use `FBY_ASSERT` for programmer errors and impossible states.
- Use returned status/error values for recoverable input, file and network
  failures.
- Do not throw from a frame-path system.
- Do not use exceptions to control normal flow.
- Log a failure with the subsystem name and the operation, not a generic
  "failed" message.

## Repository hygiene

Never commit:

- build trees such as `build/`, `build-*`, `out/` or `cmake-build-*`;
- object files, libraries, PDBs, import libraries, `.obj`, `.o`, `.ilk`,
  `.idb`, `.exp`;
- logs, crash dumps, screenshots, `.bak`, `.tmp` or editor caches;
- IDE state such as `.vs/`, `.vscode/`, `*.user`, `.idea/`;
- local editor state such as `imgui.ini` or `editor.ini`;
- Python `__pycache__/` directories.

These paths are ignored by git and must not appear under `Source/`. The style
gate treats a junk file or junk directory as a failure.

## Enforcement

The mandatory gates are:

```bash
python Source/Tools/Dev/CheckStyle.py
clang-tidy -p build <file>
```

`CheckStyle.py` checks file stems, type and alias names, namespaces, reflected
field names, Allman braces, braced control bodies, trailing and footer comments,
non-ASCII comments, ASCII-art comments, Cyrillic text, RTTI identifiers,
`Vec2`/`Vec3`/`Vec4` and repository junk.

[`.clang-tidy`](../../.clang-tidy) mirrors the naming and brace rules. In
clang-tidy, `CamelCase` means UpperCamel/PascalCase; `camelBack` is the lower
form.

The full release gate additionally runs the build, the complete CTest suite,
the documentation checks and the dependency lock.

## Review checklist

Before a change is ready:

- every new public name is PascalCase and underscore-free;
- every new field in a reflected component has a matching `FBY_FIELD`;
- braces are Allman and every control body is braced;
- comments are English, ASCII and above the code they describe;
- no comment sits to the right of code or below a closing brace;
- no build artifact, log, editor cache or generated document is in the change
  unless it is a checked-in documentation edition;
- `CheckStyle.py` passes;
- the touched files are formatted with `.clang-format`;
- the change updates or adds a test when it changes behaviour or a layout.
