# Universal GPU backends

The renderer is moving from a single "Vulkan 1.4 or nothing" gate to capability
tiers and, later, Direct3D backends. OpenGL is intentionally out of scope.

## Vulkan tiers

| Tier | Minimum | Enabled paths |
| --- | --- | --- |
| `Legacy` | — | rejected |
| `Baseline` | Vulkan 1.2 | core renderer, FSR1, motion vectors, VT |
| `Enhanced` | Vulkan 1.3 | GPU culling, clustered lighting, synchronization2 |
| `Temporal` | Vulkan 1.4 | temporal resolve, FSR2, FSR3/custom frame generation |

The device reports its tier in `DeviceInfo::tier`; temporal resources are only
created on the `Temporal` tier. A Vulkan 1.3 iGPU (for example Radeon 890M)
therefore runs the baseline/enhanced renderer instead of being rejected, while
RTX 4060 on 1.4 keeps FSR2/FSR3/FG.

## NGX/DLSS extension query

`GpuExtensionBroker` deduplicates requirements from SDL/presentation, NGX,
validation and future subsystems, then filters them against the driver lists.
Unsupported names are logged by subsystem instead of failing the whole device.

`DlssExtensions` loads the DLSS runtime DLL before device creation and calls
`NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements` and
`...GetFeatureDeviceExtensionRequirements` for DLSS SuperSampling. The Vulkan
instance and device include the returned extensions when the driver supports
them. On the current RTX 4060 the query returns no additional extensions, and
`NVSDK_NGX_VULKAN_Init` still returns `PlatformError`; that is an NGX runtime
issue, not an omitted extension in this path.

## Direct3D plan

1. **D3D12 device/resource layer**: DXGI adapter selection, swapchain, command
   queues/lists, descriptor heaps, buffers, textures and render targets.
2. **D3D12 FrameComposer parity**: sprite batches, depth, motion vectors,
   temporal resolve and the existing frame-generation interface.
3. **D3D11 backend**: same `GpuCapabilities` contract, feature level 11_0/11_1.
4. **Capability matrix**: Vulkan 1.2/1.3/1.4, D3D11, D3D12, Null; tests for
   adapter selection, fallback chains and extension filtering.

`Backend::D3D12` and `Backend::D3D11` create a real RHI device and swapchain:
`fby_d3d12_rhi_test` / `fby_d3d11_rhi_test` exercise `RHI::Create`, frames,
present, resize/VSync, offscreen render+capture, sRGB capture, depth copies and
the temporal/upscale paths. Both backends consume the engine `FramePacket`
directly: sorted sprite batches with per-batch atlas, material pipelines, inline
and cached meshes, motion vectors, temporal resolve with real depth, post-FX
composite, bloom, LUT colour grading, HUD overlay, spatial upscale and RCAS
sharpen, present blit and the accessibility composite all run on D3D11 and
D3D12. The D3D12 path records the post-FX and colour-grading passes through
`RecordGraphicsPass` and reports the engine-side upscaler capabilities in
`DeviceInfo`. Both backends select the non-software DXGI adapter with the
largest dedicated VRAM and fall back to software for headless/CI runs. Shader
targets follow the feature level: `vs_5_0/ps_5_0` on 11_0+, `vs_4_0/ps_4_0` on
10_0/10_1. The DX smoke tests build a packet from a real
`Transform2D`/`Sprite2D`/`DrawOrder` entity, so the engine packet path is
exercised rather than a hand-built quad.

## DXGI probe (first stage)

`fby_dx_probe_test` enumerates DXGI adapters and creates temporary D3D11/D3D12
devices to record feature levels. Current result on the test laptop:

| Adapter | D3D12 | D3D11 | Dedicated VRAM |
| --- | --- | --- | --- |
| RTX 4060 Laptop | yes | yes | ~8.3 GiB |
| Radeon 890M | yes | yes | shared |
| Microsoft Basic Render Driver | yes | yes | 0 |

The probe is deliberately separate from `RHI::Create`: it proves adapter/device
availability without pretending the render path exists.

`fby_d3d12_smoke_test` (RTX 4060) creates a device, swapchain, RTV heap, command
list, fence, root signature, HLSL vertex/pixel shaders, PSO and a vertex buffer,
then clears, draws a triangle and presents three frames:
`clear=1 triangle=1 present=1`.
`fby_d3d11_smoke_test` creates shaders, input layout, PSO state and a vertex
buffer, draws a triangle, clears/presents three frames at feature level 11_0
(`clear=1 triangle=1 present=1 feature_level=b000`). These are real device/swapchain paths, but the engine
still has no D3D `RHI` implementation, so FrameComposer does not render through
DX yet.

