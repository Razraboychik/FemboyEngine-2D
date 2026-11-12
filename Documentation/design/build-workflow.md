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
- `windows-release` — clang-cl, Vulkan, editor and tests;
- `linux-release` — clang++, Vulkan, editor and tests;
- `macos-release` — clang++, Metal, editor and tests.

Verified on this machine:

```text
cmake --preset dev-clang
cmake --build --preset dev-clang --target fby_frame_composer_test
cmake --build --preset dev-clang --target fby_editor_app

cmake --preset dev-clangcl
cmake --build --preset dev-clangcl --target fby_frame_composer_test
```

Both `fby_frame_composer_test` runs pass.

The desktop matrix uses `windows-release`, `linux-release` and `macos-release`.
Each preset builds the editor, sandbox, tools and the game packer. The editor
produces `bin/fby_editor`; Build & Pack Game invokes `bin/tools/fby_fempak` on
all three systems. See
[platform support](platform-support.md) for the verification status.

## Choosing a compiler

The 0.5.0 line supports Clang, AppleClang, GCC and MSVC. clang-cl is the
reference toolchain and the release gate; CI also runs clang, GCC and AppleClang.
MSVC builds the same tree for local development but is not part of the CI gate.
Every compiler is a supported alternative, never an obligation. The minimum
versions are the releases validated for this line, not older compatibility
targets.

| Compiler | Platform | Minimum | Best for |
|---|---|---|---|
| clang-cl | Windows | 23.1.4 | primary Windows build, PDB and Visual Studio debugging |
| MSVC (`cl.exe`) | Windows | 19.44 | optional alternative for teams standardized on the Microsoft toolchain |
| clang++ | Linux/macOS/Windows | 23.1.4 | cross-platform parity, primary Unix build |
| GCC | Linux/MinGW | 16.2.0 | second-opinion compiler and portability checks |
| AppleClang | macOS | 21.0 (Xcode 26.4) | native macOS/Metal build |

- **Windows:** clang-cl is the reference build. It uses the Windows SDK and the
  MSVC standard library, and works with the Visual Studio debugger and PDBs. MSVC
  (`cl.exe`) is the optional alternative for a Visual Studio-native toolchain.
- **Linux:** clang++ is the reference build; GCC 16.2.0 is the second opinion.
- **macOS:** AppleClang 21+ with the Metal backend.
- **One compiler everywhere:** Clang. It is the only frontend that covers
  Windows (clang-cl or clang++), Linux and macOS.

Optional MSVC configure. The Microsoft toolchain stays available, but it is
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
