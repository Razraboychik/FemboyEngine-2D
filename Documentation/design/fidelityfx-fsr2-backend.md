# FidelityFX FSR2 backend

## Build

```cmake
FBY_WITH_FIDELITYFX=ON
FBY_RHI_BACKEND=vulkan
```

The engine uses FidelityFX headers only:

- `api/include`;
- `api/internal`;
- `upscalers/fsr3/include`;
- `upscalers/include`;
- `backend/vk`.

SDK source files are not compiled. The runtime is:

```text
amd_fidelityfx_upscaler_vk.dll
```

`FBY_WITH_FIDELITYFX=ON` copies the signed DLL into `bin/` after the engine
build.

## Context

`Fsr2Adapter` loads the DLL and resolves:

- `ffxCreateContext`;
- `ffxDestroyContext`;
- `ffxConfigure`;
- `ffxQuery`;
- `ffxDispatch`.

Vulkan backend descriptor:

```cpp
ffxCreateBackendVKDesc
{
    vkDevice,
    vkPhysicalDevice,
    vkDeviceProcAddr
}
```

Context description uses actual render/display sizes. Resize destroys and
recreates the context.

## Dispatch

`ffxDispatchDescUpscale` receives:

- color;
- depth;
- motion vectors;
- output;
- jitter;
- motion vector scale;
- render size;
- upscale size;
- sharpening;
- frame time;
- exposure;
- reset;
- camera near/far/FOV.

Resources are built with `ffxApiGetResourceVK`.

## Quality

The adapter contract exposes:

- Quality;
- Balanced;
- Performance;
- Ultra Performance.

Render scale is also controlled by the project `render_scale`.

## Gating and fallback

- DLL missing → vendor FSR2 unavailable;
- motion vectors missing → unavailable;
- UI reports engine FSR2 fallback;
- runtime:

```text
FidelityFX FSR2 -> engine FSR2-compatible -> FSR1 -> bilinear
```

## Limitations

- the current motion target is RGBA8, not the ideal FSR2 RG16 layout;
- quality mode is not yet forwarded to a dedicated FSR2 parameter;
- runtime validation on a Vulkan device with the signed DLL is still required.