## Backend selection

`BackendFromName` accepts `vulkan`, `d3d12`, `d3d11`, `metal` and `null`.
`FBY_RHI_BACKEND=d3d12` (or `d3d11`) overrides the `DeviceDesc` backend for
manual diagnostics; an explicit unsupported backend fails instead of falling
back to Null. The D3D11/D3D12 RHI smoke tests use `RHI::Create` directly, so the
same selection path is exercised.

## Status matrix

| Area | Vulkan | D3D12 | D3D11 |
| --- | --- | --- | --- |
| Device/swapchain/present + resize | yes (1.2/1.3/1.4 tiers) | yes | yes |
| Textured sprite FramePacket (camera, rotation, flips) | yes | yes | yes |
| Sorted sprite batches with per-batch atlas | yes | yes | yes |
| Depth pass | yes (prepass) | depth-tested draws only | depth-tested draws only |
| Texture arrays, BC7, mip chains | yes (BC7 + ASTC) | yes (BC7) | yes (BC7) |
| Built-in atlas for 0xFFFFFFFF packets | yes | yes | yes |
| Render targets/motion target | yes | yes | yes |
| FrameComposer smoke | yes | yes | yes |
| Motion-vector pass (normalized UV delta) | yes | yes | yes |
| Temporal resolve (motion + depth reject) | yes (1.4) | yes | yes |
| Spatial upscale / sharpen | yes (all four) | yes (nearest/bilinear/bicubic; FSR1 maps to bicubic) | yes (same) |
| Capture / copy / PresentRenderTarget | yes | yes | yes |
| HUD / OIT blend modes | yes | yes | yes |
| FSR2-compatible chain (temporal+upscale+sharpen) | yes (1.4) | yes | yes |
| Custom frame generation (temporal+history) | yes | yes | yes |
| Vendor FSR3 frame generation | yes (1.4) | yes (DX12) | custom FG only (no vendor DX11 FG) |
| MeshRenderItem path (inline + cached mesh) | yes | yes | yes |
| Post-FX composite (CRT/vignette/chromatic/colour vision) | yes | yes | yes |
| Bloom chain (prefilter + separable blur) | yes | yes | yes |
| LUT colour grading (`color_grading.frag`) | yes | yes | yes |
| Accessibility composite (`FramePacket.accessibility`) | yes | yes | yes |
| OIT resolve (weighted blended accumulation/revealage) | yes | no | no |
| Clustered lighting / SDF shadows / virtual texturing | yes | no | no |
| Full render graph parity | partial | partial | partial |

DX is a functional sprite/mesh/depth/motion/post-FX/present backend, not yet full
FrameComposer parity: the accessibility composite, weighted OIT resolve,
clustered lighting, SDF shadows and virtual texturing remain Vulkan-only. A DX
project that enables them keeps the CPU fallback instead of crashing; the matrix
above lists them explicitly rather than implying parity. `ClearMotionVectors` and `UploadMotionVectors` are implemented on both DX
backends. Both D3D11 and D3D12 draw motion into the RG16F target with the motion
VS/PS pair and a `MotionVertex` stream, using the same normalized UV delta the
Vulkan `motion.vert` writes. Every recorded pass owns a range of the per-frame
upload ring, and the sprite transform (camera, zoom, rotation, UV flips) is
applied on the CPU exactly like `quad.vert`, so a world-space FrameComposer
packet lands where the Vulkan backend puts it.
D3D11 and D3D12 `CaptureRenderTarget` read an RGBA8/BGRA8 target back to host
memory (tested with an 8x8 target), and both `CopyColorImage` paths copy matching
targets. The earlier D3D12 `DEVICE_REMOVED` was traced with the debug layer to an
invalid SRV: `Texture2DArray.MipLevels` was left at 0. Setting `MipLevels=1`,
`MostDetailedMip=0`, `FirstArraySlice=0` fixed the sprite, motion, capture and
copy paths.
The Vulkan tier matrix is covered by unit tests for 1.2/1.3/1.4 classification and
the backend-name parser.

### D3D12 debugging

`FBY_D3D11_DEBUG=1` enables the D3D11 debug layer (whose stored messages are
drained into the log), and `FBY_D3D12_DEBUG=1` enables the D3D12 debug layer and an `ID3D12InfoQueue`
dump on device removal. It found the invalid SRV `MipLevels=0` that caused the
old motion/capture failures.

