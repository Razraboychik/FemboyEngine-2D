# Particle preset packs

`.fbyparticle` files are TOML packs for `ParticleEffectSettings`. They are
content, not plugins: copy a file, load it with
`Fby::LoadParticlePresetFile`, and the built-in presets stay intact.

- `pixel_fx_combat.fbyparticle` — `PixelFX.Combat.*`: sparks, blood, debris,
  explosion, hit flash, muzzle flash.
- `pixel_fx_ambient.fbyparticle` — `PixelFX.Ambient.*`: fireflies, leaves,
  snow, rain, fog motes, embers.
- `pixel_fx_status.fbyparticle` — `PixelFX.Status.*`: poison, frost, burn,
  shock, low oxygen, radiation.

The format and the registry are documented in
`Documentation/design/particle-presets.md`. Shape slots refer to the game's
white-mask atlas; colours are explicit RGBA in these packs.
