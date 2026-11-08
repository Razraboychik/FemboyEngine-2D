# Sprite atlases

Sprite atlases are built offline and consumed as a small runtime asset.

## Offline build

`fby_atlas` packs PNG files from one directory:

```bash
fby_atlas --input assets/sprites --output assets/generated/ui --page 2048 --padding 2
```

Packing is deterministic: sprites are ordered by height (descending), width
(descending) and file-name hash, then placed left-to-right in shelf rows. The tool
writes:

- `<output>.png` — the packed RGBA8 page;
- `<output>.fbyatlas` — page size/path and one `sprite <id> <x> <y> <w> <h>
  <pivotX> <pivotY>` line per sprite.

The id is a stable FNV-1a hash of the file name, so renaming a file changes its id
while reordering files does not.

## Runtime

`LoadSpriteAtlasAsset()` parses `.fbyatlas` into `SpriteAtlasAsset`:

- `SpriteAtlas` with a fixed array of `AtlasSprite` records (1024 max);
- page dimensions and the page image path;
- validation of page bounds, unique non-zero ids and required lines.

`FindAtlasSprite()` and `AtlasSpriteUv()` are the hot-path lookups. The texture
array/page upload and the batching key are owned by the host renderer; the asset
only supplies stable ids and UV rectangles. `TextureLayer` is a host-provided
slot in the texture array.

`SdfAtlas` is a different resource: it packs SDF bricks for the GPU field and is
unrelated to sprite pages.
