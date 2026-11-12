# OIT design for 2D transparency

## Current state

The project setting, per-effect flags and frame contract exist:

```toml
[render]
oit_mode = 0 # Off, Weighted, Accurate
```

```cpp
FramePacket::oitMode
RenderFlags::Oit
ParticleEmitterOit / ParticleEmitterOitDisabled
```

Diagnostics show `OIT mode` and `OIT items`. The weighted blended pass is
implemented for the swapchain and for render targets; the `Accurate` mode is
not, and the weighting remains an alpha approximation.

## Weighted Blended OIT

Two offscreen attachments:

- accumulation: RGBA16F, additive `ONE / ONE`;
- revealage: R16F, `ZERO / ONE_MINUS_SRC_ALPHA`.

Transparent geometry writes premultiplied colour times a depth weight. A
fullscreen pass resolves:

```
colour = accumulation.rgb / max(accumulation.a, 1e-4)
alpha  = 1 - revealage.r
```

Properties:

- order independent;
- single pass over transparent geometry;
- approximate for dense overlapping layers;
- no benefit for opaque sprites.

## Accurate OIT

Per-pixel linked lists in a storage buffer. Exact ordering, but high bandwidth
and memory. Only justified for glass/particles, not ordinary 2D sprites.

## Where the setting belongs

- project default: `ProjectSettings.render.oitMode`;
- per effect: emitter flags;
- per material/sprite: `RenderFlags::Oit`;
- runtime: the mode travels in `FramePacket` every frame.

## Implemented path

- `OitPass` renders `OitPacket` into additive accumulation and reveal targets;
- `RenderBlendMode::Additive/Reveal` pipelines;
- `oit_resolve.frag` resolves the two targets;
- editor routes `oitMode != Off` through `OitPass`;
- opaque packet excludes transparent items while OIT is active;
- GPU smoke test submits accumulation, revealage and resolve.

The current weighting is a simple alpha-weight approximation, not a full
depth-weighted Frosbee implementation.

## Editor and PIE

Both the editor swapchain path and the PIE offscreen target call
`OitPass::Render` for `FrameComposer::OitPacket()`. The backend creates a
resolve pipeline lazily for each render pass, so resolve works for the
swapchain and for `RenderTarget`s. If `oitMode` is `Off`, the transparent items
stay in the normal packet and no OIT target is allocated.

## Implemented status

1. accumulation/reveal targets are separate color targets;
2. additive/reveal blend variants exist;
3. `OitPacket` separates transparent items by depth-write flag;
4. `oit_resolve.frag` and `RecordOitResolve` composite the result;
5. editor and PIE route the pass through their render target or swapchain.

The remaining quality step is a true depth-weighted contribution and an exact
per-pixel list for `Accurate`; the current mode is an alpha-weight
approximation.
