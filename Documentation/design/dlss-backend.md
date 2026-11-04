# DLSS NGX backend

## Build

```cmake
FBY_WITH_DLSS=ON
FBY_RHI_BACKEND=vulkan
```

The engine includes `third_party/dlss/include` and defines `FBY_WITH_DLSS=1`.
No MSVC static NGX library is linked. The runtime is loaded dynamically so a
clang-cl build does not hit `/failifmismatch`.

## Dynamic loading

The adapter searches next to the executable:

```text
nvngx.dll
_nvngx.dll
```

It resolves:

- `NVSDK_NGX_VULKAN_Init`;
- `NVSDK_NGX_VULKAN_Shutdown1`;
- `NVSDK_NGX_VULKAN_GetCapabilityParameters`;
- `NVSDK_NGX_Parameter_GetI`;
- `NVSDK_NGX_Parameter_SetVoidPointer`;
- `NVSDK_NGX_Parameter_SetUI`;
- `NVSDK_NGX_Parameter_SetF`;
- `NVSDK_NGX_VULKAN_CreateFeature1`;
- `NVSDK_NGX_VULKAN_EvaluateFeature`;
- `NVSDK_NGX_VULKAN_ReleaseFeature`.

If any symbol is missing, the backend is unavailable and the renderer uses the
fallback chain.

## Capability check

The backend requires:

- Windows;
- NVIDIA GPU;
- Vulkan RHI;
- `nvngx.dll`/`_nvngx.dll` loadable;
- `SuperSampling.Available`;
- motion vectors;
- HUD separated.

`CapabilitiesFromDevice` and `DlssRuntimeProbe` provide the pre-check. The UI
shows a fallback message when the backend is unavailable.

## Feature

The adapter sets:

- Color;
- Depth;
- MotionVectors;
- Output;
- Width/Height;
- SuperSampling.ScaleFactor;
- Jitter.Offset.X/Y.

Quality modes:

- Ultra Performance;
- Performance;
- Balanced;
- Quality.

`SuperSampling.FeatureInitResult` is checked after creation. A failed feature is
released and reported as unavailable.

## Limitations

- the engine has no `nvngx.dll` in the repository;
- runtime validation needs an NVIDIA RTX machine with the driver-provided NGX
  runtime;
- output format and image layout are not validated on hardware yet.

## Fallback

```text
DLSS -> FSR2 -> FSR1 -> bilinear
```

DLSS is never presented as working when the runtime or GPU does not support it.
