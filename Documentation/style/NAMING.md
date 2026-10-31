# Naming conventions

FemboyEngine2D uses PascalCase for every public name, C++ type and namespace.
This document is the naming contract and the migration policy. The complete code
style contract is [CodeStyle.md](CodeStyle.md).

## Public CMake targets

Use these aliases:

| Public alias | Purpose |
|---|---|
| `FemboyEngine2D::Engine` | Runtime library. Link this from a game. |
| `FemboyEngine2D::Editor` | Editor library (ImGui/ImNodes). |
| `FemboyEngine2D::AssetCook` | BC7/ASTC texture cooker. |
| `FemboyEngine2D::AssetLib` | Pack/localization compiler. |
| `FemboyEngine2D::ShaderCompilerCore` | Runtime GLSL/SPIR-V compiler used by the editor. |

There are no `Runtime` or `FemboyEngine::*` compatibility aliases. The old
names were removed, not deprecated. `FemboyEngine2D::Engine` is the only alias
for the runtime target.

Internal CMake targets (`fby_engine`, `fby_editor`, `fby_shaderc_core`,
`fby_fempak`, ...) and executable output names (`fby_editor`, `fby_fempak`, ...)
stay lowercase. They are implementation details, not API, and renaming them
would break scripts, IDE launch configurations, crash reports and packaging
jobs.

## C++ names

- Namespaces: PascalCase, including nested modules.
  - `Fby`
  - `Fby::Ecs`, `Fby::Rhi`, `Fby::Rhi::Vk`, `Fby::Ui`, `Fby::Assets`
  - `Fby::Video`, `Fby::Net`, `Fby::Scene`, `Fby::Scene::Fb`, `Fby::Scene::Detail`
  - `Fby::Render`, `Fby::Editor`, `Fby::Script`, `Fby::Script::Aot`, `Fby::Weather`
  - `Fby::Platform`, `Fby::Plugin`, `Fby::Audio`, `Fby::Animation`
  - `Fby::Skeleton`, `Fby::SkeletalAnimation`, `Fby::Shaderc`, `Fby::Reflect`,
    `Fby::Noise`, `Fby::Crypto`, `Fby::Voice`, `Fby::Notices`
  - `Fby::Build`, `Fby::Loc`, `Fby::Gameplay`, `Fby::Cook`, `Fby::Test`
  - `Fby::Detail`, `Fby::Change`, `Fby::Prefab`
- Types, enums and enum values: PascalCase (`RenderItem`, `SystemGraph`,
  `SpriteAnimationComponent`, `SpriteSheetComponent`, `AICommandComponent`,
  `AICommandKind`, `AISystem`). Namespace-scope type aliases follow the same
  rule (`Fby::FlatMap`, not `flat_map`).
- Functions and methods: PascalCase (`Build`, `RestoreSnapshot`,
  `DestroyAllBodies`). The C++ entry point `main`, operators and the
  standard-library-style container methods in `Fby::FlatMap` are the deliberate
  exceptions.
- Files: PascalCase stems, enforced by `Source/Tools/Dev/CheckStyle.py`.
- Locals use the `Scoped*` prefix, members `M*`, constants `k*`. These are
  enforced by `.clang-tidy` and the style gate.
- Booleans: predicates read `Is*`, `Has*` or `Can*` (`IsPlaying`, `HasFocus`,
  `CanRetry`); plain state flags keep their PascalCase name (`Loop`, `Spatial`).
  There is no `b` prefix: serialized field names are part of the frozen layout
  and of the TOML keys, so a prefix would break both.
