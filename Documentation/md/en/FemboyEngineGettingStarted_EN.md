# FemboyEngine 0.6.0-alpha.1 — how to make a game

This document lists what to install and which files must sit next to the engine to run the editor, the runtime and the development tools on Windows, Linux and macOS.

## Supported platforms

| Platform | Editor | Game runtime | Toolchain |
|---|---|---|---|
| Windows 10/11 x64 | yes | yes | clang-cl 19.1+ (primary), MSVC 19.40+, GCC 14.1+ |
| Linux x64 | yes | yes | Clang 19.1+, GCC 14.1+ |
| macOS 14+ (Apple Silicon and Intel) | yes | yes | AppleClang 16+ (Xcode 16+), Metal backend |

The editor and the game runtime target the three desktop platforms above. SDL3 supplies the window, input and application lifecycle.

## Required software

**MSVC 19.40+ is supported as an alternative.** On Windows the project builds with clang-cl from LLVM as the reference toolchain, or with MSVC (`cl.exe`) when a team standardizes on it. Both consume the Windows SDK and the MSVC standard library; clang-cl remains the release gate. The setup script installs the **latest stable LLVM**, the latest Vulkan SDK and the latest archiver when they are missing; CI pins the reference versions below for reproducibility.

| Tool | Minimum | CI reference | Purpose |
|---|---|---|---|
| CMake | 3.22 | 3.31.6 | configure and build |
| Ninja | 1.11 | 1.12.1 | build generator |
| LLVM / clang-cl | 19.1 | 23.1.4 | primary Windows compiler; setup installs the latest stable |
| MSVC (`cl.exe`) | 19.40 | 19.44 | Visual Studio-native Windows compiler with a dedicated CI job |
| Clang (Linux) | 19.1 | 23.1.x | primary Linux compiler; distro package is usually the latest available |
| GCC | 14.1 | 16.2 | second compiler for portability checks |
| Python | 3.11 | 3.13 | documentation and notices generation |
| Git | 2.40 | — | clone and version control |
| Vulkan Loader | 1.4 | 1.4 | runtime on Windows and Linux |
| MoltenVK | 1.2 | — | Vulkan over Metal on macOS (or the Metal backend directly) |
| 7-Zip | 21 | 25.01 | unpack the LZMA SDK (otherwise the built-in FbyLZ is used) |
| Xcode | 16.0 | 16.0 | macOS builds (AppleClang 16; Homebrew LLVM is newer) |

The language standard is C++23, so an older compiler will not work even when its CMake version matches.

## Quick start with the setup scripts

The scripts live under `SetupScripts/<OS>/` and are split by OS and software
group. The one-click scripts install everything; the component scripts install
one group.

| Group | Windows | macOS | Linux |
|---|---|---|---|
| C++ toolchain, CMake and source dependencies | `SetupScripts\Windows\SetupCpp.bat` | `./SetupScripts/macOS/SetupCpp.command` | `./SetupScripts/Linux/SetupCpp.sh` |
| 7-Zip archiver | `SetupScripts\Windows\SetupArchiver.bat` | `./SetupScripts/macOS/SetupArchiver.command` | `./SetupScripts/Linux/SetupArchiver.sh` |
| Optional DLSS/FidelityFX/XeSS SDKs | `SetupScripts\Windows\Setup.bat -WithVendorSdks -AcceptVendorLicenses` | `./SetupScripts/macOS/Setup.command --with-vendor-sdks --accept-vendor-licenses` | `./SetupScripts/Linux/Setup.sh --with-vendor-sdks --accept-vendor-licenses` |

The all-in-one entry points are `SetupScripts/Windows/Setup.bat`,
`SetupScripts/macOS/Setup.command` and `SetupScripts/Linux/Setup.sh`.
The C++ scripts install the compiler, CMake, Ninja, the Vulkan headers and the
pinned source dependencies. On Windows, `SetupScripts/Windows/SetupCpp.bat` installs the official LLVM
19.1 MSI and verifies the `clang-cl` version; the Unix scripts install the
distribution Clang (19+). The archiver scripts install 7-Zip. Vendor SDKs are
downloaded by the all-in-one Setup only after the corresponding licence flags;
nothing is part of the engine repository.

