# SDF characters

[English](sdf-characters.md) | [Русский](sdf-characters_ru.md)

Deformable 2D characters use a bone-bound SDF surface instead of a distance LOD
chain. A character is a fixed set of primitives; each primitive is attached to
one skeleton bone and evaluated through that bone's current global matrix. The
same evaluation path runs at every zoom and every distance.

## Data

`Source/Engine/Physics/SdfCharacter.hpp`:

- `SdfBonePart` - an `SdfShape`, a bone index, a local offset/rotation and a
  smooth-union radius;
- `SdfCharacterComponent` - up to 64 `SdfBonePart` values, trivially copyable;
- `SdfCharacterField` - an owned dense sample grid produced by a bake.

The component never stores pointers. It serializes and snapshots exactly like
the other fixed-layout components.

## Evaluation

`SdfCharacterDistance` transforms the world point through the inverse bone
matrix, evaluates the analytic shape and unions all parts with
`SdfSmoothMin`. `SdfCharacterGradient` uses central differences and
`SdfCharacterCoversPoint` is the sign test. The cost is `O(parts)` per query and
does not change with camera distance; there is no distance LOD switch.

`BakeSdfCharacterField` samples the same evaluation into a dense grid (capped at
512x512). `BuildSdfCharacterBrickMap` packs that grid into the existing sparse
brick representation and `PackSdfAtlas` turns it into the GPU atlas image.
`BuildSdfCharacterMesh` emits a solid-cell triangle bridge for renderers that
consume the existing mesh path. `Rhi::SdfCharacterRenderCache::Build` walks the
ECS for `SkeletonComponent + SdfCharacterComponent`, evaluates the pose, bakes a
fixed-resolution field and produces one mesh per character. Passing
`cache.Meshes()` to `FrameComposer::BuildFramePacket` copies them into the
packet's unskinned mesh items. The brick atlas remains the SDF/render path. The
dense bake is the authored/preview path.

## Skinned mesh option

`SkeletalAnimation::Deform` remains available when a mesh is a better fit for a
part (for example a face texture). The SDF path is preferred for silhouette,
collision and destructible cloth/skin because it deforms without a CPU vertex
resample. Both paths read the same `SkeletonComponent`.

## Editor

`Editor/Panels/SdfCharacterPanel` edits `PartCount`, the per-part bone index,
the shape family and the `SdfShape` fields on the selected entity. It bakes a
fixed 48x48 preview and draws it through the editor's ImGui draw list, so the
preview resolution is independent of the viewport zoom.

## Verification

`fby_sdf_character_test` checks solid/empty points, gradient direction, bone
deformation, field baking, brick/atlas packing, the sample cap and the absence
of a distance LOD switch.
