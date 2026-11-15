# Upscaler hardware validation

This page records a real-GPU run of the temporal path, the vendor adapters and
the 16K benchmark. It is separate from the headless CTest suite: the GPU smoke
runs only when a Vulkan 1.4 device and the vendor runtimes are present.

## Test machine

| Item | Value |
| --- | --- |
| GPU | NVIDIA GeForce RTX 4060 Laptop GPU |
| Driver | NVIDIA 621.24, Vulkan API 1.4.357 (NGX runtime unchanged) |
| OS | Windows 11 x64 |
| Build | `build-ffx`, `FBY_WITH_FIDELITYFX=ON`, `FBY_WITH_DLSS=ON`, tests ON |
| Runtimes | `amd_fidelityfx_upscaler_vk.dll`, `amd_fidelityfx_framegeneration_vk.dll`, `amd_fidelityfx_loader_vk.dll`, `nvngx.dll`, `nvngx_dlss.dll`, `nvngx_dlssg.dll` beside the test executable |

The AMD Radeon 890M on the same laptop reports Vulkan 1.3.302 and is rejected by
the 1.4 device gate. It is not a validation target; the discrete GPU must be
selected with `FBY_VULKAN_DEVICE=NVIDIA` or a single-ICD `VK_ICD_FILENAMES`.

## Results (2026-11-13)

`fby_static_bake_gpu_test` with the build above:

| Check | Result |
| --- | --- |
| Temporal capabilities | `fsr1=1 fsr2=1 motion=1 fg=1` |
| RG16 motion target/pass | pass |
| Temporal history/resolve/depth/color | pass |
| FSR2-compatible resolve | pass |
| Custom frame generation | pass |
| FSR2 vendor adapter (`ffxCreateContext`) | pass |
| FSR3 vendor frame generator (`ffxCreateContext`) | pass |
| DLSS adapter (`NVSDK_NGX_VULKAN_Init`) | fail, `0xBAD00002` (`PlatformError`) |

The engine path is validated end to end on the RTX 4060, including the
FidelityFX FSR2 and FSR3 contexts. DLSS remains open: the NGX runtime rejects
`NVSDK_NGX_VULKAN_Init` with `NVSDK_NGX_Result_FAIL_PlatformError`. The DLLs load
and the NGX parameter vtable is used; the failure is in NGX/driver/SDK
compatibility, not the engine's capability probe. A driver update through
NVIDIA App (621.24) kept `nvngx.dll` at 30.0.14.9516 and did not change the
result; the SDK DLLs and the game-provided 310.4.0 DLSS DLL were both tried.

## Bug fixed by this run

`CreateTemporalResources` allocated a descriptor pool with `maxSets = 1` but
allocated one set per frame in flight. The second allocation failed, so the
whole temporal path was reported unavailable on every GPU. The pool is now sized
by `kMaxFramesInFlight`. This was the reason FSR2/FSR3/custom-FG caps stayed
false even on hardware that supports them.

The FidelityFX loader also relied on `ffxLoadFunctions`, whose body is compiled
out unless `_WINDOWS`/`PLATFORM_WINDOWS` is defined by the build. Both FSR2 and
FSR3 now resolve `ffxCreateContext`/`ffxDestroyContext`/`ffxConfigure`/
`ffxQuery`/`ffxDispatch` directly from the loaded DLL.

## Commands

```powershell
$env:FBY_VULKAN_DEVICE = "NVIDIA"
# or force one ICD:
$env:VK_ICD_FILENAMES = "C:\Windows\System32\DriverStore\FileRepository\...\nv-vk64.json"
$env:FBY_GPU_LOG = "1"
.\build-ffx\bin\tests\fby_static_bake_gpu_test.exe
```

`FBY_GPU_LOG=1` prints the selected device. A run is valid only when the log
names the discrete GPU and `gpu_timestamps_valid=1`; otherwise the engine fails
the explicit Vulkan request instead of silently using NullRHI.

## 16K note

The historical 16K table in [high-resolution-benchmark.md](high-resolution-benchmark.md)
is a 0.4-alpha target. The 2026-11-13 re-run on this RTX 4060 measured
11.8–27 ms at 15360x8640 (roughly 37–85 FPS), not 170. Re-run the three-run
`BenchmarkMatrix.py` median with clocks pinned before publishing a new number.