### Vulkan 1.2 legacy path

`FBY_VULKAN_API=1.2` caps the selected device API version to simulate an older
1.2 driver. `vulkan_rhi` and `fby_vulkan_rhi_legacy_1_2` both render a
FrameComposer packet; the legacy test reports `api=1.2.0 tier=baseline` on the
RTX 4060 and passes. Vulkan 1.1 is intentionally unsupported.

### Runtime device matrix

`fby_gpu_matrix_test` enumerates DXGI adapters and their D3D11/D3D12 feature
levels. On the reference machine:

```
NVIDIA GeForce RTX 4060 Laptop GPU  vram=8342470656 d3d11=1(b100) d3d12=1(c200)
AMD Radeon(TM) 890M Graphics       vram=303353856  d3d11=1(b100) d3d12=1(c200)
Microsoft Basic Render Driver      vram=0          d3d11=1(b100) d3d12=1(c100)
```

The Vulkan side is covered by `vulkan_rhi` and `fby_vulkan_rhi_legacy_1_2`.

### HUD path

Both D3D11 and D3D12 have `SetHudTarget`, `RenderHudTarget`, `CompositeHud`,
`SetUiVisible` and `SetHudUpscale`. The composite draws the premultiplied HUD
target as a straight-alpha blended fullscreen overlay (never a `CopyResource`
that would replace the scene), un-premultiplies in the pixel shader like
`hud_composite.frag`, honours the nearest/linear `SetHudUpscale` choice, and
scales a HUD target whose size differs from the swapchain. Both smoke tests use
a 160x120 HUD target composited into a 320x240 swapchain.

### Sharpen

Both D3D11 and D3D12 have an `RcasSharpen` fullscreen pass (4-neighbour unsharp
mask); both smoke tests sharpen an 8x8 source into an 8x8 destination, D3D11 also
captures the result.

### OIT blend modes

Both D3D11 and D3D12 record `RenderTargetDesc::blend` and select
opaque/additive/reveal blend states in `RenderToTarget`; both smoke tests render
an additive 8x8 target.

### FSR2-compatible chain on DX

`Fsr2Pass` is engine-side: temporal resolve + spatial upscale + RCAS sharpen. It
does not need the vendor Vulkan DLL. `fsr2_dx` (D3D12) and `fsr2_dx_d3d11`
both run `Ensure` -> `Resolve` -> capture on an 8x8 source/16x16 output and pass.
Vendor FSR2/FSR3 frame-generation contexts remain Vulkan-only.

### Custom frame generation on DX

`CustomFrameGenerator` runs on both DX backends: `custom_fg_dx` (D3D12) and
`custom_fg_d3d11` initialize, generate from color/motion/output targets and pass.
Vendor FSR3 frame generation remains pending.

### FrameComposer batch smoke

`frame_composer_dx` builds a 64-sprite, 4-layer FrameComposer packet from a real
ECS registry (plus a cached mesh item) and submits three frames on D3D12; it then
renders the same packet into a 320x240 offscreen target with per-target depth and
captures it. `fby_frame_composer_dx_d3d11` runs the same test on D3D11. Both
report `items=64 frames=3`.

### Mesh path on DX

`CreateMesh`/`DestroyMesh` cache vertex/index data; `Submit` expands
`MeshRenderItem` triangles through the sprite pipeline. The item model matrix and
the camera projection are applied on the CPU; when `boneCount > 0`, two-bone LBS
weights from `packet.boneMatrices` are applied first (CPU skinning). The
FrameComposer batch smoke submits a cached mesh handle on both D3D12 and D3D11.
`CopyDepth` currently falls back to `CopyColorImage` for the engine's
color-backed history targets. Both DX backends now create a per-render-target
D32 depth texture/DSV; `RenderToTarget` binds and clears it and
`RenderTargetDepthTexture` exposes it. The sprite/mesh PSOs keep depth test and
depth writes disabled because 2D batches are sorted on the CPU and all quads
share one z.

### Vendor FSR3 on DX12

With `FBY_WITH_FIDELITYFX=ON` the FSR3 frame generator uses the FidelityFX DX12
backend (`amd_fidelityfx_framegeneration_dx12.dll`). `fsr3_d3d12` initializes at
320x180 -> 640x360 and calls Generate successfully:

```
fsr3_d3d12: init=1
fsr3_d3d12: generate=1
```

The earlier `exit=3` abort was reproduced only with 8x8/16x16 extents; the vendor
context does not support that resolution. D3D11 vendor FSR3 is still pending.

### Known limitations

