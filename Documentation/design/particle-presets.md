# Particle preset data packs

Status: 0.4.0-alpha.1 tree. `.fbyparticle` is a TOML data pack for
`ParticleEffectSettings`. It adds ready-made presets without a DLL, a plugin
ABI change or a renderer hook. The format mirrors `.fbyvoice`: engine code owns
the registry and the loader, a pack owns the numbers.

## Registry and loader

`ParticlePresetRegistry` holds up to 128 copied descriptors. `RegisterBuiltIns()`
adds the 16 engine presets (`Particle.Sparks` … `Particle.Frost`) with the
`ParticlePresetBuiltIn` flag. Pack entries are added with an owner pointer, so a
project reload can call `RemoveOwner()` without disturbing another pack.

```cpp
Fby::ParticlePresetRegistry Presets;
Presets.RegisterBuiltIns();
std::string Error;
if (!Fby::LoadParticlePresetFile("assets/particles/pixel_fx_combat.fbyparticle", Presets, nullptr, &Error))
{
    LogError("particles", "{}", Error);
}
const Fby::ParticlePresetDesc* Spark = Presets.Find("PixelFX.Combat.Sparks");
if (Spark != nullptr)
{
    Fby::ParticleEffectComponent Component{};
    Component.FromSettings(Spark->Settings);
    // emplace Component + Transform2D and let ParticleSceneSystem drive it
}
```

`LoadParticlePresetToml()` accepts the same text directly, which is what tests
and tools use. A bad id, unknown shape, unknown emission shape, malformed colour
or duplicate id fails with the preset id in the error string; valid settings are
clamped on registration.

## Format

```toml
[pack]
id = "PixelFX.Combat"
name = "Pixel FX Combat"

[[presets]]
id = "PixelFX.Combat.Sparks"
name = "Sparks"
category = "Combat"
shape = "Spark"
emission_shape = "Point"
max_particles = 512
seed = 30201
direction = [0.0, -1.0]
gravity = [0.0, 300.0]
wind = [0.0, 0.0]
emission_extents = [0.0, 0.0]
spread = 0.9
speed = [120.0, 260.0]
lifetime = [0.25, 0.60]
size = [2.0, 0.0]
rotation = [0.0, 1.0]
drag = 1.5
turbulence = 0.0
swirl = 0.0
rate = 160.0
burst_count = 0
burst_interval = 0.0
duration = 0.0
loop = true
additive = true
colors = ["#FFE060FF", "#FF602000"]
```

Shape names are the 12 `ParticleShape` slots: `Square`, `Circle`, `Ring`,
`Glow`, `Star`, `Diamond`, `Spark`, `Smoke`, `Bubble`, `Leaf`, `Snowflake`,
`Heart`. Emission shapes are `Point`, `Circle`, `Box`, `Line`. Colours are
`#RRGGBB` or `#RRGGBBAA`. `[pack]` is metadata only; every `[[presets]]` entry
is independent and can be loaded into one registry.

## Shipped packs

| File | Ids | Character |
|---|---|---|
| `assets/particles/pixel_fx_combat.fbyparticle` | `PixelFX.Combat.Sparks`, `.Blood`, `.Debris`, `.Explosion`, `.HitFlash`, `.MuzzleFlash` | hit feedback, bursts and one-shot flashes |
| `assets/particles/pixel_fx_ambient.fbyparticle` | `PixelFX.Ambient.Fireflies`, `.Leaves`, `.Snow`, `.Rain`, `.FogMotes`, `.Embers` | world ambience with wind, swirl and slow rates |
| `assets/particles/pixel_fx_status.fbyparticle` | `PixelFX.Status.Poison`, `.Frost`, `.Burn`, `.Shock`, `.LowOxygen`, `.Radiation` | status overlays and pulses |

Each pack is 6 presets; the three packs add 18 to the 16 built-ins. A pack is
content, not a plugin: shipping a pack is copying a file, and a missing pack
leaves the built-ins intact.

## Palette and shape notes

Pack colours are explicit RGBA. A game that wants palette-driven presets can
resolve the pack colours against its active palette before spawning, or ship a
second pack per theme. Shape masks remain the game's white-mask atlas; a pack
only selects a slot. The same 12-slot contract is documented in
[particle effects](particle-effects.md).

## Tests

`particle_presets` covers built-in registration, duplicate rejection, owner
removal, the inline TOML path, invalid shape rejection and loading all three
shipped files (16 + 18 presets). `particle_effects` and `particle_scene` cover
the simulation and the scene adapter.
