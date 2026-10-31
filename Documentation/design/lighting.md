# 2D lighting and SDF shadows

Lighting has two paths: a deterministic CPU solve used by tests and the Null
backend, and a screen-space compute mask used by the renderer. Both read the
same `SdfField`, so baked and moving geometry behave the same.

## CPU solve

`Source/Engine/ECS/Lighting.hpp/.cpp` defines `Light2DComponent` (colour,
radius, intensity, offset, enable flag), `Light2DTargetComponent` (base colour,
emissive), `LightingSettings` (ambient, tonemap, light cap, SDF shadow options)
and `LightingSystem::Run` with quadratic falloff and additive lights.

### SDF shadows and ambient occlusion

`Run(registry, settings, const SdfField*)` sphere-traces the existing `SdfField`
from each lit sprite toward each light with a fixed step budget (`SdfMaxSteps`)
and a penumbra width (`SdfSoftness`). The closest approach along the ray produces
the soft shadow factor; `SdfAmbientOcclusion`/`SdfOcclusionRadius` darken creases.
A ray that enters solid geometry is fully occluded. Everything is fixed-capacity,
deterministic and allocation-free.

### Dynamic casters

The static field only describes baked terrain, so moving entities would never
shadow anything. `Light2DOccluderComponent` (16 bytes: circle radius or box half
extents, plus flags) is gathered into a fixed `OccluderSample[64]` array every
pass and combined with the static field by distance (`min`), then sphere-traced
together. A receiver never shadows itself (its own occluder is skipped), so a
crate can cast onto the ground while staying lit.

### Output split

The system writes `Light2DResultComponent` (4 bytes, the light tint) instead of
overwriting `Sprite2D::Color`. `LightingSystem::Compose` multiplies RGB and
preserves the authored alpha, and the render extract composes. The component
write happens in a post-pass outside the view iteration, so no structural change
happens while iterating.

`Light2DComponent` and `Light2DTargetComponent` are in the scene schema;
`Light2DResultComponent` is derived and never serialized.

## GPU SDF shadows

`SdfShadowPass` records a `sdf_shadow.comp` dispatch at half viewport resolution;
the editor builds, compiles and executes that graph every frame. The pass is
scheduled before the sprite pass, and `RHI::RecordComputePass` records the
dispatch plus a compute-to-fragment barrier into the frame command buffer.

- Each mask texel recovers its world position from the inverse sprite viewport
  transform, sphere-traces the static field plus the dynamic casters toward the
  sun and writes an `r16f` visibility. The sprite fragment shader samples the
  mask at its NDC+0.5 offset and keeps the per-pixel march as the fallback on a
  backend without compute.
- `FramePacket::sdf` (`Rhi::SdfLightingFrame`) carries a non-owning `field`
  pointer plus size/origin/cell, sun direction/softness/ambient, the step budget,
  occlusion settings and up to 16 dynamic casters.
- The static field uploads to a 256x256 R8 texture only when `fieldVersion`
  changes (pointer + dimensions). Dynamic casters travel in a 576-byte UBO
  rewritten every frame. Nothing allocates on the frame path.
- `SdfShadowConstants` (608 bytes) mirrors the compute block: camera, viewport,
  field bounds, sun, params, encode and the caster pairs.
- Vulkan creates the SDF texture, sampler, UBO and descriptor bindings once:
  1 = field sampler, 2 = block, 3 = mask sampler. The same bindings are written
  for the built-in atlas and every texture-array set. The mask is an `r16f`
  image in GENERAL, recreated on viewport size changes; its compute descriptor
  sets are a small per-frame ring.
- `RenderToTarget` applies the packet before recording, so the GPU reads the
  parameters for the scene pass; the UI-only `Submit` does not overwrite them.

### Authoring

`SdfSunComponent` (reflected: direction, softness, ambient, occlusion
radius/strength, steps, disable flag) is placed on an entity, and
`BuildSdfLightingFrame` collects it with `Light2DOccluderComponent` into the
render frame. Without a sun the GPU path stays off and the CPU lighting output is
used unchanged, which is also what the Null backend does.

## Composite shader

The composite record and `sdf_shadow_composite.frag` were removed: the mask is
sampled directly by the sprite shader, so there was nothing to composite. A
graphics consumer for full-screen post-processing is still tracked in
[ROADMAP.md](../ROADMAP.md).

## Tests

- `lighting`: falloff, additive clamp, disabled lights, ambient off, emissive
  passthrough, tonemap, unlit sprites untouched, SDF occlusion, open side lit,
  ambient occlusion, shadows disabled, compose round trip, dynamic circle/box
  casters, moving caster, disabled caster, self-shadow skip, and the frame builder
  (no sun/disabled sun, field view, steps/softness/ambient, occluder collection,
  box flag, field version).
- `render_weather`: builds the graph, executes it and checks one
  `sdf_shadow.comp` dispatch at half resolution with the 608-byte block, plus no
  compute dispatch when the frame has no SDF shadows.
