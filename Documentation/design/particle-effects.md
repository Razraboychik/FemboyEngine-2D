# Particle effects

Status: 0.5.0 tree. `ParticleEffectSystem` is the CPU, deterministic
particle path for gameplay effects: sparks, blood, dust, smoke, fire, embers,
bubbles, splashes, snow, rain, leaves, confetti, magic, explosions, poison and
frost. It is data-oriented and allocation-free after construction.

The GPU authoring path (`ParticleEmitterComponent` and `particle_sim.comp`) is a
separate, future-facing path; this document covers the CPU system that ships in
this tree. It is deliberately not connected yet: the 2D scale is thousands, not
millions, of particles; a CPU step is cheaper than a dispatch plus GPU/CPU
synchronization, and replay determinism would become vendor-dependent. A GPU
backend is worth revisiting only behind the same settings/component interface
when a project needs six-figure particle counts.

## Why a CPU path

- deterministic playback for rollback, replays and tests;
- no per-emitter compute dispatch or GPU buffer round-trip;
- the game owns the masks, the palette and the draw order;
- one fixed pool bounds memory and frame cost.

The engine does not ship particle textures. A game provides a **white-mask
atlas**: one layer per shape, alpha in the mask. The engine only selects a frame
(layer and UVs) and emits a `RenderItem`.

## Settings

`ParticleEffectSettings` is a primitive-only struct:

| Field | Meaning |
|---|---|
| `MaxParticles` | per-emitter cap; the global pool is 8192 |
| `Seed` | deterministic emitter RNG |
| `Shape` | one of 12 slots: square, circle, ring, glow, star, diamond, spark, smoke, bubble, leaf, snowflake, heart |
| `EmissionShape` | point, circle, box, line |
| `EmissionExtents` | radius/half-extents/half-length for the emission shape |
| `Direction`, `SpreadRadians` | cone direction and half-angle |
| `Gravity`, `Wind` | constant acceleration in world units per second squared |
| `SpeedMin/Max`, `LifetimeMin/Max` | per-particle random ranges |
| `StartSize/EndSize`, `StartRotation`, `RotationSpeed` | visual evolution |
| `Drag` | exponential velocity decay per second |
| `Turbulence` | random acceleration per particle per frame |
| `Swirl` | tangential acceleration around the emitter |
| `EmissionRate` | particles per second; 0 disables continuous emission |
| `BurstCount`, `BurstInterval` | periodic bursts; interval 0 means one burst at spawn |
| `Duration`, `Loop` | emitter lifetime and restart policy |
| `Additive` | additive blend flag for glow/fire/spark effects |
| `StartColor`, `EndColor` | RGBA endpoints, interpolated over lifetime |

`Clamp()` keeps every value in a safe range; `Spawn()` clamps a copy, so a
caller can pass edited preset data directly.

## Presets

`ParticleEffectPresets()` returns 16 ready presets. They are starting points,
not fixed content: copy the settings and change direction, rate, colour, size or
physics.

| Id | Character |
|---|---|
| `Particle.Sparks` | short additive sparks with drag |
| `Particle.Blood` | heavy droplets with gravity |
| `Particle.Dust` | slow drifting haze |
| `Particle.Smoke` | rising, expanding, fading smoke |
| `Particle.Fire` | fast additive glow |
| `Particle.Embers` | slow rising sparks |
| `Particle.Bubbles` | rising bubbles |
| `Particle.Splash` | one-shot radial burst |
| `Particle.Snow` | slow falling flakes with wind |
| `Particle.Rain` | fast falling streaks |
| `Particle.Leaves` | swirling leaves |
| `Particle.Confetti` | spinning colourful squares |
| `Particle.Magic` | turbulent additive stars |
| `Particle.Explosion` | one-shot radial glow burst |
| `Particle.Poison` | turbulent green rings |
| `Particle.Frost` | falling cyan diamonds |

## Shape atlas

`ParticleShapeAtlas` maps the 12 `ParticleShape` slots to a texture-array layer
and a UV rectangle. `ParticleShapeAtlas::Defaults()` maps slot `i` to layer `i`
with full-layer UVs; a game that packs all masks into one layer overrides the
UVs instead.

```cpp
Fby::ParticleShapeAtlas Atlas = Fby::ParticleShapeAtlas::Defaults();
Atlas.Frames[static_cast<std::uint32_t>(Fby::ParticleShape::Circle)].Layer = MyMaskLayer;
Atlas.Frames[static_cast<std::uint32_t>(Fby::ParticleShape::Circle)].UvMin = Fby::Vector2D{0.0f, 0.0f};
Atlas.Frames[static_cast<std::uint32_t>(Fby::ParticleShape::Circle)].UvMax = Fby::Vector2D{0.25f, 1.0f};
```

