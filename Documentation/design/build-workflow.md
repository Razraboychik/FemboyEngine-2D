# FemboyEngine2D: build and editor

[English](build-workflow.md) | [Русский](build-workflow_RU.md)

## Recommended editor

- VS Code + clangd + CMake Tools;
- CLion is a viable alternative.

Disable VS Code's built-in IntelliSense: the project uses clangd and
`compile_commands.json`.

```json
{
  "C_Cpp.intelliSenseEngine": "disabled",
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build-audit"
  ]
}
```

## Presets

```bat
cmake --preset dev-clang
cmake --build --preset dev-clang
ctest --preset dev-clang
```

Available presets:

- `dev-clang` — Debug, clang++, tests;
- `release-clang` — RelWithDebInfo;
- `dev-clangcl` — clang-cl;
- `audit-clang` — the existing `build-audit` tree;
- `msvc-dev` — Debug, MSVC `cl.exe`, for a Developer Prompt;
- `windows-release` — clang-cl, Vulkan, editor and tests;
- `linux-release` — clang++, Vulkan, editor and tests;
- `macos-release` — clang++, Metal, editor and tests.

Validated commands:

```text
cmake --preset dev-clang
cmake --build --preset dev-clang --target fby_frame_composer_test
cmake --build --preset dev-clang --target fby_editor_app

cmake --preset dev-clangcl
cmake --build --preset dev-clangcl --target fby_frame_composer_test
```

Both frame-composer runs pass.

New Project offers **Empty / 2D Platformer / Top-Down / UI App**: a valid
`scenes/main.toml` with reflected components only, a README and a generated
`CMakeLists.txt` on the CMake 3.22 floor. Existing files are never overwritten.
Distribution: `fby_fempak --exclude`, `PackageDesktop.py --game --features ...
--assets ...` and `--max-size-mb`.

Visual Script compiles a graph to bytecode, nativizes it to a native library
and writes a `<module>.fbyaot` manifest next to it. **Plugin Manager** connects
that library to the project (paths are restricted to the project root), loads it,
and **Reload/Unload** works without restarting the editor. **Compatibility**
shows the host ABI and component count, validates the plugin's component layout
hashes on load, and **Write plugin report** saves
`Saved/Plugins/plugin-report.md` with version, author, build info, components and
systems.

**Accessibility QA** audits the published semantic tree and the project
profile: node count, unnamed interactive roles, unknown roles, degenerate bounds
and a stale focus id, plus Blind mode without narration, Deaf mode without
subtitles, a disabled flash guard and an empty narrator language. **Write QA
report** saves a Markdown report under `Saved/Accessibility/qa-<stamp>.md`.

**Network Diagnostics** hosts the lobby client: enter the lobby host/port and
its 32-byte Ed25519 public key (64 hex), then create a room, join by id or start
the match. The panel shows ready/room/player/started/error and the signed ticket;
the game calls `NetLobbyClient::ApplyToSessionConfig()` before creating the
`NetSession`, and the session view below shows the match's rollback diagnostics.
Lobby traffic uses the same UDP transport as the game.

**SDF Lighting** creates and edits the scene's single `SdfSunComponent`:
direction, softness, ambient, occlusion radius/strength and the step budget. It
adds box or circle `Light2DOccluderComponent` entities at the camera centre and
edits their transform, shape and enabled flag. The renderer collects at most
`Rhi::kMaxSdfGpuOccluders` dynamic casters per frame; the panel shows the budget
and warns when it is exceeded. Components serialize with the scene, so authored
lighting round-trips through the normal scene save.

The **Profiler** (F7) draws the frame-time plot and the per-category rows;
**Write trace** saves `Profiler/trace.json`. **Crash Diagnostics** shows whether
the platform handler is armed, the current report session and the recent
sessions under `Saved/Crashes/`. **Write report now** writes the same text report
and minidump as an unhandled crash, with the project name and root included, so a
tester can attach it without reproducing the crash.

