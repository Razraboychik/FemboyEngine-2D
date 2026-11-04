# Custom frame generation

## Settings

Project:

```toml
allow_frame_generation = true
frame_generation = 1 # Custom
```

Player settings expose the same mode. Vendor FG and custom FG are mutually
exclusive.

## Policy

`FrameGenerationPolicy` gates:

- ready/disabled/unsupported;
- low FPS;
- HUD not separated;
- reset history.

`FramePacing` decides whether a generated frame is allowed.

## Editor

When custom FG is active:

1. a generated target at display size is created;
2. `CustomFrameGenerator::Initialize`;
3. color, depth, motion, history and output are submitted;
4. `Generate` performs temporal resolve;
5. after the real frame, `PresentGeneratedFrame` acquires a swapchain image,
   blits the generated target and presents it.

HUD:

- generated frame is blitted first;
- if the HUD is visible, a dedicated `LOAD` render pass draws the HUD
  pipeline over the generated swapchain image before present;
- HUD stays native and never enters FG history.

## PIE

PIE uses the same generator, writes the interpolated result into the PIE
display target and leaves HUD/ImGui composition to the editor.

## RHI

`PresentGeneratedFrame` is a separate present path:

- waits for the frame fence;
- acquires its own image;
- blits;
- submits;
- presents.

It does not reuse the real frame's present.

## Resize

Editor and PIE generated targets are recreated when the viewport size changes.
The generator history is reset at the same time, so a stale target cannot be
sampled by the temporal resolve.

## Fallback

If generation fails:

- real frame stays visible;
- no black frame;
- HUD remains native.

`PresentGeneratedFrame` rejects invalid or destroyed targets. This is covered by
the GPU test:

```text
generated present=1
invalid generated present=0
```

## Tests

- `static_bake_gpu: generated present=1`;
- `static_bake_gpu: invalid generated present=0`;
- `static_bake_gpu: generated present with HUD=1`;
- the generated-present test fails the process if HUD present fails or an
  invalid target is accepted;
- `settings_test: OK`;
- optimization gate.

## Limitations

- PIE writes the generated result into the PIE display texture instead of a
  second swapchain present;
- runtime validation of the HUD overlay path is still required.
