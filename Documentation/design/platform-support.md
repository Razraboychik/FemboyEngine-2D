# Desktop platform support

The editor, sandbox, asset tools and game packer share one CMake project. The
desktop matrix is Windows, Linux and macOS. The renderer is selected at
configure time: Vulkan on Windows and Linux, Metal on macOS, null for headless
servers and tests.

Only the desktop matrix above is supported. The project and source format are
portable between Windows, Linux and macOS; the build for each target is produced
natively on that OS. The project contract is in
[desktop-portability.md](desktop-portability.md).

## Build matrix

| Platform | Toolchain | RHI | Editor | Game cook | CI |
|---|---|---|---|---|---|
| Windows 10/11 x64 | clang-cl 23.1.4 / Clang 23.1.4 / MSVC 19.44 / GCC 16.2.0 | Vulkan | `windows-release` | editor Build & Pack Game | authoritative |
| Linux x64 | clang++ 23.1.4+ / GCC 16.2.0+ | Vulkan | `linux-release` | editor Build & Pack Game | advisory until green |
| macOS 14+ x86_64/arm64 | AppleClang 21+ / Homebrew Clang 23.1.4+ | Metal | `macos-release` | editor Build & Pack Game | focused gate green |

The presets configure `FBY_BUILD_EDITOR=ON`, `FBY_BUILD_TESTS=ON` and
`CMAKE_EXPORT_COMPILE_COMMANDS=ON`. The generated build trees are
`build-windows-release`, `build-linux-release` and `build-macos-release`.

```bash
cmake --preset windows-release && cmake --build --preset windows-release
cmake --preset linux-release   && cmake --build --preset linux-release
cmake --preset macos-release   && cmake --build --preset macos-release
```

The editor is `build-<platform>/bin/fby_editor` (`.exe` on Windows). The game
packer is `build-<platform>/bin/tools/fby_fempak`.

On macOS, configure with Homebrew LLVM rather than the Xcode AppleClang if the
AppleClang standard library does not provide `std::jthread`:

```bash
LLVM_PREFIX="$(brew --prefix llvm)"
cmake --preset macos-release   -DCMAKE_C_COMPILER="${LLVM_PREFIX}/bin/clang"   -DCMAKE_CXX_COMPILER="${LLVM_PREFIX}/bin/clang++"
```

`SetupScripts/macOS/SetupCpp.command` installs that LLVM.

The package wrappers build the same preset and produce a versioned desktop
bundle with the editor, sandbox, tools, licenses and a source offer:

```bat
SetupScripts\Windows\Package.ps1
```

```bash
./SetupScripts/Linux/Package.sh
./SetupScripts/macOS/Package.command
```

They all call `Source/Tools/Dev/PackageDesktop.py`; pass `--skip-build` to
package an existing release tree.

## Platform seams

- SDL3 owns windows, input, gamepads and dialogs on every desktop target.
- The RHI chooses Metal on Apple builds and Vulkan elsewhere; a null backend
  keeps servers and tests headless.
- `ReleaseCooker` runs the packer through `CreateProcess` on Windows and
  `posix_spawn` on Linux/macOS, with the child log, progress polling, stop
  handling and exit code preserved.
- The asset watcher uses `ReadDirectoryChangesW` on Windows and a deterministic
  polling watcher on Linux/macOS. Polling is slower than a native OS stream but
  has no platform-specific dependency and keeps auto-import working.
- `PlayInEditor` launches the sandbox with `CreateProcess` on Windows and
  `posix_spawn` on Linux/macOS.
- The AOT builder emits a DLL on Windows, a `.dylib` with `-dynamiclib` on
  macOS, and a `.so` with `-shared` on Linux.
- The packer search checks the common build directories and both `bin/` and
  `bin/tools/` layouts, so a developer build and a release build both work.

## Cooked game

Build & Pack Game in the editor performs the same sequence on all three systems:

1. cook assets into the project archive with `fby_fempak`;
2. encrypt the archive when the project selects AES or ChaCha;
3. compile the LogicGraph to the AOT module when the project has one;
4. stage the archive and notices under the project `stores/` folders.

The game executable is the project player or sandbox target; the archive and AOT
module are platform-independent. A game built on one desktop system can load the
same cooked archive on another system when the player executable is built there.

## Verification status

Windows is the locally validated gate for this branch and its hosted CI job is
green. The hosted Linux `linux-clang` job builds the editor and passes the test
scope. macOS has the build preset, platform code, a compiled-file review and a CI job.
The hosted macOS job now builds the editor, sandbox and tools, runs pak
reproducibility, the release checklist, the desktop package and the focused
editor/input/text/frame-composer/release-cooker tests.

The macOS gate is a hosted Intel runner with a headless test scope. It proves
the build, cook and core engine seams; it does not prove a physical Apple
Silicon GPU, a visible Metal window, or every GPU-dependent test. The full
CTest suite remains the Windows/Linux gate. Do not describe macOS GPU
behavior as hardware-verified until a physical Mac run is recorded. Do not treat a successful Windows build as proof of Linux or
macOS runtime behavior.

## Vendor usage report

Before considering a slimmer vendor checkout, generate the read-only report:

```bash
python Source/Tools/Dev/ReportVendorUsage.py --build build-dev   --output build/vendor-usage.md --json-output build/vendor-usage.json
```

It classifies `third_party/` as compiled source, transitively included header,
uncompiled source, unreferenced header or resource. It never deletes or edits a
file. A file that is absent from one build can still be the Linux/macOS backend,
a test, a license, a patch or an optional codec for another configuration.

Every dependency update repeats the compiled-file and license review described
in the code style standard and `third_party/patches/README.md`.