Masks are white RGBA with alpha coverage. Colours come from `StartColor` and
`EndColor`; a game resolves them from its active palette before spawning, so the
effect follows the same palette as the rest of the presentation. Additive
presets set `RenderFlags::Additive` on every item.

`BuildParticleShapeMask(shape, size, span<std::uint8_t>)` renders the same 12
slots into an 8-bit alpha buffer, so a project can ship generated masks instead
of authored PNGs:

```cpp
std::array<std::uint8_t, 64 * 64> Mask{};
const std::uint32_t Pixels = Fby::BuildParticleShapeMask(Fby::ParticleShape::Circle, 64, Mask);
// Upload Mask as the alpha of a 64x64 RGBA tile; repeat for the other slots.
```

## Usage

```cpp
Fby::ParticleEffectSystem Effects; // heap-own it: the pool is large
const Fby::ParticleEffectPreset* Preset = Fby::FindParticleEffectPreset("Particle.Fire");
if (Preset != nullptr)
{
    Fby::ParticleEffectSettings Settings = Preset->Settings;
    Settings.Direction = Fby::Vector2D{0.0f, -1.0f};
    Settings.EmissionRate = 120.0f;
    Effects.Spawn(Settings, Fby::Vector2D{120.0f, 80.0f});
}
// Shortcut: Effects.SpawnPreset("Particle.Fire", Fby::Vector2D{120.0f, 80.0f});

// Frame path:
Effects.Update(DeltaSeconds);

std::array<Fby::Rhi::RenderItem, 1024> Items{};
const std::uint32_t Count = Effects.BuildRenderItems(Items, Atlas);
```

`Spawn()` returns an emitter handle; `SetEmitterTransform()` moves it,
`StopEmitter()` stops new particles while live ones finish, and `Clear()` resets
the pool. `BuildRenderItems()` writes into the caller's span and returns the
count, so a frame can share one extract array with sprites.

## Frame cost and limits

- 8192 particles and 64 emitters total; per-emitter `MaxParticles` caps the
  share.
- `Update()` is O(emitters + pool) and never allocates.
- The step is clamped to 100 ms so a hitch cannot teleport particles.
- Emitters are released automatically when they stop emitting and their last
  particle dies.
- The object is large (roughly 0.8 MB); own it on the heap, not on a thread
  stack.

## Data packs

Ready-made presets can ship as `.fbyparticle` TOML packs instead of a DLL. See
[particle presets](particle-presets.md) for the registry, the format and the
shipped `PixelFX.Combat`, `PixelFX.Ambient` and `PixelFX.Status` packs.

## Scene path

`ParticleEffectComponent` is the scene-facing authoring component: a flat copy
of the settings, a `Flags` word with `ParticleEffectComponentPlaying`, and a
`RestartCounter` that respawns the emitter when bumped. Pair it with
`Transform2D`; the transform position and rotation drive the emitter and are
updated every frame.

`ParticleSceneSystem` scans entities with both components and keeps one emitter
per entity (up to 64). It starts an emitter when `Playing` is set, follows the
transform, restarts on a changed counter, and stops the emitter when `Playing`
is cleared or the component/transform disappears. `BuildRenderItems()` delegates
to the same pool, so the scene can append particles to its normal extract.

```cpp
Fby::ParticleEffectComponent Component{};
Fby::FillParticleEffectComponent(Component, "Particle.Fire");
Registry.emplace<Fby::ParticleEffectComponent>(Entity, Component);
// each frame:
Scene.Update(Registry, DeltaSeconds);
const std::uint32_t Count = Scene.BuildRenderItems(Items, Atlas);
```

The Sandbox demo spawns `Particle.Fire`, `Particle.Snow` and `Particle.Bubbles`
at fixed world positions; they share the sprite extract and are sorted after
the sprites on layer 8. Masks are the default white layer in that demo, so the
shapes read as tinted quads until the project supplies a mask atlas.

## Editor preview

`View -> Particle FX` opens a tuning panel. It owns one live
`ParticleEffectSystem`; selecting a preset, dragging direction, spread, rate,
speed, gravity, drag, turbulence, swirl, size, rotation or the two colours
restarts the preview immediately. `Playing` freezes it, `Restart` respawns the
current settings, and `Reset` reloads the selected preset. The panel draws the
`RenderItem` output with ImGui primitives; it does not write project files, so
the values are copied into gameplay code once the look is found.

## Coordinate and ordering rules

- Particles are stored in world space; an emitter transform moves the spawn
  point, not the existing particles.
- Emission direction is rotated by the emitter rotation.
- `RenderItem` carries the shape frame, colour, rotation and additive flag; the
  game's render extract sorts them with sprites by its usual keys.

## Tests

`particle_effects` covers the preset table, clamping, continuous rate, bursts,
lifetime cleanup, per-emitter cap and pool limit, gravity, drag, deterministic
replay of two systems, the shape atlas mapping, generated shape masks, the
additive flag, box emission bounds, emitter transforms and `Clear()`.
