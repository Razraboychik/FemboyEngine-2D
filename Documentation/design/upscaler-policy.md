# Unified upscaler and FG policy

## Player UI

```text
Upscaler policy:
  Auto
  Universal (FSR)
  Off

Frame generation policy:
  Auto
  Universal (FSR3/custom)
  Off
```

The player does not choose DLSS/FSR/MetalFX directly.

## Resolution

`Auto` prefers:

```text
DLSS + DLSS FG on NVIDIA/Vulkan
FSR2 + FSR3 FG on other GPUs
FSR1 / custom FG as fallback
```

`Universal` always uses:

```text
FSR2 -> FSR1
FSR3 FG -> custom FG
```

`Off` uses bilinear upscaling and no frame generation.

## Internal values

Project and user files keep the full enums:

```text
upscale_policy = auto | universal | off
upscaler_backend = auto | fsr1 | fsr2 | dlss | metalfx | xess
frame_generation_policy = auto | universal | off
frame_generation = off | custom | fsr3 | dlss | xess
```

Old projects keep working. If a stored backend is unavailable, resolution
falls back to the universal chain.

## MetalFX

`metalfx` remains in the serialized enum for cross-platform projects, but its
capability is false until a Metal adapter exists. It is never shown on Windows.

## Safety

Presence of a DLL is not enough. The resolver and adapters check:

- RHI backend;
- operating system;
- GPU vendor;
- runtime presence/loadability;
- feature availability;
- motion vectors;
- HUD separation.

Every call is guarded; a failed check falls back and never crashes.

## Tests

- Auto chooses DLSS when NVIDIA caps are present;
- Universal chooses FSR2;
- Off chooses bilinear;
- Auto chooses DLSS FG when available;
- Universal chooses FSR3/custom FG.
