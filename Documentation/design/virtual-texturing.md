# Virtual texturing

Virtual texturing (VT) is an opt-in engine feature: no resource exists unless the
host creates one with a valid description. This document is the normative
contract shared by the CPU residency system, the page table and the sprite
shader. The tile/array geometry in Complete Reference chapter 32 is background
from an earlier design; where the two disagree, this document and
`VirtualTextureFormat.hpp` win.

## Address contract

`Source/Engine/RHI/VirtualTextureFormat.hpp` is the single source for the
address math.

- **Page-table entry**: one `R32_UINT`. Bits 0-7 are the layer, bits 8-23 the
  slot, `0xFFFFFFFF` means "not resident". `UnpackVirtualTexturePageTable`
  rejects the sentinel and reserved bits.
- **Slot grid**: `pageSize / tileSize` cells per axis (32x32 for the default
  2048/64), `slot = y * axis + x`. `VirtualTextureSlotOrigin*` returns the texel
  origin inside the layer page.
- **World to tile**: floor division, so negative coordinates stay on a stable
  grid. `VirtualTextureTileLocalUv` returns the in-tile UV.
- **Layer prediction**: `VirtualTextureLayerForTile` uses the same hash as
  `VirtualTextureSystem::LayerForTile`. The hash has an asymmetric cross term; a
  plain `x * C1 ^ y * C2` mapped a tile and its mirrored neighbour to the same
  layer.
- **Feedback record**: one `R32_UINT` per sampled pixel. Bit 31 is valid, the two
  axes are 15-bit signed (range `[-16383, 16383]`), zero means empty. Two's
  complement pairs were rejected because `(-1, -1)` collided with the old
  all-ones sentinel.

`VirtualTextureSystem` uses the same helpers (`HashTile`, `LayerForTile`,
`Resolve`), so the CPU and GPU paths cannot drift.

## GPU resources

`RHI.hpp` exposes `VirtualTextureHandle`, `VirtualTextureResourceDesc`,
`CreateVirtualTexture`, `DestroyVirtualTexture`, `UploadVirtualTexturePage` and
`UpdateVirtualTexturePageTable`.

- Vulkan creates an RGBA8 page array (one page per layer) and an `R32_UINT`
  page-table image, both in a shader-ready layout.
- A page upload is a staging copy into the slot origin of the target layer; the
  table upload copies a tight `R32` block.
- Handle id 0 is invalid, freed ids are reused, `Shutdown` releases everything.
- The Null backend keeps the no-op/false contract.
- `CreateVirtualTexture` checks driver support for sampled RGBA8 pages and a
  sampled/storage `R32_UINT` table. If either is missing it returns an invalid
  handle, so the caller stays on the atlas path instead of crashing.

`VirtualTextureGpu.hpp` is the bridge: it maps a `VirtualTextureTileUpload`
(texel origin inside a layer page) to a slot, rejecting unaligned origins,
oversized tiles, short row pitches and out-of-grid slots, and adapts the result
to `VirtualTextureUploadFn`.

## Sampling

- `RenderFlags::VirtualTexture` (`1 << 5`) marks an item. The vertex shader
  forwards `vWorld` and the flag.
- The fragment shader resolves `world -> tile -> page-table cell -> (layer,
  slot)` with `texelFetch(usampler2D)` and performs manual bilinear filtering
  with four `texelFetch` calls clamped inside the 64x64 page tile.
- A missing or out-of-table page returns neutral grey. Items without the flag
  keep sampling the atlas, so existing scenes are unchanged.
- Sprite descriptors use binding 4 (page table, `usampler2D`) and binding 5
  (page array). Every sprite set is written with the active VT or with 1x1
  neutral fallbacks; `ApplyVirtualTexture` rewrites the sets when the frame binds
  a different VT.
- Sprite push constants are 64 bytes, four vec4s: `viewportAndZoom`,
  `centerAndPadding`, `vtWorld`, `vtLayout`. A packed 56-byte block was rejected
  because std430 pads `vtWorld` to 16 bytes; the vec4 layout removes the
  mismatch. The feedback pass adds a `frame` vec4 (80 bytes total).
- `ResolveVirtualTextureSample` mirrors the shader on the CPU and is what
  `vt_gpu` checks, so a sampling change has a CPU oracle.

## Feedback and streaming