Useful flags: `--skip-system` leaves system packages alone (a CI image already
has the tools); `--force` re-downloads every C++ archive. The C++ script writes
`third_party/dependencies.lock` with the pinned versions and archive hashes.
At the end it prints a summary of the installed toolchains.

### From a GitHub archive

1. Open the repository page and choose **Code > Download ZIP**.
2. Extract the archive to a directory without spaces or non-ASCII characters.
3. Run the host OS `SetupCpp` and `SetupArchiver` entry points from the table above.
4. Optionally pass the vendor licence flags to the host `Setup` entry point.
5. Configure and build:

```
cmake --preset dev-clang
cmake --build --preset dev-clang
ctest --preset dev-clang --output-on-failure
```

The archive contains no vendor SDK. The C++ script installs the toolchain and
materialises the pinned sources; the archiver script installs 7-Zip. The
upscaler scripts are only needed when a DLSS/FidelityFX backend is explicitly
enabled. Without them, FSR1, the FSR2-compatible core path and custom frame
generation build and run with runtime fallback.

## Manual installation

Without the script the steps are the same: install a compiler, CMake, Ninja, the Vulkan headers, and lay out the third-party sources.

### Windows

1. Install the latest stable LLVM (or any 19.1+) and add its `bin` directory to `PATH`.
2. Install Visual Studio Build Tools with the "C++ Build Tools" workload (Windows SDK and standard library headers). clang-cl uses the MSVC STL; the Microsoft compiler is only needed for the separate MSVC build.
3. Install CMake 3.31 and Ninja 1.12 (the Build Tools copy is fine).
4. Install the Vulkan SDK 1.4 — it provides `vulkan-1.dll` and `VULKAN_SDK`.
5. Install 7-Zip (optional).
6. Download the archives the script would fetch and unpack them into the same `third_party/` directories: SDL3 `release-3.4.18`, Vulkan-Headers `v1.4.366`, glslang `15.3.0`, miniaudio `0.11.23`, Box2D `v3.2.0`, RNNoise `0.1.1`, Monocypher `4.0.3`. HarfBuzz and SheenBidi need no download: they are already in the repository.

### Linux

```
sudo apt install build-essential clang cmake ninja-build git pkg-config libvulkan-dev vulkan-tools p7zip-full
```

Then lay out the same archives as on Windows. GCC 14.1 comes from the distribution or a separate build; Clang is enough for the primary build.

### macOS

```
xcode-select --install
brew install cmake ninja git p7zip glslang vulkan-headers molten-vk llvm
```

Xcode Command Line Tools are mandatory: they provide `clang++`, the Metal frameworks and the SDK headers. The Metal backend needs no Vulkan SDK.

## Building

From the repository root:

```
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFBY_BUILD_TESTS=ON
cmake --build build-dev
ctest --test-dir build-dev --output-on-failure
```

On Windows the same commands run inside a `vcvars64` environment; `SetupScripts/Windows/SetupWindows.ps1` prepares the dependencies and the toolchain. Main CMake options: `FBY_BUILD_EDITOR`, `FBY_BUILD_TESTS`, `FBY_BUILD_TOOLS`, `FBY_BUILD_SANDBOX`, `FBY_BUILD_SERVER`, `FBY_BUILD_LOBBY`, `FBY_WITH_ASAN`, `FBY_RHI_BACKEND` (`auto`, `vulkan`, `metal`, `null`).

The executables are grouped by purpose:

