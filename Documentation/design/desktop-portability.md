# Desktop portability

FemboyEngine2D is a desktop engine. One project opens on Windows, Linux and
macOS; each OS builds its own native player and editor from the same sources.

## Product contract

- A project is a directory with `project.femproj`, scenes, scripts, materials and
  assets. It is not tied to a machine account or an absolute path.
- Source files are the portable part. Build trees, cooked archives, shader
  caches and editor-local state are disposable and per-machine.
- The same scene, script and asset ids resolve on every supported OS.
- A native build is produced on the OS that will run it: Windows on Windows,
  Linux on Linux, macOS on macOS. Cross-compilation of a desktop game is not a
  substitute for a native build and is not the default workflow.

## Project files

```text
project/
  project.femproj
  assets/
  scenes/
  scripts/
  materials/
  ...
```

Rules:

- all paths in project data are relative and use `/`;
- no drive letters, UNC roots or `/home/<user>` paths;
- asset ids are content-based, not path-based;
- project settings are portable game-facing data; editor settings form a
  separate typed structure, mirrored to `editor.ini`, and never change shipped
  game behaviour;
- line endings are normalized in text sources; binary assets are untouched;
- file names that differ only by case are rejected by the bundle exporter because
  Linux is case-sensitive while Windows and macOS defaults are not;
- a project bundle excludes `build/`, `Saved/`, `.git`, cooked archives and
  editor-local metadata.

## Portable project archive

`ProjectBundle` writes a source-only `.fbyproj` archive:

- magic/version header, fixed entry table, relative path table and payload;
- FNV-1a checksum per file;
- `project.femproj` is required;
- imported paths are rejected if absolute or containing `..`;
- the destination must be empty unless overwrite is requested;
- entries are sorted by path, and case-folded duplicate paths are rejected;
- the destination archive is byte-for-byte deterministic.

The archive is intentionally uncompressed: a project moved between Windows,
Linux and macOS has identical source bytes, entry order and checksums.

## Build matrix

| OS | Compiler | RHI | Build |
|---|---|---|---|
| Windows 10/11 x64 | clang-cl 23.1.4 | Vulkan | `windows-release` |
| Linux x64 | Clang 19+ / GCC 16+ | Vulkan | `linux-release` |
| macOS 14+ x64/arm64 | Homebrew Clang 19+ | Metal | `macos-release` |

Presets configure the editor, sandbox, tools and tests. Each preset writes its
own build tree; no build directory is shared between OSes.

## Platform-dependent data

- Shaders: Vulkan targets consume SPIR-V; macOS consumes the embedded Metal
  library. Material and scene sources stay shared.
- Textures: desktop cook uses BC7; ASTC is not a desktop requirement.
- Audio: WAV/Ogg sources are shared; output devices are selected locally.
- Scripts: the VM bytecode is shared. AOT modules are built per OS
  (`.dll`, `.so`, `.dylib`) and only loaded by the matching player.
- Save data and settings use the OS application-data location, never the
  project root.

## Workflow

1. Create or open a project on any desktop OS.
2. Edit scenes, scripts and assets.
3. Export `project.fbyproj` when moving to another machine.
4. Import the archive on the other OS.
5. Build the native player/editor with that OS preset.
6. Cook assets and package for the target OS.

A project can move in both directions. The importing OS reports unsupported
tools or missing target capabilities; it does not rewrite source data to fit
the local machine.

## Delivery phases

1. **Contract** — path rules, project bundle, checksums, descriptor validation.
2. **Build matrix** — Windows/Linux/macOS presets and CI jobs.
3. **Editor portability** — project open/save on all three OSes, local settings
   separated from project settings.
4. **Asset/cook portability** — deterministic cook, per-target shader and
   texture outputs, no cooked data in the project archive.
5. **Release** — native packages and store artifacts per OS.

## Acceptance criteria

- a project created on Windows opens on Linux and macOS without conversion;
- export/import round-trips source bytes and asset references;
- no absolute path or machine name appears in a project;
- the same scene plays identically on all three desktop builds;
- cooked outputs never become the source of truth;
- each OS can rebuild from the project archive and its own toolchain.
