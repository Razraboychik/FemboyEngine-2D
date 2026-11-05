# Camera motion and screen effects

The camera stack separates the **base camera** from the **resolved render
camera**. Gameplay, scripts and aim code read the base pose; only the renderer
reads the resolved pose. This keeps cosmetic motion out of simulation state and
prevents camera shake from being written back into `Camera2D`.

## Motion channels

| Channel | Owner | Notes |
|---|---|---|
| Follow and look-ahead | camera stack | base only |
| Trauma shake | camera stack | seeded value noise, never accumulated into base |
| Scripted offset | camera stack | fades with a duration |
| Scripted zoom / roll | camera stack | additive on the resolved pose |
| Screen distortion | camera stack + post-process | double vision, stretch, warp, chromatic split, wobble |
| Flash guard / colour vision | accessibility kernel | unchanged |

`CameraStackSystem::SetMotionScale` multiplies shake, scripted offset, zoom,
roll and distortion. The final scale is `script scale * accessibility
presentation.motionScale`, so a game can honour "reduce motion" even when a
script asks for a strong effect.

## Shake

Trauma remains bounded in `[0, 1]` and decays per second. Intensity is
`trauma * trauma`, so small impulses stay subtle. Displacement uses deterministic
value noise keyed by `CameraStackDesc::seed`; separate phases drive X, Y and roll.
The result is clamped by the authored amplitude and `motionScale`. Clearing
effects returns the resolved camera exactly to the base pose.

## Distortion primitives

The engine does **not** own a gameplay state such as "drunk". It provides the
building blocks a game can compose:

- `CameraDistortionDoubleVision` — a ghost sample at a small UV separation.
- `CameraDistortionStretch` — independent X/Y UV scale, including a deliberately
  non-proportional stretch such as X 1.4 / Y 0.7.
- `CameraDistortionRadialWarp` — barrel/pincushion lens warp.
- `CameraDistortionChromaticSplit` — extra RGB separation independent of the
  static postfx chromatic setting.
- `CameraDistortionWobble` — low-frequency UV displacement.

The composite shader receives these through transient post-process constants,
not through the serialized `PostFxSettings` policy. A game can model intoxication,
poison, heat haze or a damaged visor as its own component and translate that
state into these primitives. HUD text is composited after the post-process pass
and stays readable.

## FemboyScript

Text functions:

```text
camera_add_trauma(amount)
camera_distort(flags, strength, param_x, param_y, duration)
camera_offset(x, y, duration)
camera_zoom(zoom, duration)
camera_roll(radians, duration)
camera_clear_effects()
camera_set_motion_scale(scale)
```

`flags` is a bit mask: `1` double vision, `2` anisotropic stretch, `4` radial
warp, `8` chromatic split, `16` wobble. Duration `0` keeps the effect until it is
replaced or cleared. A game-side intoxicated state can call
`camera_distort(2, 0.0, 1.35, 0.72, 8.0)` for a melt/stretch, or
`camera_distort(1, 0.6, 0.0, 0.0, 8.0)` for double vision.

LogicGraph exposes the same operations:

- `Camera Add Trauma`
- `Camera Distort`
- `Camera Offset`
- `Camera Zoom`
- `Camera Roll`
- `Camera Clear Effects`
- `Camera Motion Scale`

The VM and AOT host share the same `CameraStackSystem` service through
`VmContext::camera`; generated plugins call the ABI-versioned camera callbacks.

## Accessibility

- `motionScale = 0` removes shake, sway, scripted offsets, zoom, roll and
  distortion, and restores the exact base each frame.
- The engine does not add flashing or strobe effects; flash guard remains in the
  accessibility post-process path.
- Scripted camera effects are cosmetic and excluded from rollback snapshots by
  default. Replay can recreate them from the seed and sim time.

## Verification

- `camera_effects` — base/resolved split, motion scale zero, writeback excludes
  effects, distortion lifetime and clear.
- `script_camera` — text and visual camera nodes compile and run.
- `accessibility_composite` — post-process constants and flash guard.