- The fragment shader writes the packed tile id into an `r32ui` feedback image
  array (three slots, one per frame in flight) at quarter viewport resolution,
  only for VT fragments. Empty records are zero; the backend clears the slot
  before the render pass and copies it to a host-visible readback buffer after
  the pass.
- At `BeginFrame` the CPU reads the slot whose fence was just waited (a
  two-to-three frame delay, no mid-frame stall), dedups with
  `DeduplicateVirtualTextureFeedback` into a fixed 4096-entry array and exposes
  it through `RHI::ReadVirtualTextureFeedback`.
- `VirtualTextureSystem::BeginFrame`, `RequestTiles`, `SetFocus` and
  `SetMaxLoadsPerUpdate` form the streaming loop: requests are deduplicated
  against residents, sorted nearest-first around the focus point, and at most
  the per-frame budget is loaded; the rest are counted as deferred.
- Loads and LRU evictions append batched `VirtualTextureTableUpdate` records
  (packed layer/slot or the missing sentinel); the caller flushes them once per
  frame.
- Determinism: equal-distance ties break by tile `(Y, X)`, so two identical runs
  assign the same layer/slot addresses. `vt_streaming` runs the same request
  sequence twice and compares every `Resolve`.

## Editor surface

- `Editor/Panels/VirtualTexturePanel` ("Virtual Texture" in the View menu,
  hidden by default) shows frame, visible/resident, uploads/evictions, VFS
  misses, deferred requests, uploaded bytes, the VRAM budget and the pending
  page-table batch, with a live "Loads / frame" slider.
- Without a system (`EditorSystem::SetVirtualTextureSource`) the panel shows an
  explicit inactive state.
- Debug view: off / tint by page id / tint by slot. The host passes
  `FramePacket::virtualTextureDebugMode`; the backend puts it in the push-constant
  `frame` vec4; the shader returns a hash tint per (layer, slot), or magenta for
  out-of-table and non-resident pages. `FBY_VT_DEBUG=1|2` preselects a mode.
- `[virtual_texture]` in `project.femproj` carries `enabled`,
  `streaming_enabled`, `layers`, `page_size`, `tile_size`, `chunk_tiles`,
  `page_table_size`, `world_tile_size`, `budget_loads_per_frame`,
  `prefetch_tiles` and `debug_mode`. Structural values feed the layout at VT
  launch; runtime values are applied every frame. Load-time validation lives in
  `VirtualTextureSettings::Clamp`/`Issue`; the guidance is in
  [ProjectSettingsRecommendations](../md/en/ProjectSettingsRecommendations_EN.md).

## Diagnostics

- `FBY_VT_SELFTEST=1` runs a two-layer 128x128 self-test (one orange page and a
  64x64 table) and logs `VT self-test: ok`.
- The editor demo writes 100 synthetic chunks, pans the camera, drives streaming
  from feedback and probes the viewport; it logs
  `VT stream: resident=8 total uploads=48 evictions=40 misses=0 deferred=1` and
  `VT sprite visual check: ok`.
- `FBY_VT_STRESS=1` runs the ping-pong/zoom demo; 30 seconds held
  `resident=8 total uploads=150 evictions=142 misses=0` without a crash.
- `FBY_VULKAN_VALIDATION=1` requests `VK_LAYER_KHRONOS_validation`; a machine
  without the layer reports `validation: layer not available` and continues.

## Tests

`vt_format` (pack/unpack, sentinels, slot origins, world/tile floor math, layer
determinism, feedback round trip), `vt_gpu` (CPU mirror, dedup, Null backend),
`vt_streaming` (budget, residency, determinism, stress, VFS misses) and the
editor infrastructure test (panel clamp).

## Known limits

- The editor render target follows the dock content region times the framebuffer
  scale (clamped to 1..4096), and the feedback extent is a quarter of the target.
  A live resize therefore changes both; the backend re-creates the feedback
  images on an extent change (`EnsureVtFeedback`).
- VT and SDF are frame-global state: `ApplyVirtualTexture` early-returns for the
  same id and `ApplySdfLighting` overwrites one mapped block, so the PIE client
  and the editor draw in one frame share the last writer. The PIE packet does not
  set its own VT/SDF; a client-specific pass is tracked in
  [ROADMAP.md](../ROADMAP.md).
- Feedback image recreation can happen mid-frame without waiting for the device,
  so a live resize can free images still referenced by in-flight command
  buffers (ROADMAP P1).
- VT stays opt-in: the editor creates a resource only under `FBY_VT_SELFTEST=1`.