A selected prefab instance shows **Prefab overrides** in the Inspector: one
position/scale/rotation/colour switch per prefab entity. Only enabled fields are
stored in `PrefabInstanceComponent` masks, and the children are rebuilt from the
prefab instead of copied, so editing an instance never edits the prefab asset.
**Clear all overrides** drops every mask.

**Build & Pack Game** cooks and packs the project, then (with the "Stage runnable
game" toggle) copies the sandbox/server, runtime DLLs and the cooked pak into
`<release>/<project>-game/`. That folder is the one-click export: it runs without
the editor or the build tree. Vendor upscaler runtimes stay out by default; the
packaging CLI selects them per feature.

The editor's Lite/Balanced/High performance profiles cap the undo arena, the
thumbnail cache and the frame rate; Custom keeps the individual values. The
frame limit and the unfocused throttle are the two switches that trade latency
for CPU and GPU time without changing the project.

The desktop matrix uses `windows-release`, `linux-release` and `macos-release`.
Each preset builds the editor, sandbox, tools and the game packer. The editor
produces `bin/fby_editor`; Build & Pack Game invokes `bin/tools/fby_fempak` on
all three systems. See
[platform support](platform-support.md) for the verification status.

## Choosing a compiler

The 0.6.0 line supports Clang, AppleClang, GCC and MSVC. clang-cl is the
reference Windows toolchain and the release gate; CI also runs clang, GCC,
AppleClang and MSVC. Every compiler is a supported alternative, never an
obligation. The minimum versions are the oldest supported generations, not an
upper bound; the hosted CI pins LLVM 23.1.4 on Windows, LLVM 23.1.x on Linux,
GCC 16.2 and the current Visual Studio 2022 MSVC.

| Compiler | Platform | Minimum | Best for |
|---|---|---|---|
| clang-cl | Windows | 19.1 | primary Windows build, PDB and Visual Studio debugging |
| MSVC (`cl.exe`) | Windows | 19.40 | supported alternative with its own Windows CI job |
| clang++ | Linux/macOS/Windows | 19.1 | cross-platform parity, primary Unix build |
| GCC | Linux/MinGW | 14.1 | second-opinion compiler and portability checks |
| AppleClang | macOS | 16.0 (Xcode 16) | native macOS/Metal build |

- **Windows:** clang-cl is the reference build. It uses the Windows SDK and the
  MSVC standard library, and works with the Visual Studio debugger and PDBs. MSVC
  (`cl.exe`) is the supported Visual Studio-native alternative.
- **Linux:** clang++ is the reference build; GCC 14.1 is the second opinion.
- **macOS:** AppleClang 16+ with the Metal backend.
- **One compiler everywhere:** Clang. It is the only frontend that covers
  Windows (clang-cl or clang++), Linux and macOS.

### Availability target

The engine targets **C++23**, the latest ratified standard. The supported floors
are Clang 19.1, GCC 14.1, MSVC 19.40 and AppleClang 16. A direct `cmake -S . -B`
configure needs CMake 3.22; the checked-in presets use schema v6 and therefore
need CMake 3.25.

The floors are set by the required C++23 library surface (`std::mdspan`,
`std::format`, `std::jthread`, `std::expected`), not by the language mode alone.
Older compilers such as GCC 13 or Clang 17 may lack `std::mdspan`, so they are
not targets. Ubuntu 22.04 therefore needs a newer compiler from its repositories
or the LLVM apt repository; Ubuntu 24.04 ships GCC 13 and Clang 18 and normally
needs `g++-14` or `clang-19` for the same reason.

### C++ standard modes

`-DFBY_CXX_STANDARD=23` is the default and the public contract.
`-DFBY_CXX_STANDARD=26` is the experimental mode: the same sources with C++26
flags and whichever C++26 library facilities the feature probes can prove. No
public API uses C++26 syntax, so both modes expose the same engine and the C++23
build is not a reduced port.

One acceleration is wired today: `Fby::Simd` for the audio bus peak scan.
`Fby::FunctionRef` is the matching non-allocating callback view, the C++23
equivalent of `std::function_ref`. The build probes `std::simd`, but neither
libc++ 19 nor libstdc++ 16 ships `<simd>` yet, so the C++23 build uses an
SSE2/NEON reduction with a scalar tail.
Measured on a 1 MiB float buffer at `-O2`/`-O3` on x86-64:

| Kernel | Scalar loop | `Fby::Simd` | Result |
|---|---|---|---|
| peak scan | 26.8 ms | 6.8 ms | **3.8–4.0x** |
| add | 4.1 ms | same scalar loop | no win; the auto-vectoriser already handles it |
| scale | 2.5 ms | same scalar loop | no win; the auto-vectoriser already handles it |

Engine-wide this is below 1% of frame time: it shortens the audio bus peak scan
and gives the audio thread headroom. It is not an FPS win, and the build does not
claim one. New C++26 library accelerations go through the same probes and
`Fby::` wrappers; core-language C++26 features are deliberately out of scope.

MSVC configuration. The Microsoft toolchain has its own CI job but is
never the reference gate. From a Developer Prompt, `cmake --preset msvc-dev`
uses the same configuration:

```bat
cmake -S . -B build-msvc -G Ninja ^
      -DCMAKE_C_COMPILER=cl ^
      -DCMAKE_CXX_COMPILER=cl ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DFBY_BUILD_TESTS=ON ^
      -DFBY_WITH_RNNOISE=OFF
cmake --build build-msvc --parallel 8
ctest --test-dir build-msvc --output-on-failure
```

RNNoise stays disabled on MSVC because its vendored C sources use variable-length
arrays, which the Microsoft C frontend does not implement. The spectral denoise
tier remains the fallback; Clang, GCC and clang-cl keep the neural tier.


The build requires Vulkan 1.4 on Windows and Linux; macOS uses Metal. See
[system requirements](system-requirements.md) for the editor hardware targets.

Quick optimization gate:

```bat
ctest --preset optimization
```

Never mix toolchains in one build directory. clang++ and clang-cl use separate
directories.

## Windows: GUI subsystem

`FBY_CONSOLE_APPS=ON` keeps a console on applications. With clang-cl the project
builds a GUI-subsystem executable using `/ENTRY:mainCRTStartup`. For GNU-like
clang++/GCC, `WIN32_EXECUTABLE` is not enabled: those drivers expect `WinMain`,
while the engine uses a normal `int main()`. Therefore on Windows `dev-clang`
builds console applications and `dev-clangcl` builds GUI applications.

## Manual build

```bat
cmake -S . -B build -G Ninja ^
      -DCMAKE_C_COMPILER=clang-cl ^
      -DCMAKE_CXX_COMPILER=clang-cl ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DFBY_BUILD_TESTS=ON
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

GNU-like clang++:

```bat
cmake -S . -B build-clang -G Ninja ^
      -DCMAKE_C_COMPILER=clang ^
      -DCMAKE_CXX_COMPILER=clang++ ^
      -DCMAKE_BUILD_TYPE=Debug ^
      -DFBY_BUILD_TESTS=ON ^
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-clang --parallel 8
```

## clangd

`.clangd` points at `build-audit`. If another directory is used, change
`CompilationDatabase` or pass `--compile-commands-dir`.

Windows system headers may require:

```text
--query-driver=C:/Users/<user>/AppData/Local/Programs/LLVM/bin/clang++.exe
```

## Debugging

`launch.json` contains configurations for:

- `fby_editor`;
- `fby_frame_composer_test`.

Use RenderDoc for GPU debugging; do not embed a debugger in the editor.

## Engine editor integration

Do not write a custom text editor. Integrate through:

- `compile_commands.json`;
- CMake presets;
- LSP;
- DAP;
- `project.femproj`.

That is enough for completion, navigation and debugging without building an IDE
clone.
