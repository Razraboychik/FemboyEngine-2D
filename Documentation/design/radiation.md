# Radiation screen effect

Radiation is an **engine post-process effect**, not a renderer plugin. The engine
owns the pass, the parameter clamp and the frame binding; a plugin may supply
presets only.

## Why not a plugin

A radiation look needs the scene colour, the frame time and the final composite
target. Those are RHI resources. Giving them to a plugin would duplicate the
render path, create a second shader contract and make the effect unavailable
without a DLL. The existing accessibility effect path already has the correct
consumer (`AccessibilityEffectConsumer` → `PostFxSettings` → composite shader),
so radiation reuses it.

## Look

The shader combines:

- animated value noise over the screen;
- horizontal scanlines/interference;
- tint towards radioactive green;
- vignette;
- desaturation;
- intensity.

The effect profile reuses the generic fields:

| Field | Radiation meaning |
|---|---|
| `intensity` | overall dose/severity |
| `pattern_scale` | noise scale |
| `blur` | scanline/interference strength |
| `tint` | dominant radiation colour |
| `desaturation` / `vignette` | accessibility-safe look |

No new ABI fields are needed. A preset provider can register
`FbyEffectDesc` with `kind = 3`; `PluginHost` maps it to
`AccessibilityEffectKind::Radiation`.

## Binding

The game drives intensity from its own radiation/dose value:

```cpp
effect.Intensity = ComputeRadiationDose();
```

The project/game chooses the active effect exactly like NVG/thermal/alcohol.
UI and subtitles can stay outside the effect by using a separate surface policy.

## Failure and limits

- zero intensity is a no-op;
- the pass is frame-boundary only;
- it does not alter simulation state;
- a missing preset falls back to no effect, not to a broken shader;
- photosensitivity policy still applies: FlashGuard remains independent and must
  not be disabled to make radiation "stronger".
