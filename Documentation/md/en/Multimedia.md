# FemboyEngine2D — Multimedia & Animation Ecosystem

This document describes the modules added on top of the fixed core (SDL3 window,
Vulkan RHI, EnTT ECS, FrameArena/CommandBuffer, deterministic rollback, plugin
ABI, ImGui editor). It is written to be read next to the headers: every claim
below names the file that owns it.

## Module map

| Module | Public headers | Implementation | Target |
| --- | --- | --- | --- |
| FemboyScript 2.0 | `Source/Engine/Script/{Bytecode,EcsBridge,Vm,ScriptSystem,GraphCompiler,AotTranspiler,AotAbi,AotModule}.hpp` | `Source/Engine/Script/*` | `fby_engine` (AOT loader), `fby_aotc` (cook tool) |
| MaterialGraph | `Source/Engine/RHI/{MaterialGraph,MaterialGraphCompiler}.hpp`, `Source/Editor/Infrastructure/MaterialGraphRuntime.hpp` | `Source/Engine/RHI/MaterialGraph.cpp`, `Source/Editor/Infrastructure/MaterialGraphRuntime.cpp` | `fby_engine` (graph+GLSL), `fby_editor` (+glslang) |
| Video | `Source/Engine/Video/{VideoTypes,WebmDemuxer,Vp9Backend,VideoStream,VideoPlayer}.hpp` | `Source/Engine/Video/*` | `fby_engine` |
| FrameExporter | `Source/Engine/RHI/FrameExporter.hpp` | `Source/Engine/RHI/FrameExporter.cpp` | `fby_engine` |
| Texture cook | `Source/Engine/Assets/TextureFormat.hpp`, `Source/Engine/Assets/TextureCook.hpp` | `Source/Engine/Assets/TextureFormat.cpp`, `Source/Engine/Assets/TextureCook.cpp` | `fby_engine` / `fby_fempak` |
| DSP mixer | `Source/Engine/Audio/{Audio,DspGraph,OggOpus}.hpp`, `Source/Engine/Assets/AudioClipFormat.hpp` | `Source/Engine/Audio/*`, `Source/Engine/Assets/AudioClipFormat.cpp` | `fby_engine` + `Opus::opus` |
| Pink UI | `Source/Engine/UI/PinkUI.hpp` | `Source/Engine/UI/PinkUI.cpp` | `fby_engine` |

## 1. Formats (closed whitelists)

| Asset | Accepted | Rejected on purpose |
| --- | --- | --- |
| Video | WebM / EBML, `V_VP9`, optional alpha via WebM `BlockAdditions` (`BlockAddID=1`) | MP4 (`ftyp`), H.264, H.265, AV1, VP8 — the demuxer rejects the CodecID, not the extension |
| Texture source | PNG, TGA | JPEG (artifacts + no alpha): rejected by extension **and** by magic (`FF D8 FF`), even if renamed |
| Cooked texture | `.ftex` v2: BC7 (desktop), ASTC 4x4 / 6x6 (mobile), Rgba8 (legacy atlas/video), full mip chain | runtime PNG/JPEG decode: the RHI uploads blocks verbatim |
| Audio source | WAV PCM 16/24-bit, Ogg Opus / Ogg Vorbis | MP3 (extension **and** ID3 / `0xFFEx` sync magic) — gaps at seamless loops |
| Cooked audio | `.fpcm` v1: PcmS16 / PcmS24 / PcmF32, or Ogg clip metadata | float conversion for short SFX (PCM is copied verbatim into the mixer) |
| Logic | `.fbsc` bytecode v1 (canonical, hashable) | runtime graph objects: the VM only sees flat instructions |

## 2. FemboyScript 2.0 — two execution contours

* **Development (Editor/PIE).** `LogicGraph` (flat 20-byte nodes, 8-byte links)
  → `CompileLogicGraph()` → canonical bytecode (`ProgramHeader`, 8-byte
  instructions, Mirror offsets baked into `FieldBinding`). `ScriptRuntime`
  validates the program once (`ParseProgram`), resolves every component by
  `LayoutHash` through `EcsBridge` (`BindProgram`), and `FemboyScriptSystem`
  runs it per entity. No strings, no map, no `std::function`, no allocation on
  the hot path (enforced by `Source/Tests/ScriptVmTest.cpp` with a counting
  `operator new`).
* **Release (Asset Cooker).** `fby_aotc` reads the *same* `.fbsc` and emits a
  dependency-free C++26 translation unit: one flat function per event, typed
  component base pointers resolved once, direct float arithmetic, `goto` for
  control flow, `-O3 -shared` via clang/g++, loaded by `AotModule`. The module
  exports `fby_aot_module()`, the graph hash, source hash and every component
  `LayoutHash`; the loader refuses a plugin whose layout does not match the
  running engine. `AotHostApi` is plain function pointers — no EnTT, SDL or
  Vulkan types cross the DLL boundary.