| Directory | Contents |
|---|---|
| `build-dev/bin` | applications: `fby_editor`, `fby_sandbox`, `fby_server`, `fby_lobby`, platform plugins and `plugins/` |
| `build-dev/bin/tools` | console tools: `fby_fontc`, `fby_textc`, `fby_fempak`, `fby_scene_tool`, `fby_template_gen`, `fby_aotc`, `fby_loc_tool`, `fby_shaderc` |
| `build-dev/bin/tests` | test executables (`fby_*_test`) that `ctest` runs |
| `build-dev/bin/lib` | static libraries |
| `build-dev/bin/plugins` | native engine plugins |

The `*.pdb` files next to the executables are debug symbols written by the compiler. They are only used by a debugger and are not needed to run anything; a release package does not have to ship them.

## Code editor and IDE

The engine does not ship its own text editor. Development uses an external editor that understands the CMake project and `compile_commands.json`.

Recommended setup:

- **VS Code**;
- the **clangd** extension;
- the **CMake Tools** extension;
- **CodeLLDB** or `cppdbg` for debugging;
- RenderDoc for GPU debugging.

CLion also works: it understands CMake natively and can launch `fby_editor` and the tests. Visual Studio can be used as an IDE and for the MSVC build; see below.

### VS Code setup

1. Install VS Code.
2. Install the extensions:
   - `llvm-vs-code-extensions.vscode-clangd`;
   - `ms-vscode.cmake-tools`;
   - `vadimcn.vscode-lldb` or `ms-vscode.cpptools` for `cppdbg`.
3. Open the repository root (the folder that contains `CMakePresets.json`).
4. Disable the built-in IntelliSense; otherwise it competes with clangd:

```json
{
  "C_Cpp.intelliSenseEngine": "disabled"
}
```

5. Select the `dev-clang` or `dev-clangcl` CMake preset.
6. Configure:

```bat
cmake --preset dev-clang
```

7. Build:

```bat
cmake --build --preset dev-clang
```

8. Run tests:

```bat
ctest --preset dev-clang
```

The repository already contains:

- `CMakePresets.json` — the presets;
- `.clangd` — compile-database path;
- `.vscode/settings.json` — clangd and CMake Tools settings;
- `.vscode/launch.json` — editor and `frame_composer` launch targets.

### clangd

clangd must use the same Clang that builds the project:

```json
{
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build-audit",
    "--query-driver=C:/Users/<user>/AppData/Local/Programs/LLVM/bin/clang++.exe"
  ]
}
```

`--query-driver` lets clangd find the Windows SDK headers. If a different build directory is used, change `--compile-commands-dir` or `.clangd`.

Do not enable C/C++ IntelliSense and clangd at the same time: that produces two different diagnostic and navigation stacks.

### Build presets

| Preset | Compiler | Purpose |
|---|---|---|
| `dev-clang` | `clang++` | Debug, tests, `compile_commands.json` |
| `release-clang` | `clang++` | RelWithDebInfo, tests |
| `dev-clangcl` | `clang-cl` | Debug with the validated Windows toolchain |
| `audit-clang` | `clang++` | existing `build-audit` for clangd |

Different compilers must use **different build directories**. Do not reconfigure `build-audit` from clang++ to clang-cl: the CMakeCache belongs to the old toolchain.

### Debugging

For `cppdbg`, use gdb from MinGW/LLVM; CodeLLDB uses LLVM's lldb. Example:

```json
{
  "name": "Editor (Debug)",
  "type": "cppdbg",
  "request": "launch",
  "program": "${workspaceFolder}/build-dev/bin/fby_editor.exe",
  "cwd": "${workspaceFolder}",
  "MIMode": "gdb",
  "miDebuggerPath": "gdb.exe"
}
```

Use RenderDoc for GPU debugging: attach it to `fby_editor.exe` and capture a frame without embedding a debugger in the editor.

## Compiler selection

