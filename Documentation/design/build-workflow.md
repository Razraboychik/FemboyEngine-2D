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
- `audit-clang` — the existing `build-audit` tree.

Verified on this machine:

```text
cmake --preset dev-clang
cmake --build --preset dev-clang --target fby_frame_composer_test
cmake --build --preset dev-clang --target fby_editor_app

cmake --preset dev-clangcl
cmake --build --preset dev-clangcl --target fby_frame_composer_test
```

Both `fby_frame_composer_test` runs pass.

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