Determinism: RNG is `DeterministicRng` (PCG32); arithmetic is one opcode one
operation; the bytecode is byte-identical for the same graph (tested). Cycles
are a compile error (Kahn pass), never a runtime object cycle.

## 3. MaterialGraph → GLSL → SPIR-V → PipelineHandle

`MaterialGraph` is a fixed-capacity DAG (nodes/pins/params, trivially copyable).
`MaterialGraphCompiler` emits GLSL 450 into a caller buffer and rejects overflow,
dangling pins, type mismatches and cycles. `MaterialGraphRuntime` detects a dirty
graph by hash, compiles in-process through `fby_shaderc_core` (the same glslang
configuration as the build-time shaders), and calls
`RHI::CreateShaderPipeline`. The RHI owns two `VkPipeline` objects per handle —
one for the swapchain pass, one for the shared offscreen pass — because a
VkPipeline is tied to a render pass. `MeshRenderItem::materialPipeline` selects
the pipeline; indirect draws are grouped by it, so mixing materials does not
disable multi-draw for the default material. Generated vertex shaders keep the
engine contract: set 1 SSBOs, `mat4 viewProjection` push constant, full 4x4
`viewProjection * model * position` for tilted 2.5D planes and perspective.

## 4. Video

`WebmDemuxer` parses EBML/Segment/Tracks/Clusters/BlockGroups, validates
`TimestampScale`, pixel size, alpha mode and `CodecID`, and rejects every
non-VP9 codec. `Vp9Backend` is a runtime-loaded libvpx function table (no link
dependency; tests inject a deterministic fake). Frames are decoded on a
`std::jthread` into a preallocated pool and cross to the render thread through a
fixed SPSC ring; `VideoSystem` (PostUpdate) uploads the newest due frame with
`RHI::UpdateTextureArrayLayer` into an RGBA8 texture array (one layer per pooled
frame). YUV→RGBA is BT.709, alpha is composited from the separate VP9 alpha
plane. `VideoStreamComponent` is a trivial component; decoder state lives in the
system's fixed slots.

`FrameExporter` renders a viewport offscreen with `RenderToTarget`, captures it
with `RHI::CaptureRenderTarget` (host readback, top row first) and appends the
frame to a `.fbyseq` sequence written through the VFS sink used by the editor.

## 5. DSP mixer graph

`DspGraph`: fixed bus tree (Master/Sfx/Music + dynamic buses with parent
routing), lock-free SPSC command ring (game thread producer, audio thread
consumer; full ring drops the command, never blocks the callback), atomic gain
parameters, peak meters, Ducking (sidechain, threshold/ratio/attack/release) and
Schroeder Reverb (4 combs + 2 allpasses, preallocated delay lines per bus).
`Process()` is noexcept and allocation-free (asserted by tests). WAV PCM is read
verbatim from `.fpcm`; Opus is decoded through the vendored libopus with an
in-tree Ogg page demuxer.

## 6. Pink UI

`WidgetCanvas` is SoA with hard capacities (512 widgets, 4096 glyph quads, 8 KiB
text pool). Layout is two deterministic passes: a `std::mdspan`-backed grid
(column/row tracks, gaps, padding, spans, auto rows) and the UGUI anchor/inset
convention. `WidgetCanvasSystem::BuildFrame` emits 48-byte `rhi::RenderItem`s into
caller storage, ascending `RenderKey`s and **exactly one** `DrawBatch` per canvas
(white layer 0 for panels, font-atlas layer 1 for glyphs). Text is drawn from
the mounted `.fbyfont` atlas set, one quad per glyph; a canvas with text and no
mounted atlas is refused instead of drawing a fallback font. The element set is
`Panel`, `Button` (face plus a centred caption), `Label`, `Image`, `ProgressBar`
(left-to-right or bottom-up), `Hypertext`, `Spacer`, `Border`, `Slider`,
`Checkbox`, `Scrollbar`, `Switcher`, `Overlay`, `Custom`, `Tooltip`,
`TextInput`, `NineSlice`, `ToggleSwitch`, `RadioButton` and `TextBlock`. Focus
is opt-in through `FocusPolicy` (default `None`) with tab index, groups, wrap, a
modal scope, arrow-key movement (`FocusMove`) and an optional focus ring in
addition to Tab. Visibility is
inherited, so hiding a parent hides its subtree and every kind can be hidden.
A `Switcher` selects its visible child through a pointer-free `WidgetHandle`
(slot plus generation), and `WidgetFlags::ClipChildren` clips descendants to a
parent rect with UV-correct quad trimming for hit testing, focus and rendering.
Tooltips use the same handle for their target and a hover clock advanced by
`WidgetCanvasSystem::Update`; `TextInput` adds a single-line UTF-8 caret/selection editor over the shared text
pool, with an optional cluster map built from the supplied shaper and a shaped
glyph path when the shape cache is configured. `TextBlock`
references a `TextService` block by `TextAssetRef` and renders it through
`TextLayout`/`TextRender`, keeping long and rich text out of the widget pool;
when a `TextShapeCache` and shaper are present it uses the HarfBuzz-shaped
render path. Inline `U+FFFC` atoms are emitted at their shaped placeholder
clusters (a Sprite atom gets its layer/UV from the asset's
`TextService::AddInlineSprite` entry, and shaped wrapping uses the atom width). `Text::TextRuntime` owns the service and shape cache once per host,
and the Sandbox/Editor apps bind it to `SystemContext` at startup, mounting the
IBM Plex TTF through a `TextFontStack` when available. `SetTextLineRange`
restricts a shaped block to a line window for long-document viewports, while
`SetTextBlock` pages between `TextStore` blocks.
`PointerDown`/`PointerMove`/`PointerUp` drive focus, hover, press and
Slider/Scrollbar drag capture. `Custom` widgets run an application paint
callback whose quads are budgeted in the same pass. Hit-testing, focus and press
state are allocation-free. `WidgetComponent` (16 bytes, Mirror-registered)
attaches a canvas to an entity; `Update()` resolves world placement from
`Transform2D`. The full element catalog, the focus contract, clipping and the
remaining UI work are in [Widgets.md](../../design/Widgets.md).

