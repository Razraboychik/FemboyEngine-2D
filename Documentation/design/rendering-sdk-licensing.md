# Rendering SDK licensing and build isolation

FemboyEngine2D is licensed under MPL 2.0. Vendor rendering SDKs are not part of
that license and must not be relicensed as MPL.

## Layout

```text
Source/Engine/RHI/...          MPL 2.0 engine and adapters
Source/Engine/RHI/Dlss/...     MPL 2.0 adapter, optional
third_party/dlss/              NVIDIA SDK + NVIDIA license
third_party/fidelityfx/        AMD SDK + AMD license
third_party/xess/              Intel SDK + Intel license
```

Vendor headers must not appear in public MPL headers. Adapters include them only
inside optional translation units.

## Build flags

```cmake
FBY_WITH_DLSS=OFF
FBY_WITH_FSR2=OFF
FBY_WITH_METALFX=OFF
```

When a flag is off:

- no vendor headers are included;
- no vendor libraries are linked;
- capability is false;
- the UI does not offer the backend.

## Distribution

- vendor binaries ship inside the application, not as a standalone SDK;
- `LICENSE.txt` and notices are retained;
- MPL files keep MPL headers;
- vendor files keep vendor licenses;
- source archives exclude vendor binaries when the vendor license requires it;
- no reverse engineering or relicensing.

## Runtime gating

Every backend is checked before use:

- platform;
- GPU vendor;
- runtime/DLL presence;
- driver version;
- motion vectors;
- HUD separation;
- frame-generation policy.

Unsupported backends are never called and never shown as working options.

## MetalFX

MetalFX is an Apple system framework. It is not vendored and is not distributed
with the engine. The Metal adapter links it only on Apple platforms; the Apple
SDK license applies.

## DLSS

DLSS SDK is in `third_party/dlss`. The NGX DLLs are optional deployment payload.
`DetectDlssRuntime` checks their presence before the adapter is considered.

## FSR

FidelityFX source is optional. FSR1/FSR2 adapters keep their own notices and do
not relicense the engine.

## XeSS

XeSS upscaling and XeSS Frame Generation were removed from the supported
product feature set.

## Verification

- notices gate;
- no vendor header in MPL public headers;
- backend off by default;
- unsupported OS/vendor test;
- fallback chain test;
- binary distribution manifest lists vendor notices.