The 0.6.0 line supports **Clang 19.1+** (clang-cl on Windows, clang++ on Unix), **AppleClang 16+**, **GCC 14.1+** and **MSVC 19.40+**. clang-cl is the reference toolchain; the others are supported alternatives, not an obligation. CI runs clang-cl, clang, GCC and AppleClang with the full test suite; MSVC 19.40+ builds the same tree in its own Windows CI job.

- **Windows, primary:** clang-cl. It is Clang with the MSVC ABI, the Windows SDK and the MSVC standard library, and it works with the Visual Studio debugger and PDBs.
- **Windows, alternative:** MSVC (`cl.exe`). Choose it when the team standardizes on the Microsoft toolchain; CMake selects `/std:c++latest` and the MSVC warning set. RNNoise falls back to the spectral tier because its vendored C sources use variable-length arrays.
- **Linux, primary:** clang++. GCC 14.1 is the second compiler for portability checks.
- **macOS:** AppleClang 16+ from Xcode 16+ with the Metal backend.
- **One compiler on every platform:** Clang (clang-cl on Windows, clang++ elsewhere). It is the only toolchain that covers all three desktop platforms with one frontend.

The full matrix and the per-OS presets are in [build-workflow.md](../../design/build-workflow.md).

## Running the editor

The editor opens the project from its **current working directory**: it looks for `project.femproj`, mounts `assets/` as the virtual file system, writes project editor settings to `editor.ini` next to `project.femproj`, and keeps the ImGui layout in `Saved/Config/editor.ini`. Start it from the project root (the repository root for the demo project).

```
cd %USERPROFILE%\FemboyEngine
build-dev\bin\fby_editor.exe
```

Linux and macOS:

```
cd /path/to/project
./build-dev/bin/fby_editor
```

Everything the engine writes at runtime goes to `Saved/` in the working directory, so the project directory stays clean:

| Path | Contents |
|---|---|
| `Saved/Logs/<category>/<category>-YYYYMMDD-HHMMSS.log` | the full log of a run: `engine`, `editor`, `sandbox`, `server`, `lobby`. The 20 newest files per category are kept and older ones are removed automatically |
| `Saved/Crashes/<YYYYMMDD-HHMMSS>-<pid>/report.txt` | crash report: time, application, pid, reason, address and, on Linux and macOS, a backtrace |
| `Saved/Crashes/<...>/crash.dmp` | Windows minidump for a debugger |
| `Saved/Config/editor.ini` | editor settings and panel layout |
| `Saved/Cache/` | cache of intermediate data |

Environment variables: `FBY_STORAGE_DIR` moves the `Saved/` root (for example to a disk with space) and `FBY_LOG_FILE=0` disables the log file while the console keeps working.

### Files that must be present

| Path | Required | Purpose |
|---|---|---|
| `build-dev/bin/fby_editor` | yes | the editor itself |
| `assets/` | yes | fonts, localization, scenes, atlases, project configs |
| `assets/fonts/IBMPlexSans-Regular.ttf`, `...-Bold.ttf`, `...-Italic.ttf`, `...-BoldItalic.ttf`, `IBMPlexMono-Regular.ttf` | yes | the font family used by the editor and by scene text |
| `assets/fonts/IBMPlex.family.toml` | yes | family manifest with the five style slots |
| `assets/fonts/Inter.fbyfont` | yes | pre-baked atlas used by the legacy UI path |
| `assets/loc/ui.fbin` | yes | interface localization table |
| `assets/scenes/*.toml` | yes | project scenes |
| `project.femproj` | yes | project description: default scene, simulation, render, accessibility |
| `build-dev/` | yes | the CMake build tree: executables and build results live there |
| `build-dev/bin/fby_sandbox` | no | second process for networked Play-in-Editor |
| `Saved/` | created | logs, crash reports, caches and editor settings |
| `Saved/Config/editor.ini` | created | panel layout and editor settings for this machine |
| `build-dev/bin/plugins/` | no | native engine plugins |
| Vulkan Loader installed on the system | yes | window and GPU rendering on Windows and Linux |
| `Projects/` | no | default location for new projects |

