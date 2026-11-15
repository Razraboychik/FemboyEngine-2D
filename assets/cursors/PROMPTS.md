# Cursor prompt pack

The editor accepts a 32-bit BMP with magenta `(255, 0, 255)` as the transparent
key (`themed_cursor` + `cursor_image` in Editor Settings). Generate the art first,
remove the background, then convert it with the last section. Keep the arrow tip
at the extreme top-left; the editor hotspot is `(2, 2)`.

## Shared prompt

> Single game cursor sprite, 64x64, pixel-art / clean vector hybrid, a chubby
> cat-ear arrow pointing to the top-left, thick dark plum outline, soft pastel
> pink fill, small white muzzle. Centered composition, crisp 1-pixel outline,
> readable at 32x32 and 64x64, no perspective, no motion blur, no drop shadow,
> isolated on a pure magenta `#FF00FF` background, no other objects, no text
> outside the face, no watermark, no frame, no border.

**Negative prompt:** background, checkerboard, floor, shadow, glow, blur,
gradient halo, multiple cursors, mouse, text caption, logo, watermark, frame,
border, realistic photo, 3D render, extra ears, extra faces, cropped tip.

## Variant faces

Replace the muzzle line in the shared prompt with one of these:

| Variant | Muzzle prompt | File name |
|---|---|---|
| `:3` | "small white cat muzzle with a black `:3` mouth, relaxed happy expression" | `femboy_cursor.bmp` |
| `uwu` | "small white muzzle with closed happy `^ ^` eyes and a `w` mouth, blush marks" | `femboy_cursor_uwu.bmp` |
| `owo` | "small white muzzle with round open eyes and a tiny `o` mouth, curious expression" | `femboy_cursor_owo.bmp` |
| `F` | "bold capital F on the arrow body instead of a face, minimal and restrained" | `femboy_cursor_f.bmp` |

## Accessibility variants

Ask for a **high-contrast 32x32 edition** as well: black arrow, white 2-pixel
outline, no pastel fill. It stays visible on light and dark desktops and is the
safer default for low vision. A monochrome edition also survives Windows pointer
schemes and remote desktop compression.

## Background removal and conversion

```python
from PIL import Image

src = Image.open("generated.png").convert("RGBA")
# Keep only the sprite: drop a baked background that is not already magenta.
pixels = src.load()
for y in range(src.height):
    for x in range(src.width):
        r, g, b, a = pixels[x, y]
        if a < 8 or (g < 24 and r < 24 and b < 24):
            pixels[x, y] = (0, 0, 0, 0)

# SDL loads BMP without alpha, so bake a magenta key for the transparent pixels.
keyed = Image.new("RGB", src.size, (255, 0, 255))
keyed.paste(src, (0, 0), src)
keyed.save("femboy_cursor.bmp")
```

Do not leave magenta inside the art; it will become a hole. Check the converted
BMP in the editor at 100% and 200% UI scale, and on both a light and a dark
wallpaper. Uncheck **Themed cursor** to restore the platform arrow at any time.
