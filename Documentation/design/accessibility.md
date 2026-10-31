# Accessibility contract

Accessibility is one profile applied to the frame, not a set of scattered
switches. The runtime section drives the game, the presentation section drives
the HUD, and the editor section drives the ImGui window. Complete Reference
chapter 13 documents the primitives; this document records the profile, the
project keys and the GPU composite path.

## One profile

`Source/Engine/UI/Accessibility.hpp` defines `AccessibilityProfile` with three
POD sections:

- `runtime` (`AccessibilitySettings`): blind, deaf, flash guard, colour vision
  and its strength;
- `presentation`: OLED orbit, motion scale, HUD idle fade, minimum contrast;
- `editor`: blind/deaf/high-contrast/reduce-motion/colour-vision for the editor
  window itself.

`DefaultAccessibilityProfile()` is the 1.0 policy: flash guard and OLED orbit on,
no correction, full motion.

`AccessibilityKernel::ApplyProfile()` is the single apply entry point. It copies
the profile, clamps hostile values (strength 0..1, flash threshold 0.02..0.45,
motion 0..1, contrast 1..21), stores the runtime section and reconfigures the
flash guard. `Profile()` returns the live profile, so individual setters and a
saved profile cannot disagree.

`ProjectSettings` carries one `AccessibilityProfile accessibility`; `EditorSettings`
no longer duplicates the IDE switches, because they are the profile's `editor`
section. `project.femproj` keeps the old keys (`[accessibility]`, `[editor]
ide_*`) and adds `profile_version`, `color_vision`,
`color_vision_strength`, `motion_scale`, `hud_fade_after_idle_seconds` and
`minimum_contrast_ratio`, so existing projects load unchanged.

`ProjectSettingsPanel::Apply()` calls `ApplyProfile()` once and derives the Master
mute from the same flags; `EditorSettingsPanel` edits the `editor` section and
applies it to the ImGui palette.

## Frame path

- `ApplyPostFx()` translates the profile into `Rhi::PostFxSettings`: the
  `PostFxFlashGuard` and `PostFxColorVision` bits, the flash residual gain and
  the colour-vision matrix selector. `PostFxPass` bakes the matrix rows into the
  composite constants and `postfx_composite.frag` applies both terms, so the
  shader and the kernel read one profile.
- The flash residual is the guard's live gain (`CurrentGain()`), not the
  configured floor, so a calm frame is never dimmed by default.
- `fby_sandbox` owns an `AccessibilityKernel`, applies the default profile once,
  toggles blind/deaf/flash/colour vision with F2-F5 and calls `ApplyToFrame()` on
  every packet. The Clean Slate `GameMain.cpp` does the same around `Submit()`,
  with a comment where `BuildSubtitleCanvas()` composes the caption overlay.
- The editor composes captions and applies the project profile to the PIE
  viewport packet only, so a game blind mode blanks the play view, never the
  authoring window.

## Captions

`AccessibilityOverlay` owns the subtitle canvas and the fixed storage.
`ComposeUiOverlay()` appends the UI items after the game items, offsets the
overlay batches by the game item count and stamps the font texture array.
`fby_sandbox` builds the overlay every frame, so deaf mode shows a real caption
panel; the Clean Slate template carries the same call. The composition is
CPU-only and refuses when storage is short; an empty overlay is a no-op.

## CPU colour fallback

`ApplyCpuColorFallback()` corrects the clear colour and every item colour with the
same matrix and guard gain for a path without a final composite. It is exact for
the clear colour and approximate for alpha-blended sprites, and it is mutually
exclusive with `ApplyPostFx()`.

## GPU composite

- `Shaders/accessibility_composite.frag` samples the scene image and applies the
  colour-vision matrix and the flash residual gain from one push block.
- Every render target keeps a companion scene image, view, framebuffer and
  descriptor set. `RenderToTarget` redirects the scene there when
  `AccessibilityCompositeWanted()` is true, runs one fullscreen pass into the
  target, then hits the capture barrier. Pipeline, layout, sampler and pool are
  created once; without them the old path renders (fail closed).
- The swapchain path keeps a private scene image, view, framebuffer and
  descriptor set sized with the swapchain (recreated with it). `Submit` renders
  into it when the policy asks, and `EndFrame` composites into the acquired
  swapchain image.
- `Shaders/accessibility_flash.comp` runs before the composite with an R8
  luminance history per pixel and clamps a sudden delta to the residual gain. It
  stores raw luminance, so a static bright frame stops being dimmed after the
  first frame; the composite owns colour vision only, so the two terms cannot
  darken a frame twice.
- Both the offscreen and swapchain paths keep their own history image and
  descriptor set, so sandbox and PIE share one behaviour and every resource is
  recreated with the target that owns it.

## Render graph

The render graph executes compute passes, but its graphics and post records
(`RenderCommandRecord`) are still not executed by any backend, so `PostFxPass` is
not reachable today. The accessibility passes run inside `Submit` and
`RenderToTarget`, where the backend owns the images.

## Tests

`ui_accessibility` (defaults, clamping, live view, frame mapping, `ApplyPostFx`
flags/matrix/gain, caption append order and batch offset) and
`accessibility_composite` (8x8 offscreen target on a real Vulkan device:
baseline, deuteranopia matrix, zero-correction byte-for-byte baseline, 0.5
residual dimming, lone flash flag, dark-to-bright clamp, unguarded control,
flagged swapchain submit). The composite test skips without a Vulkan device.

## Known limit

The editor's BlindMode narrates to LogBoy but does not yet speak focused-widget
labels; hooking ImGui's nav focus would complete it. Tracked in
[ROADMAP.md](../ROADMAP.md).