- AMD FidelityFX frame generation ships DX12/Vulkan providers only; D3D11 uses
  the portable `CustomFrameGenerator` instead of vendor FSR3.
- MaterialGraph runtime pipelines are SPIR-V based; the DX backends only build a
  material PSO when a cooked DXIL/DXBC fragment stage is present and keep the
  engine sprite VS (the DX mesh path CPU-expands geometry into the sprite vertex
  contract). The material vertex blob is selected to match the fragment shader
  model, because D3D12 rejects mixing SM 6.0 with SM 5.x in one PSO.
- DX mesh skinning is CPU two-bone LBS; there is no GPU skinning/vertex-shader
  palette path yet.
- The DX post-FX composite and bloom chain are HLSL ports of the Vulkan shaders
  (`d3d11|d3d12/postfx_composite.hlsl`, `bloom_prefilter.hlsl`, `bloom_blur.hlsl`):
  the scene renders into an offscreen target, the prefilter reduces it to a
  half-resolution RGBA16F pair, two blur dispatches ping-pong them, and one
  fullscreen draw applies CRT/vignette/chromatic/colour-vision/accessibility
  effects. D3D12 transitions the scene through compute and pixel shader states
  around the chain. Both the packed `FramePacket::postFx` settings and the
  render-graph "postfx_composite" constant block are accepted on Vulkan, D3D12
  and D3D11.
- `ColorGradingPass` is wired on every backend: `lutTexture` is a
  `TextureArrayHandle` id whose strip is sampled from layer 0, and the pass copies
  the final swapchain image to a scratch target before re-rendering it through the
  LUT (Vulkan), or copies the back buffer and redraws it (D3D11/D3D12).
- User-authored material parameters are still baked into the generated shader;
  the RHI carries the engine camera/material-clock block only. A per-material
  instance constant block (values from a MaterialAsset instance) is the remaining
  design task.
- The Metal path covers device/swapchain, sprite/mesh rendering, LBS, HUD targets
  and compressed textures. Compute passes, virtual texturing, OIT, SDF shadows,
  temporal resolve, motion vectors, bloom, capture and editor ImGui remain
  unported; `CreateMotionVectorTarget` now fails instead of silently returning an
  RGBA8 target.

### Automatic backend selection

Leave `DeviceDesc::backend` at `Backend::Null` and pass a native window:
`RHI::Create` probes **Vulkan first**, then Metal on Apple, then D3D12 and D3D11
on Windows. `FBY_RHI_BACKEND=vulkan|d3d12|d3d11|metal|null|auto` is a diagnostic
override; an explicit backend fails loudly instead of silently switching, an
unknown value is rejected, and an explicit `null` really selects the null device
instead of falling back into the auto probe.
`auto_backend` covers the preference on this machine and reports `vulkan`.

### Optional vendor release

`CMakePresets.json` now has `release-vendor-clang` (RelWithDebInfo,
`FBY_WITH_FIDELITYFX=ON`, `FBY_REQUIRE_VENDOR_SDKS=ON`). It is separate from
`release-clang`; the core preset stays vendor-free. With FidelityFX enabled CMake
copies the signed upscaler and frame-generation DLLs (DX12/VK plus loaders) into
`bin/` and `bin/tests/`.

### DX material pipelines

D3D11 keeps the engine sprite shaders; `SupportsRuntimeShaderPipelines()` stays
false there. D3D12 exposes `CreateShaderPipeline` for a cooked fragment stage and
selects the material PSO per `DrawBatch::pipeline`, falling back to the default
sprite PSO. The vertex stage is always the engine material-sprite VS, compiled in
both SM 5.0 and SM 6.0; the backend inspects the fragment blob and picks the
matching model so D3D12 never mixes DXIL with DXBC in one PSO. MaterialGraph
cooking to HLSL/DXIL stays offline (`fby_matc --dx12`), and the PSO cache avoids
first-use hitches.

### Offline DX shader bytecode

D3D12 sprite/motion/temporal/sharpen/post-FX/bloom/grading/present-blit pipelines
and D3D11 SM5 pipelines use precompiled DXBC from
`Source/Engine/RHI/Shaders/DxShaderBlobs.hpp`; no
`D3DCompile` runs on those startup paths. D3D11 feature-level 10 still falls back
to runtime compilation for the SM4 variant; `FBY_D3D11_FORCE_FL10=1` exercises
that path in `d3d11_rhi_fl10`.

After editing `Source/Engine/RHI/Shaders/d3d11/*.hlsl` or
`d3d12/*.hlsl`, regenerate the header with:

