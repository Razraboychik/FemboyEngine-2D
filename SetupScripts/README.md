# Setup scripts

Dependency setup is split by host OS. Pick the directory for the machine that
will build the engine; there is no cross-OS launcher.

| OS | Directory | One-click entry point |
|---|---|---|
| Windows 10/11 | `Windows/` | `Windows/Setup.bat` or `SetupWindows.ps1` |
| Linux | `Linux/` | `Linux/Setup.sh` |
| macOS | `macOS/` | `macOS/Setup.command` |

## Compilers installed or checked

| Host | Supported frontends |
|---|---|
| Windows | clang-cl, clang++, MSVC `cl`, MinGW-w64 `g++` |
| Linux | clang++ and GCC `g++` |
| macOS | AppleClang `clang++`, Homebrew LLVM `clang++`, GCC `g++` |

Windows deliberately installs both compiler families because clang-cl is the
primary gate and MSVC/GCC are the portability gates. Linux and macOS do not
reference MSVC, clang-cl or any Windows-only toolchain.

## Component entry points

Windows and Linux keep the component wrappers next to the all-in-one script:

```text
Windows/SetupCpp.bat       Linux/SetupCpp.sh
Windows/SetupArchiver.bat  Linux/SetupArchiver.sh
```

macOS has the same filenames with the `.command` extension; the wrappers run the
all-in-one macOS setup. Vendor SDK download is no longer a separate script: use
`-WithVendorSdks -AcceptVendorLicenses` on Windows or
`--with-vendor-sdks --accept-vendor-licenses` on Linux/macOS.

## What each script does

- C++ setup installs the pinned toolchain (or checks it), downloads the pinned
  third-party sources into `third_party/`, and writes
  `third_party/dependencies.lock`. On Windows it installs/checks clang-cl,
  clang++, MSVC Build Tools and MinGW-w64 GCC; on Linux it installs/checks
  clang++ and GCC; on macOS it installs/checks AppleClang, Homebrew LLVM and GCC.
- The archiver setup installs 7-Zip/p7zip for `.fempak` compression.
- Vendor SDK setup is part of the all-in-one Setup: it verifies local DLSS,
  FidelityFX and XeSS SDKs and can download them from GitHub only with an
  explicit licence acceptance flag. It never redistributes a vendor SDK. DLSS
  goes to `third_party/dlss`.
- `SetupWindows.ps1` is the implementation behind the Windows `.bat` wrappers.
  It supports `-Component Cpp|Archiver|Upscalers|All`, `-SkipSystem`, `-Force`,
  `-WithVendorSdks` and `-AcceptVendorLicenses`.
- Linux and macOS accept `--with-vendor-sdks` and `--accept-vendor-licenses`.
- `RegisterFemboyProjects.ps1` is Windows-only and registers the `.femproj`
  association with the built editor.

## Repository root

Launchers resolve the repository root from their own directory, so they work
from Explorer, Finder, a terminal and CI without an absolute path. Do not copy a
single script out of its OS directory: the wrappers rely on their sibling
implementation and on the two-level root.

## Desktop package

The package wrappers build the release preset when needed, then write a
versioned bundle with the editor, sandbox, asset tools, licenses and a source
offer. They all call the same Python implementation:

```bat
SetupScripts\Windows\Package.ps1
```

```bash
./SetupScripts/Linux/Package.sh
./SetupScripts/macOS/Package.command
```

Pass `--skip-build` to package the existing `build-<platform>-release` tree.

## Vendor release

Vendor SDKs remain optional and are never committed or redistributed by the
setup scripts. After accepting the licences:

```text
Windows: SetupScripts\Windows\Setup.bat -WithVendorSdks -AcceptVendorLicenses
Linux:   SetupScripts/Linux/Setup.sh --with-vendor-sdks --accept-vendor-licenses
macOS:   SetupScripts/macOS/Setup.command --with-vendor-sdks --accept-vendor-licenses
```

Then configure the separate vendor preset (not the normal release preset):

```text
cmake --preset release-vendor-clang
cmake --build --preset release-vendor-clang
ctest --preset release-vendor-clang
```

CMake copies the signed FidelityFX runtime DLLs into `bin/` and `bin/tests/`.
`PackageDesktop.py` automatically adds any `amd_fidelityfx_*`, `nvngx_dlss*`
and `libxess*` runtime found in the build `bin/` to the desktop bundle.

The D3D12 validation layer is an optional Windows "Graphics Tools" feature and
is not required by the normal build or by the vendor preset.

SPIRV-Cross is downloaded as an **offline material-cook dependency** only. It is
linked into `fby_matc` (`--dx12` writes HLSL), never into the engine or a game
runtime. If the checkout is absent, `fby_matc` still builds the Vulkan path and
reports that DX HLSL cooking is disabled.