## 7. RHI additions

* `TextureArrayDesc::format` + mip chains; `CreateTextureArray` copies cooked
  blocks (BC7/ASTC/RGBA8) region-by-region into optimal-tiling images. ASTC is
  gated on the device feature; BC7 is core. No CPU decode.
* `UpdateTextureArrayLayer`: persistent upload arena, one copy per frame.
* `CreateShaderPipeline` / `DestroyShaderPipeline`: runtime SPIR-V modules, two
  pass variants, material grouping in the mesh indirect path. The mesh push
  block is 80 bytes and registered for VERTEX | FRAGMENT:
  `{ mat4 viewProjection; vec4 materialParams }` where `materialParams =
  { timeSeconds, viewportWidth, viewportHeight, 0 }`.
* `CaptureRenderTarget`: offscreen readback for FrameExporter.

## 8. Verification

Engine library and editor library build with clang-cl 23 / C++26 (`/std:c++latest`).
Tests are plain executables (no framework):

| Test | Covers |
| --- | --- |
| `script_vm_test` | program validation, VM ALU/branches, ECS field bridge, graph→bytecode, cycle/type/hash rejection, 0 allocations/1000 updates |
| `script_aot_test` | transpiler shape/determinism, AotAbi host bridge, stale LayoutHash rejection, missing plugin path |
| `material_graph_test` | DAG validation, cycles, deterministic GLSL, overflow, SPIR-V if glslang present |
| `dsp_mixer_test` | routing sums, ducking, reverb, SPSC ring under threads, no allocation in Process |
| `audio_cook_test` | WAV 16/24-bit, `.fpcm` validation, Ogg pages, MP3 rejection |
| `pink_ui_test` | mdspan grid, anchors, hit order, border/slider/checkbox/scrollbar quad counts, drag and toggle interaction, one-batch invariant, overflow, 0 allocations, ECS Update |
| `video_test`, `texture_cook_test` | WebM/VP9-alpha demux + codec rejection; PNG/TGA→BC7/ASTC + BC7 round-trip |

End-to-end checks performed on this machine (clang-cl 23, Ninja, Windows):

* full `ninja` build: `fby_engine`, `fby_editor`, `fby_fempak`, `fby_sandbox`,
  `fby_aotc`, plugin samples and every test binary link;
* `ctest`: 10/10 tests pass (rollback, mesh smoke, script VM/AOT, Pink UI, DSP
  mixer, audio cook, material graph, video, texture cook);
* `fby_fempak cook --target desktop` on a 1024x1024 PNG produces a `.ftex`
  v2 with format `bc7-srgb`, 9 mips and BC7 mode-6 blocks (verified at the bit
  level: block byte 0 has bit 6 set); `--target mobile --astc 6x6` produces
  `astc-6x6-srgb` (626,296 bytes vs 1,398,136 for BC7); a `.jpg` is rejected by
  both extension and magic;
* AOT path: `script_aot_test` dumps a half-velocity program and its generated
  C++ (`FBY_AOT_DUMP_DIR`), the C++ compiles with `clang-cl -O2 -LD` into a
  DLL, and `fby_script_aot_test.exe` with `FBY_AOT_DLL=<dll>` loads it through
  `AotModule` and observes the native update halving an EnTT `Velocity` — no VM
  involved.

## 9. Known gaps

The live backlog is [ROADMAP.md](../../ROADMAP.md); this chapter does not keep a
second copy. The gaps that were listed here are closed or moved: the script
text-language front end, the MaterialGraph node panel, the graph serializers and
the video/UI registration in the sandbox all exist now. The remaining specifics
are tracked as: per-frame texture uploads waiting on the device (transfer queue
and `WaitIdle` removal in P1), no hardware VP9 decode (out of scope; libvpx is
the system codec), `FrameExporter` PNG/GIF muxing (a tool step), and animation
state machines/timelines (already in the tree).