```text
python Source/Tools/Dev/CompileDxShaders.py
```

`fxc.exe` comes from the Windows SDK/MSVC Build Tools; this is a developer tool,
not a runtime or setup dependency. MaterialGraph DX cooking is split in two stages. `fby_matc --dx12` translates
SPIR-V to HLSL with SPIRV-Cross, writes `<material>.hlsl`, and (when `dxc` is
available) compiles `.frag.dxil`/`.vert.dxil`. The cooked `.femmat` v2 payload
carries the optional DXIL stages. D3D12 exposes `CreateShaderPipeline` for a
sprite-domain payload with DXIL, and `Submit` selects the material PSO per `DrawBatch::pipeline` (falling back to the
default sprite PSO). The material root signature carries the texture table plus a
28-dword constant block; `Submit` fills it from the packet camera and material
clock (sprite layout: viewport/centre/zoom, frame slot, material time/viewport;
mesh layout: view-projection and material clock) instead of binding zeros. The mesh domain uses the same material PSO path for CPU-expanded mesh draws.
Material PSOs are stored in `ID3D12PipelineLibrary` and serialized to
`FBY_D3D12_PSO_CACHE` (default `Saved/<executable>_pso_cache.bin`, one file per
executable so parallel test binaries never share a cache), so a second launch
loads the cached PSO instead of recreating it.

## Audit status

The render-API audit findings are tracked here; each fixed item is exercised by a
test (`vulkan_rhi` with the validation layer, `d3d11_rhi` and `d3d12_rhi` with
their debug layers).

The D3D12 debug layer stays opt-in (`FBY_D3D12_DEBUG=1`): on the current NVIDIA
driver it intermittently ends the process at teardown after the first post-FX
frame without emitting a validation message. The same test is stable and silent
without the layer, so the default test run does not enable it; D3D11's debug
layer runs clean.

### Fixed

- Vulkan: upscale descriptor sets and per-view effect sets, `UpscaleRenderTarget`
  early-out, swapchain `TRANSFER_SRC`/`TRANSFER_DST` usage, present-blit barriers,
  bloom and colour-grading resources, extra validation coverage for clustered
  lights, SDF shadows, GI probes and the accessibility composite.
- D3D12: per-frame vertex ring, CPU camera/rotation/UV transform, depth state,
  motion PSO, persistent descriptor heaps, resize/VSync, scene + post-FX +
  bloom + colour grading, filled 28-dword material constants, graph post-FX
  block, accessibility composite, real depth SRVs for temporal, sRGB capture,
  depth copies, capability reporting and uploaded-byte statistics.
- D3D11: the same sprite/motion/post-FX/bloom/grading features, resize/VSync,
  real depth SRVs for temporal, sRGB capture, depth copies, resize state reset,
  and a clean debug layer (SRV/UAV unbinding, scene render-target release before
  the bloom compute).
- Shaders: FSR1 modes, motion/temporal semantics, HLSL ports for post-FX, bloom
  and colour grading, regenerated DXBC/DXIL bytecode.
- Cross-cutting: explicit backend override semantics, statistics parity, removed
  dead heaps, present-target validation. D3D11 and D3D12 present a differently
  sized render target with a fullscreen scaling blit instead of `CopyResource`
  (which silently failed on a size mismatch). The DX HUD composite is a blended
  overlay with nearest/linear upscaling instead of a scene-replacing copy.
  `UpdateTextureArrayLayer` is implemented on both DX backends (D3D11 arrays are
  `DEFAULT`, not `IMMUTABLE`, so a layer can stream; D3D12 stages through an
  upload buffer and a one-shot copy list). `CreateTextureArrayFromRenderTarget`
  wraps a completed target as a one-layer array on both DX backends, so
  `StaticBakeRenderer` no longer depends on Vulkan for the bake path.
  `UpscaleRenderTarget` on DX now runs a dedicated nearest/bilinear/bicubic
  fullscreen pass (`upscale.hlsl`); FSR1 has no Direct3D port and uses the
  bicubic filter.

### Remaining

- Material instances (per-material runtime parameter values). The graph stores
  literals only; an instance layer needs a parameter table in the asset and a
  uniform block emitted by the graph compiler.
- Metal renderer surface (virtual texturing, OIT, SDF shadows, editor ImGui).
  Depth attachments, motion vectors, temporal resolve, spatial upscale, HUD
  composite, render-target capture, colour/depth copies and the present blit are
  implemented; the rest cannot be built or verified on Windows and remain a
  separate macOS-gated work item.