### Selecting a project

A project is a directory with a `project.femproj` file and `assets/`, `configs/` subdirectories. New projects are created in `Projects/` inside the engine root; an existing one is opened from the editor dashboard by the path to `project.femproj` or to the project folder. There is no command-line argument for the project: the working directory decides, which is why a "project shortcut" is a shortcut with a different working directory.

## Development tools

Besides the editor, `build-dev/bin` contains separate executables. All of them are console programs that read files and need no GUI.

| Tool | Purpose | Files it needs |
|---|---|---|
| `fby_editor` | editor: scenes, assets, PIE, text preview | `assets/`, `project.femproj` |
| `fby_sandbox` | sandbox and Play-in-Editor client | `assets/` |
| `fby_server` | dedicated server | project directory if configured |
| `fby_lobby` | lobby and matchmaking service | none |
| `fby_fontc` | build a `.fbyfont` from a TTF/OTF | the input TTF |
| `fby_textc` | compile marked-up text into `.fbin` | the input `.toml` text |
| `fby_fempak` | pack assets into a `.fempak` | the asset directory |
| `fby_scene_tool` | validate and convert scenes | the scene `.toml` |
| `fby_template_gen` | generate a new project skeleton | destination directory |
| `fby_aotc` | nativize scripts (AOT) | project with bytecode |
| `fby_loc_tool` | validate and build localization tables | localization `.toml` |
| `fby_shaderc` | compile shaders to SPIR-V | `.vert`/`.frag`; the build calls it automatically |

The tools live in `build-dev/bin/tools`; the test executables in `build-dev/bin/tests`.

Build and test through the CMake presets; dependency setup lives in `SetupScripts/<OS>/*`.

## Shortcuts for fast launching

The working directory is the only thing that distinguishes "launching the engine" from "launching a project", so a shortcut must always set it explicitly.

### Windows

1. Right-click `build-dev\bin\fby_editor.exe` → Send to → Desktop (create shortcut).
2. Open the shortcut properties: "Start in" = project root (for example `%USERPROFILE%\FemboyEngine`), "Target" = full path to `fby_editor.exe`.
3. Rename it to "FemboyEngine" and drag it to the taskbar or the Start menu.
4. For a specific project, create a second shortcut with the same target and "Start in" = the project folder. That is the project selection without a dialog.

### macOS

1. Open the project folder in Finder.
2. Drag `fby_editor` (or the `.app` bundle, if built) to the Dock while holding `Command`.
3. For a specific project create a `.command` file with two lines: `cd /path/to/project` and `/path/to/fby_editor`; Finder runs it on double-click and the alias can live in the Dock.
4. `SetupScripts/macOS/Setup.command` starts the same way — by double-click.

### Linux

Create `~/.local/share/applications/fby-editor.desktop`:

```
[Desktop Entry]
Type=Application
Name=FemboyEngine
Exec=/path/to/fby_editor %f
Path=/path/to/project
Icon=/path/to/Logo.png
Terminal=false
Categories=Development;
```

Then pin it to the dock or the panel. A second `.desktop` file with a different `Path` gives a shortcut to another project.

## Verifying the toolchain

The exact versions can be checked with:

```
cmake --version
ninja --version
```

On macOS, `xcodebuild -version` reports the Xcode and SDK versions.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| The editor shows no assets | it was started outside the project root | start it from the directory with `assets/` and `project.femproj` |
| Vulkan error at startup | the system has no Vulkan Loader | install the Vulkan Runtime or build with `-DFBY_RHI_BACKEND=null` |
| The editor or a game closed without a message | the process crashed | open `Saved/Crashes/` in the working directory: `report.txt` and `crash.dmp` |
| A run log is needed for a report | logs are written to a file now | take the newest file from `Saved/Logs/<category>/` |
| `glslang` missing during the build | `third_party/glslang-src` is missing | run the setup script again |
| No fonts in the editor | the TTFs or the family manifest are missing | restore `assets/fonts/` from the repository |
| CMake reports a missing harfbuzz or SheenBidi | the `third_party/` directory was damaged or removed | restore `third_party/harfbuzz-download/` and `third_party/sheenbidi-download/` from the repository |
| The Windows build fails on headers | the Windows SDK is missing | install Build Tools with the C++ workload |

