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
- motion vectors (RG16F, normalized UV delta scaled by the render extent);
- output;
- jitter;
- motion vector scale (`renderWidth`, `renderHeight`);
- render size;
- upscale size;
- sharpening;
- frame time;
- exposure;
- reset;
- camera near/far/FOV.

Resources are built with `ffxApiGetResourceVK`. The motion target is created as
`R16G16_SFLOAT` (`RenderTargetFormat::Rg16Float`), so the shader writes signed
motion without a 0..1 bias and FSR2 samples pixel deltas directly.

## Quality

The adapter contract exposes:

- Quality (0.67);
- Balanced (0.58);
- Performance (0.5);
- Ultra Performance (0.33).

`Fsr2QualityForExtent` maps the configured render/upscale extents to the nearest
mode and `Fsr2RenderSizeMatches` validates the pair with a two-pixel tolerance.
The editor forwards that mode to `Dispatch`; the in-game panel stores
`graphics.fsr2_quality` and uses the same scale for the internal render target.

## Validation and gating

- DLL missing → vendor FSR2 unavailable;
- motion vectors missing → unavailable;
- null views, zero extents, render > upscale, extents above `maxRenderSize`, or a
  render/upscale pair that does not match the forwarded quality → `Dispatch`
  returns false;
- `Initialize` rejects zero/oversized extents and render > upscale;
- UI reports engine FSR2 fallback;
- runtime:

```text
FidelityFX FSR2 -> engine FSR2-compatible -> FSR1 -> bilinear
```

## Limitations

- vendor FSR2 still requires a Vulkan device/driver that exposes the signed
  runtime and the FFX Vulkan backend; CI runs the pure validation and fallback
  paths, and the GPU motion pass is exercised by `static_bake_gpu` on capable
  hardware.
