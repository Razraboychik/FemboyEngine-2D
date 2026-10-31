# ADR 0002: materials belong to the engine (cooked SPIR-V, not runtime glslang)

- Status: accepted; steps 1 to 3 implemented (shipped path, mesh and sprite)
- Date: 2026-10-30
- Decision owners: engine rendering

## Context

The current tree splits the material system across two libraries:

- `Source/Engine/RHI/MaterialGraph.*` and `MaterialGraphCompiler.hpp` hold the
  data model, validation and the GLSL emitter. They ship with `fby_engine`.
- `Source/Editor/Infrastructure/MaterialGraphRuntime.*` holds the live driver:
  glslang, SPIR-V, `CreateShaderPipeline` and a private 128-entry LRU table. It
  ships only with `fby_editor`, because glslang is a private editor dependency.

`fby_sandbox` and every generated game link `FemboyEngine2D::Engine` only, so a
shipped build can validate and compile a material graph into text and then has
no way to turn that text into a pipeline. Materials were an editor feature.

Two more facts shaped the decision:

1. `Rhi::ShaderPipelineDesc` already takes SPIR-V bytes. The RHI boundary was
   correct; nothing on the runtime side of it existed.
2. Every other runtime-replaceable subsystem in this tree follows
   "cook compiles, runtime loads": `fby_shaderc` for engine shaders, `fby_aotc`
   for scripts (`FBY_WITH_SCRIPT_VM=OFF`), the texture cooker, `.fanim`. The
   material path was the only subsystem that compiled on the frame path's side
   of the boundary.

## Decision

1. **Cook-time compilation.** `fby_matc` (`Source/Tools/MaterialCompiler`) reads
   an authored `.fgraph`, runs `MaterialGraphCompiler` and glslang, and writes a
   `.femmat` payload. The shipped runtime never links glslang and never sees
   GLSL text.
2. **Engine-owned wire formats.** `Engine/RHI/MaterialAsset.*` defines both
   payloads. `.fgraph` is the graph storage (counts, nodes, pins, params, editor
   positions; the hash excludes positions). `.femmat` is a 32-byte header
   (magic `FBYM`, version, contract mask, graph hash, two stage lengths) followed
   by the vertex and fragment SPIR-V. Decoding is zero-copy and validates magic,
   version, sizes, SPIR-V words and contract consistency before returning spans.
3. **One pipeline cache, in the engine.** `Render::MaterialPipelineCache`
   (`Engine/RHI/MaterialPipelineCache.*`) is a fixed 128-entry table keyed by
   graph hash with a contract check, reference counts, residency at zero
   references, LRU eviction of unreferenced entries and a fixed error buffer. It
   never allocates and never formats a string on the frame path. A pipeline is
   destroyed only by eviction, `ReleaseAll()` or `Shutdown()`.
4. **Explicit backend capability.** `Rhi::RHI::SupportsRuntimeShaderPipelines()`
   defaults to false; Vulkan returns true. A backend without the capability keeps
   the engine shaders, and the cache says so instead of handing out a handle the
   frame cannot bind.
5. **The editor is an adapter.** `Fby::Editor::MaterialGraphRuntime` keeps the
   live recompile/hot-reload behaviour and the per-tab error strings, but its
   pipelines live in the engine cache. `MaterialRuntimeStats` reports the cache's
   created/destroyed/shared counters.
6. **The mesh path is the shipped contract.** `MeshRenderItem::materialPipeline`
   takes the cache handle id; `kDefaultMaterialPipeline` keeps the engine shader.
   Vulkan already groups mesh records by that id, so no backend change was needed.
7. **Sprite sort key.** Sprites carry a material bucket rather than a per-item
   field: the 64-bit `RenderKey` is 16 layer | 8 material bucket | 32 depth |
   8 sequence. `MakeRenderKey` keeps its signature and uses bucket 0, so every
   existing caller keeps its ordering; `MakeMaterialRenderKey` adds the bucket.
   The bucket is authored in `DrawOrder::MaterialBucket`, which is already part
   of the change mask, so no new component or watch bit was needed.
8. **Sprites bind materials per batch.** `MaterialCompileOptions::domain`
   selects a second fragment contract: the generated stage matches the
   `quad.vert` varyings and samples the item atlas slice through `vLayer`.
   `Fresnel` and `VertexPosition` are refused there because the quad block has
   no view-projection, but `Time` works: the sprite push block was extended from
   80 to 96 bytes with a material clock at offset 80 (`x` = seconds, `yz` =
   viewport), so a sprite shader reads the same `pc.materialParams.x` a mesh
   shader reads. `fby_matc --sprite` cooks that variant and sets
   `MaterialContractSpriteDomain`. `ChangeMaskRenderExtract::SetMaterialPipelines`
   maps buckets to pipeline ids and resolves each batch, and the Vulkan sprite
   loop binds `DrawBatch::pipeline` per run, restoring the engine quad pipeline
   afterwards. Slot 0 of the Vulkan material pool stays empty so
   `DrawBatch::pipeline == 0` keeps meaning "engine quad".
9. **No Substrate-style closures.** A material stays a fixed
   BaseColor/Opacity/Emissive DAG compiled to one pipeline per graph hash. Layer
   composition, if it is ever added, is a compile-time node family (resolved into
   straight-line GLSL), never a variable-length per-pixel closure list.

The editor follows the same contract: `MaterialGraphRuntime::SetDomain` switches
the live compile between mesh and sprite, the panel exposes it as a combo, and
`SaveGraph`/`LoadGraph` write and read the `.fgraph` payload the cooker consumes,
so an authored graph no longer lives in memory only.

## Migration plan

| Step | Content | Gate |
|---|---|---|
| 1 (done) | `.fgraph`/`.femmat` formats, `fby_matc`, engine cache, RHI capability | `material_asset`, `material_pipeline_cache`, `material_cook` |
| 2 (done) | editor runtime delegates to the engine cache; mesh items carry cache ids | `material_mesh_path`, `editor_infrastructure`, `graph_editor` |
| 3 (done) | sprite material contract (fragment shader over `quad.vert` varyings), new key layout, per-batch `pipeline` bind in the Vulkan sprite loop | `sprite_material`, `material_graph`, `material_cook` |
| 4 (optional) | compile-time layer stack (`Layer`/`LayerComposite` node kinds) | `material_graph` + cook |

## Consequences

- A new asset kind is real now: `.femmat` is produced by a build step and loaded
  by the runtime. The cook pipeline must copy it into the package.
- Materials work in shipped builds: `material_mesh_path` links only
  `FemboyEngine2D::Engine` and drives cooked bytes into mesh items.
- Metal and Null keep the engine shaders. A game that targets them must not rely
  on a material graph changing the image; the runtime reports the capability
  instead of failing silently.
- The graph hash is the pipeline identity. Editing a graph produces a new hash, so
  the old pipeline becomes an unreferenced LRU entry instead of being mutated.
- The editor's 128-pipeline table and the engine's are the same table, so an
  editor session cannot exhaust one budget while the other is empty.
- A sprite material is a bucket plus a pipeline id, not a new render path: the
  engine still emits one 48-byte `RenderItem` per sprite, and a batch that names
  a material only changes the bound pipeline.
- The sprite push range is 96 bytes, 16 more than the 80-byte mesh material
  block. The engine quad shader ignores the last 16 bytes; generated sprite
  shaders declare the full block so every member offset matches the layout.