## From an empty project to a playable scene

A new project contains one empty `assets/scenes/main.toml`, an empty profile and no forced
actors. The editor does not decide the game for you.

1. Open the dashboard and create a project. No genre template is applied.
2. In **Project Settings → Physics** choose the default view profile: Default, Side View,
   Top-Down, Isometric or Hex Isometric.
3. Open `scenes/main.toml` and add only the entities the game needs. A minimal side-view start
   is a camera, one sprite and one ground sprite.

```toml
version = 2
name = "main"
author = "you"
profile = "side-view"

[[entity]]
id = 0
name = "camera"

[[entity.components]]
type = "Camera2D"
Center = [640.0, 360.0]
ViewSize = [1280.0, 720.0]
Zoom = 1.0
Rotation = 0.0

[[entity]]
id = 1
name = "player"

[[entity.components]]
type = "Transform2D"
Position = [320.0, 240.0]
Scale = [1.0, 1.0]
Rotation = 0.0

[[entity.components]]
type = "Sprite2D"
Size = [48.0, 48.0]
UVMin = [0.0, 0.0]
UVMax = [1.0, 1.0]
Layer = 1
Color = "#4FC3F7FF"
Flags = 0
```

4. Assign the texture atlas through the inspector. `UVMin`/`UVMax` pick the cell.
5. Add a `RigidBody2D` and `Collider2D` only if the entity should be simulated; a camera or a
   static background does not need them.
6. Press Play (PIE). The scene profile is applied; if it is `Default`, the project profile is
   used.

For a top-down scene set `profile = "top-down"` and leave gravity alone: the profile maps to
zero gravity. For a hex scene use `profile = "hex"`; adjacency and occupancy stay in the grid
and tilemap layers, while loose objects can still use physics.

### Tilemaps and Tiled

Create a `TilemapComponent` on an entity, then open **Window → Tilemap**. The panel edits the
component directly and paints into the shared runtime data. **Import Tiled** reads a `.tmx`
file with CSV layers:

- orthogonal maps use the TopDown projection;
- isometric maps use IsoRect;
- hexagonal maps use IsoHex;
- CSV and uncompressed base64 layers are supported; compressed layers are rejected with a message;
- maps larger than 64×64 must be split or streamed;
- object groups expose name/type/position/size and point objects for spawn markers.

Auto-tiling is available through the tilemap API:

```cpp
Fby::Tilemap::AutoTileRule rule;
rule.Tiles[0] = 1;   // isolated
rule.Tiles[15] = 16; // surrounded
Fby::Tilemap::AutoTileAll(map, rule);
```

### Aseprite animation

Export the sprite sheet and JSON sidecar from Aseprite. In the Sprite Animation inspector set
**Aseprite JSON** to that file and press **Import Aseprite**. The first frame tag becomes the
clip; without tags every frame is imported. Frames must share one grid cell, and the sheet must
be divisible by the cell size. The importer fills the start cell, frame count, duration and the
`SpriteSheetComponent` grid. Non-grid exports stay available to tools but cannot use the
fixed-cell animation path.

### Scripting and debugging

Open **Window → Script Debugger**. It compiles FemboyScript text to bytecode and provides:

- breakpoints by source line;
- step/resume;
- Locals and Self fields;
- watch/evaluate for `rN`, numeric literals and named locals;
- per-breakpoint conditions such as `r3 >= 12.5`;
- call stack;
- graph-node highlight for LogicGraph programs;
- Compile + Nativize for release.

