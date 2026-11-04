# Static layer bake: cache design

## Goal

Replace thousands of static opaque quads with one cached surface without
paying full-screen 8K/16K memory or losing dynamic lights.

## Memory

Do not allocate one full-screen RGBA8 surface per resolution. Use a tiled BC7
atlas:

| Target | RGBA8 full | BC7 full | BC7 tiles, visible only |
|---|---:|---:|---:|
| 2560x1600 | 16 MB | 4 MB | 2-4 MB |
| 4K | 33 MB | 8 MB | 4-8 MB |
| 8K | 133 MB | 33 MB | 12-24 MB |
| 16K | 531 MB | 133 MB | 24-48 MB |

- tile size 256 or 512;
- LRU eviction;
- no depth in the cache;
- LDR sRGB BC7 is the default;
- HDR/bloom lives in a separate half-resolution buffer.

## Zoom

Bake at a reference zoom, then:

- mip pyramid for zoom-out;
- zoom buckets for zoom-in (`0.5x`, `1x`, `2x`, `4x`);
- or virtual-texture tiles requested per visible region.

The cache key contains `zoomBucket`; crossing a threshold invalidates only that
bucket.

## Invalidation

Split the bake into layers:

1. static albedo/geometry: bake once;
2. dynamic light: separate lightmap or shader pass;
3. animated sprites: never baked.

Dirty tiles are keyed by:

```text
{ layer, tileX, tileY, zoomBucket, lightVersion }
```

Only tiles touched by a changed light or a changed static item are rebaked.
Bake at most N tiles per frame and reuse the previous tile until the new one is
ready.

## Depth and HDR

- The cache has no depth attachment.
- The cached quad is drawn with the normal opaque depth path.
- HDR is not stored in the cache; bloom uses a separate low-resolution chain.

## When not to use it

If zoom and lighting change every frame, use static mesh batching instead:
one mesh per tilemap chunk, no bake memory, no invalidation.