- Access specifiers: new classes in the network, voice and notice modules mark
  their non-public data `protected` (the owner's preference); `private` stays
  where it already is. `struct` members stay public by design: components are
  flat data. A `protected` or `private` member is always reachable inside its
  own class; the specifier only governs derived types and external code.
- `AI` is always spelled with two capitals: `AIStateTreeCanvasPanel`,
  `AICommandComponent`, `AICommandKind`, `AISystem`. `Airborne` and `Aiming`
  are ordinary words, not the AI acronym.

## Serialized component layout

Component members are PascalCase in 0.1.0-alpha.1/layout v4:

- `Transform2D::Position`, `Scale`, `Rotation`;
- `Velocity::Value`;
- `Sprite2D::Size`, `UVMin`, `UVMax`, `Layer`, `Color`, `Flags`;
- `SpriteAnimationComponent::TextureAtlas`, `StartFrame`, `FrameCount`,
  `CurrentFrame`, `FrameDuration`, `ElapsedTime`, `IsLooping`, `IsPlaying`;
- `SkeletonComponent`/`SkinComponent` SoA fields;
- all other registered components.

The 1.0 contract is:

- fields are flat, standard-layout, trivially copyable data;
- member order and types define binary offsets;
- `Fby::Reflect::LayoutHash<T>()` covers the field names;
- scene/prefab TOML uses the same Pascal field names as keys;
- Snapshot v4 and project format 4 record the new layout.

There is no legacy lowercase TOML key decoder and no old snapshot path. The
engine is pre-release; old `.femeta` and scene files are regenerated.

## Animation naming

- Skeletal animation: `Fby::SkeletalAnimation::Evaluate/Deform`,
  `SkeletonComponent`, `SkinComponent`. The data is SoA and flat.
- Frame-by-frame animation: `SpriteAnimationComponent`,
  `SpriteSheetComponent`, `SpriteAnimationSystem`, `SpriteFrameEvent`,
  `SpriteAnimationEventQueue`. The data is flat POD; the system writes UVs only.
- `AnimationGraphCanvasPanel` is the visual animation state machine.
- `AnimationTimelinePanel` is the clip timeline.
- `SpriteAnimationPanel` is the per-entity flipbook inspector.

The old animation names are gone from C++ and editor code.

## Project descriptor and attribution

- A project is one `project.femproj` file (TOML, `format_version = 4`). The
  `.femproj` suffix matches the rest of the asset family (`.femeta`, `.fempak`,
  `.femarc`, `.fanim`). There is no `.fbyproj` compatibility path.
- Shipped attribution is generated from `third_party/notices.toml` into
  `THIRD_PARTY_NOTICES.md` and `Fby::Notices`; both are committed, and
  `Documentation/Tools/GenerateThirdPartyNotices.py --check` fails when they are
  stale.

## Frozen ABI

The C ABI symbols `fby_*` in `PluginAPI.h` are not a namespace and are not
legacy. They are the stable binary plugin contract and must not be renamed
without a new ABI version.

## Enforcement

```bash
python Source/Tools/Dev/CheckStyle.py
```

The gate checks file stems, type/alias/namespace names, reflected
`FBY_FIELD` names, `Vector2D/3D/4D`, no `Vec2/Vec3/Vec4`, no
`dynamic_cast`/`typeid`, Allman braces, `In*`/`Out*`/`Scoped*` naming and the
required `Definitions.hpp` contract. It also rejects trailing comments, footer
markers such as `// End of namespace`, non-English comments and ASCII-art
banners in C++, Python, CMake, shell, PowerShell and CI YAML files. CMake
aliases are checked by the build: every shipped target links
`FemboyEngine2D::Engine`.

Layout is owned by [`.clang-format`](../../.clang-format); naming and braces are
also mirrored in [`.clang-tidy`](../../.clang-tidy). Use:

```bash
clang-format -i <files>
python Source/Tools/Dev/CheckStyle.py
```

Neither tool can move a comment above a block, so trailing comments are
forbidden by the gate instead of being auto-reformatted.

## Audit boundary

The enforced surface is every public C++ name: file stems, namespaces, types,
namespace-scope aliases, enums, functions and methods, registered component
fields and reflected `FBY_FIELD` names. The gate also enforces the comment and
script rules above.

Two categories are deliberately outside a blind rename:

- The C ABI symbols `fby_*` in `PluginAPI.h` are the frozen plugin contract.
- Non-reflected implementation PODs (audio graph commands, scene helper
  records, snapshot headers) still contain some lowerCamel fields. They are
  not part of the component/TOML contract and must be migrated in a dedicated
  layout-neutral pass, not mixed with a serialized-layout change.