The debugger owns a separate runtime, so a paused session does not stop the game world. Use the
same source and component registry the game uses; watch the Status line for compile errors.

### Build & Pack

Press **Build & Pack Game** in the dashboard or `Ctrl+B`. The release cooker:

1. validates the project;
2. cooks textures/audio/localization;
3. builds the game target;
4. writes archives under `archives/`;
5. reports the produced path in the editor status.

The desktop pipeline is available from the UI; it is not terminal-only. Android/iOS export is
not part of this build yet.

### Profiling a scene

Use **Render Tuning**, **Scene Diagnostics**, **Network Diagnostics** and the LogBoy panels.
They read the same components the game uses; no special debug build is required. For a
performance regression, compare `FrameStats` and the per-system timings in the graph panel
before changing art or physics.

## UI designer

Open **Window → UI Layout**. The panel edits a `WidgetLayoutAsset` directly:

- add Panel/Label/Button/Image;
- select on the preview canvas;
- drag to move, drag the bottom-right handle to resize;
- toggle the design grid and snapping;
- edit anchors, offsets, text, tint, fill/text colours, opacity, focus policy, visibility/enabled,
  clip/read-only flags, UVs, NineSlice insets and ProgressBar/Slider values;
- reorder under a new parent with the parent combo or by dragging in the tree;
- duplicate and delete;
- undo/redo with `Ctrl+Z` / `Ctrl+Y`;
- save to `assets/ui/*.toml` and load it back.

The preview is a layout view, not a live PinkUI render: it draws the resolved widget rectangles
and selection so geometry can be aligned before the game runs.

## Command palette

Press `Ctrl+P` and type to filter:

- Save scene;
- Build & Pack Game;
- Content Browser;
- Console;
- Material Graph;
- VisualScript;
- Script Debugger;
- Tilemap;
- UI Layout;
- Render Tuning.

## Debugging a PIE session

The editor passes its live `ScriptRuntime` to the Script Debugger. In PIE the debugger targets
`pie.World()`; if nothing is selected it uses the first entity with `ScriptComponent`. Pausing the
debugger pauses PIE and the graph; resuming removes only that debugger-created pause. A manual PIE
pause is left untouched.

## Live breakpoints and build target

The Script Debugger binds to the selected entity's live `ScriptComponent` program while PIE is
running: breakpoints, conditions, locals and the call stack now refer to the same bytecode the
game executes. Coroutine programs use the same debug path and preserve their suspend state; AOT
modules still use the normal execution path and are not paused by the VM debugger.

Build & Pack exposes a `desktop` / `mobile` target. `mobile` passes `--target mobile` to
`fby_fempak` and cooks ASTC assets; it is an asset-cook target, not an Android/iOS executable
export or signing step.

## Surface normals

A lit sprite can carry `SurfaceNormal2D`:

```toml
[[entity.components]]
type = "SurfaceNormal2D"
Normal = [0.0, 1.0]
Strength = 1.0
```

The 2D lighting solver dots the light direction with this normal and blends the light by
`Strength`. This is per-sprite surface-normal lighting; texture normal maps use the per-pixel
path below.


## Texture normal maps

Normal maps are ordinary textures with a linear import. Name them with `_n`, `_normal` or
`.normal`; the importer sets `srgb = false` automatically. Keep the normal map in the same texture
array as the sprite and add `NormalMap2D`:

```toml
[[entity.components]]
type = "NormalMap2D"
Layer = 1
Strength = 1.0
```

`Layer` is the texture-array slice; layer 0 disables the path. The sprite shader reconstructs the
normal from the RGB sample, applies `Strength`, and combines it with the scene lights per pixel.
D3D11 uses up to 64 point lights, D3D12 and Metal use four, and Vulkan uses its cluster-light path
with a fixed directional fallback when cluster lighting is off. The Inspector shows the selected
normal layer as a preview.

`SurfaceNormal2D` remains the cheap per-sprite orientation fallback.
